/* 
 * This file is part of the SDRPP distribution (https://github.com/qrp73/SDRPP).
 * Copyright (c) 2025 qrp73.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <map>
#include <string>
#include <stdio.h>
#include <random>
#include <stdexcept>

#include <sstream>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#include <thread>
#include <chrono>

#include <utils/flog.h>
#include "../net.h"
#include "http.h"
#include "xhr.h"



namespace net::websock {

    inline std::vector<std::string> stringSplit(const std::string &str, const std::vector<std::string> &separators) {
        std::vector<std::string> result;
        size_t start = 0;
        while (start <= str.size()) {
            size_t pos = std::string::npos;
            size_t sepLen = 0;
            for (const auto &sep : separators) {
                size_t p = str.find(sep, start);
                if (p != std::string::npos && (pos == std::string::npos || p < pos)) {
                    pos = p;
                    sepLen = sep.size();
                }
            }
            if (pos == std::string::npos) {
                result.push_back(str.substr(start));
                break;
            }
            result.push_back(str.substr(start, pos - start));
            start = pos + sepLen;
            if (start == str.size()) {
                result.push_back("");
                break;
            }
        }
        return result;
    }


    struct WSClient {
        std::shared_ptr<::net::Socket>  _socket;
        std::mutex                      _socketMutex;
        std::string                     _path;
        std::string                         _secKey;
        std::random_device                  _rd;
        std::mt19937                        _gen;
        std::uniform_int_distribution<int>  _dist;


        WSClient() : _socket(), _gen(_rd()), _dist(0, 255) {
        }

        void sendString(const std::string& msg) {
            sendFrame(TEXT_FRAME, msg.data(), msg.size());
            //flog::debug("WSClient.sendString: {0}", msg);
        }
    
        void sendBinary(const std::vector<uint8_t>& data) {
            sendFrame(BINARY_FRAME, data.data(), data.size());
        }

        std::function<void(const std::string&)> onTextMessage = [](auto){};
        std::function<void(const std::string&)> onBinaryMessage = [](auto){};
        std::function<void()> onConnected = [](){};
        std::function<void()> onDisconnected = [](){};
        std::function<void()> onEveryReceive = [](){};

        
        
        
        std::string computeAccept(const std::string& key) {
            std::string concat = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            uint8_t sha[SHA_DIGEST_LENGTH];
            SHA1((const uint8_t*)concat.c_str(), concat.size(), sha);
            return base64(sha, SHA_DIGEST_LENGTH);
        }
        std::string genSecKey() {
            uint8_t key[16];
            for (int i = 0; i < 16; i++) key[i] = _dist(_gen);
            return base64(key, 16);
        }
        std::string base64(const uint8_t* data, size_t len) {
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO* mem = BIO_new(BIO_s_mem());
            BIO_push(b64, mem);
            BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
            BIO_write(b64, data, len);
            BIO_flush(b64);
            BUF_MEM* bptr;
            BIO_get_mem_ptr(b64, &bptr);
            std::string out(bptr->data, bptr->length);
            BIO_free_all(b64);
            return out;
        }
        
        enum Opcode {
            CONTINUATION = 0x0,
            TEXT_FRAME   = 0x1,
            BINARY_FRAME = 0x2,
            CLOSE_FRAME  = 0x8,
            PING_FRAME   = 0x9,
            PONG_FRAME   = 0xA
        };        
        void sendFrame(Opcode opcode, const void* data, size_t len, bool fin = true) {
            std::vector<uint8_t> frame;
            uint8_t firstByte = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
            frame.push_back(firstByte);
    
            uint8_t maskBit = 0x80;
            if (len <= 125) {
                frame.push_back(maskBit | uint8_t(len));
            } else if (len <= 65535) {
                frame.push_back(maskBit | 126);
                frame.push_back((len >> 8) & 0xFF);
                frame.push_back(len & 0xFF);
            } else {
                frame.push_back(maskBit | 127);
                for (int i = 7; i >= 0; --i)
                    frame.push_back((len >> (8 * i)) & 0xFF);
            }
    
            uint8_t mask[4];
            for (int i = 0; i < 4; i++) mask[i] = _dist(_gen);
            frame.insert(frame.end(), mask, mask + 4);
    
            const uint8_t* p = (const uint8_t*)data;
            for (size_t i = 0; i < len; i++)
                frame.push_back(p ? (p[i] ^ mask[i % 4]) : mask[i % 4]);
    
            sendAll(frame);
        }
        Opcode recvFrame(std::vector<uint8_t>& payload) {
            payload.clear();
            bool fin = false;
            Opcode firstOpcode = CONTINUATION;
            do {
                uint8_t hdr[2];
                recvAll(hdr, sizeof(hdr));
                fin = (hdr[0] & 0x80) != 0;
                Opcode op = Opcode(hdr[0] & 0x0F);
                if (firstOpcode == CONTINUATION && op != CONTINUATION)
                    firstOpcode = op;
    
                bool masked = (hdr[1] & 0x80) != 0;
                uint64_t len = hdr[1] & 0x7F;
                if (len == 126) {
                    uint8_t ext[2]; recvAll(ext, sizeof(ext));
                    len = (ext[0] << 8) | ext[1];
                } else if (len == 127) {
                    uint8_t ext[8]; recvAll(ext, sizeof(ext));
                    len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
                }
    
                uint8_t mask[4] = {0};
                if (masked) recvAll(mask, sizeof(mask));
    
                std::vector<uint8_t> data(len);
                if (len) recvAll(data.data(), len);
                if (masked) {
                    for (size_t i = 0; i < len; i++)
                        data[i] ^= mask[i % 4];
                }
                payload.insert(payload.end(), data.begin(), data.end());
    
                if (op == PING_FRAME) {
                    sendFrame(PONG_FRAME, data.data(), data.size());
                } else if (op == CLOSE_FRAME) {
                    sendFrame(CLOSE_FRAME, nullptr, 0);
                    throw std::runtime_error("recvFrame: CLOSE_FRAME received");
                }
            } while (!fin);
            return firstOpcode;
        }        
        
        void handshake(std::string url) {
            auto urlParts = net::xhr::parseUrl(url);
            if (urlParts.scheme.empty()) {
                urlParts = net::xhr::parseUrl("ws://"+url);
            }
            flog::debug("connect {}://{}{}", urlParts.scheme, urlParts.hostPort, urlParts.path);
            
            openSocket(urlParts.host, urlParts.port);

            
            _secKey = genSecKey();
            std::ostringstream req;
            req << "GET " << urlParts.path << " HTTP/1.1\r\n"
                << "Host: " << urlParts.hostPort << "\r\n"
                //<< "Origin: http://" << host << ":" << port << "\r\n"
                << "Upgrade: websocket\r\n"
                << "Connection: Upgrade\r\n"
                //<< "sec-websocket-extensions: permessage-deflate; client_max_window_bits"
                << "Sec-WebSocket-Key: " << _secKey << "\r\n"
                << "Sec-WebSocket-Version: 13\r\n"
                //<< "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/108.0.0.0 Safari/537.36\r\n"
                << "\r\n";
            sendAll(req.str());

            std::string resp = recvHeaders();
            flog::info("recvHeaders: {}", resp);

            // process headers
            auto pos = resp.find("\r\n\r\n");
            if (pos == std::string::npos) {
                stopSocket();
                throw std::runtime_error("websock: invalid response, no header end");
            }
            // take only headers part
            std::string headersStr = resp.substr(0, pos);
            std::vector<std::string> lines = stringSplit(headersStr, {"\r\n"});

            // print all headers for debug
            //for (const auto& l : lines) {
            //    printf("lines: %s\n", l.c_str());
            //}

            // check status line contains 101
            const std::string& statusLine = lines[0];
            if (statusLine.find("101") == std::string::npos) {
                throw std::runtime_error("websock: handshake failed, status != 101");
            }

            // parse headers into map
            std::unordered_map<std::string, std::string> headers;
            for (size_t i = 1; i < lines.size(); ++i) { // skip status line
                auto sep = lines[i].find(':');
                if (sep != std::string::npos) {
                    std::string key = lines[i].substr(0, sep);
                    std::string value = lines[i].substr(sep + 1);
                    // trim spaces
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    // key to lower case
                    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::tolower(c); });
                    headers[key] = value;
                }
            }
            // print all headers for debug
            //for (const auto& [k,v] : headers) {
            //    printf("headers: %s: %s\n", k.c_str(), v.c_str());
            //}
            
            // check Upgrade header
            auto it = headers.find("upgrade");
            if (it == headers.end() || it->second != "websocket") {
                throw std::runtime_error("websock: handshake failed, missing Upgrade: websocket");
            }
            
            // check Sec-WebSocket-Accept
            it = headers.find("sec-websocket-accept");
            std::string secResponse = it != headers.end() ? it->second : "";
            std::string secExpected = computeAccept(_secKey);
            if (secResponse != secExpected) {
                flog::warn("sec-websocket-accept check failed\nexpected: {}\nresponse: {}", secExpected, secResponse);
                throw std::runtime_error("websock: handshake failed, bad Sec-WebSocket-Accept");
            }
        }

        
        void openSocket(const std::string& host, int port) {
            std::lock_guard<std::mutex> lock(_socketMutex);
            flog::debug("WSClient.openSocket()");
            if (_socket) {
                throw std::runtime_error("socket already open");
            }
            _socket = net::connect(Address(host, port));
        }
        void stopSocket() {
            std::lock_guard<std::mutex> lock(_socketMutex);
            flog::debug("WSClient.stopSocket()");
            if (!_socket) return;
            _socket->close();
            _socket.reset();
        }
        void sendAll(const uint8_t* data, size_t len) {
            std::lock_guard<std::mutex> lock(_socketMutex);
            if (!_socket) {
                throw std::runtime_error("socket closed");
            }
            size_t sent = 0;
            while (sent < len) {
                //int n = ::send(sock, (const char*)data + sent, len - sent, 0);
                int n = _socket->send(data + sent, len - sent);
                if (n <= 0) throw std::runtime_error(flog::formatSafe("send() fail: errno={}, {}", errno, strerror(errno)));
                sent += n;
            }
        }
        void recvAll(void* buf, size_t len) {
            std::lock_guard<std::mutex> lock(_socketMutex);
            if (!_socket) {
                throw std::runtime_error("socket closed");
            }
            size_t recvd = 0;
            while (recvd < len) {
                //int n = ::recv(sock, (char*)buf + recvd, len - recvd, 0);
                int n = _socket->recv((uint8_t*)buf + recvd, len - recvd, false, 5000);
                //if (n == 0) {
                //    continue;
                //}                
                if (n <= 0) throw std::runtime_error(flog::formatSafe("recv() fail: errno={}, {}", errno, strerror(errno)));
                recvd += n;
            }
        }
        std::string recvHeaders() {
            std::lock_guard<std::mutex> lock(_socketMutex);
            if (!_socket) {
                throw std::runtime_error("socket closed");
            }
            std::string hdrs;
            uint8_t c;
            while (hdrs.find("\r\n\r\n") == std::string::npos) {
                //int n = ::recv(sock, &c, 1, 0);
                int n = _socket->recv(&c, 1, false, 5000);
                //if (n == 0 && errno == 115) {
                //    continue;
                //}
                if (n <= 0) throw std::runtime_error(flog::formatSafe("recv() fail: errno={}, {}", errno, strerror(errno)));
                hdrs.push_back(c);
            }
            return hdrs;
        }
        void sendAll(const std::string& s) {
            sendAll((const uint8_t*)s.data(), s.size());
        }
        void sendAll(const std::vector<uint8_t>& v) {
            sendAll(v.data(), v.size());
        }
        
        
        void connectAndReceiveLoop(const std::string& url) {
            handshake(url);
            flog::info("WSClient socket connected");
            onConnected();
            try {
                std::vector<uint8_t> payload;
                while (_socket) {
                    Opcode op = recvFrame(payload);
                    if (op == TEXT_FRAME && onTextMessage) {
                        onTextMessage(std::string(payload.begin(), payload.end()));
                    } else if (op == BINARY_FRAME && onBinaryMessage) {
                        onBinaryMessage(std::string(payload.begin(), payload.end()));
                    } else {
                        flog::warn("recvFrame(): {}", (int)op);                    
                    }
                    onEveryReceive();
                }
            } catch(const std::runtime_error& e) {
                flog::debug("WSClient.connectAndReceiveLoop: stop with: {}", e.what());
            }
            if (_socket) {
                stopSocket();
            }
            onDisconnected();
        }
    };
}
