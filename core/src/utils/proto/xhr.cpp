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
#include <sstream>
#include <stdexcept>
#include <vector>
#include <utils/flog.h>
#include <utils/net.h>
#include <utils/proto/http.h>
#include <utils/proto/xhr.h>


namespace net::xhr {
    
    // TODO: check
    std::string url_decode(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(s[i+1]) && std::isxdigit(s[i+2])) {
                int val;
                std::stringstream ss;
                ss << std::hex << s.substr(i+1, 2);
                ss >> val;
                out += static_cast<char>(val);
                i += 2;
            } else if (s[i] == '+') {
                out += ' ';
            } else {
                out += s[i];
            }
        }
        return out;
    }
    
    UrlParts parseUrl(const std::string& url) {
        UrlParts parts;
        std::string::size_type pos = 0;
    
        auto fail = [&](const std::string& msg) {
            throw std::invalid_argument("URL parse error: " + msg + " [\"" + url + "\"]");
        };
    
        // scheme:// (mandatory if scheme present)
        auto schemeEnd = url.find("://");
        if (schemeEnd != std::string::npos) {
            parts.scheme = url.substr(0, schemeEnd);
            if (parts.scheme.empty()) {
                fail("empty scheme");
            }
            pos = schemeEnd + 3;
        } else {
            // check for invalid "scheme:host" form
            auto colonBeforeSlash = url.find(':');
            auto firstSlash = url.find('/');
            if (colonBeforeSlash != std::string::npos &&
                (firstSlash == std::string::npos || colonBeforeSlash < firstSlash)) {
                fail("invalid scheme (missing //)");
            }
        }
    
        // host[:port]
        auto pathStart = url.find('/', pos);
        parts.hostPort = (pathStart == std::string::npos)
                         ? url.substr(pos)
                         : url.substr(pos, pathStart - pos);
    
        if (parts.hostPort.empty()) {
            fail("empty host");
        }
    
        parts.port = -1;
        auto colonPos = parts.hostPort.rfind(':');
        if (colonPos != std::string::npos && colonPos > 0) {
            parts.host = parts.hostPort.substr(0, colonPos);
            auto portStr = parts.hostPort.substr(colonPos + 1);
            if (portStr.empty()) {
                fail("empty port");
            }
            try {
                int p = std::stoi(portStr);
                if (p <= 0 || p > 65535) {
                    fail("invalid port range");
                }
                parts.port = p;
            } catch (...) {
                fail("invalid port format");
            }
        } else {
            parts.host = parts.hostPort;
        }
    
        if (parts.host.empty()) {
            fail("empty host");
        }
    
        // path
        if (pathStart != std::string::npos) {
            parts.path = url.substr(pathStart);
        } else {
            parts.path = "/";
        }
        // default ports
        if (parts.port < 0) {
            if (parts.scheme == "https" || parts.scheme == "wss") parts.port = 443;
            else parts.port = 80;
        }
        return parts;
    }
    
    std::string request_xhr(const std::string& url) {
        flog::debug("request {}", url);
        auto urlParts = parseUrl(url);
        auto controlSock = net::connect(urlParts.host, urlParts.port);
        auto controlHttp = net::http::Client(controlSock);
    
        // Make request
        net::http::RequestHeader rqhdr(net::http::METHOD_GET, urlParts.path, urlParts.host);
        controlHttp.sendRequestHeader(rqhdr);
        net::http::ResponseHeader rshdr;
        controlHttp.recvResponseHeader(rshdr, 5000);
    
        flog::debug("response from {} {}", url, rshdr.getStatusString());
    
        std::string response;
        if (rshdr.hasField("Content-Length")) {
            std::string contentLenStr = rshdr.getField("Content-Length");
            size_t contentLen = 0;
            if (!contentLenStr.empty()) {
                contentLen = std::stoul(contentLenStr);
                flog::debug("ContentLength: {}", contentLen);
            } else {
                flog::debug("Unknown ContentLength");
            }
            std::vector<uint8_t> buf(8192);
            size_t totalRead = 0;
            while (totalRead < contentLen) {
                size_t toRead = std::min(buf.size(), contentLen - totalRead);
                auto len = controlSock->recv(buf.data(), toRead);
                if (len <= 0) break;
                response.append((char*)buf.data(), len);
                totalRead += len;
            }
        } else if (rshdr.hasField("Transfer-Encoding") && rshdr.getField("Transfer-Encoding")=="chunked") {
            std::vector<uint8_t> buf(8192);
            while (true) {
                // 1. read string with chunk size (hex)
                std::string line;
                while (true) {
                    char c;
                    auto len = controlSock->recv((uint8_t*)&c, 1);
                    if (len <= 0) break; // connection end or error
                    if (c == '\n') break;
                    if (c != '\r') line += c;
                }
        
                if (line.empty()) break; // end of stream
                size_t chunkSize = 0;
                try {
                    chunkSize = std::stoul(line, nullptr, 16);
                } catch (...) {
                    break; // size parse error
                }
        
                if (chunkSize == 0) break; // the last chunk
        
                size_t totalRead = 0;
                while (totalRead < chunkSize) {
                    size_t toRead = std::min(buf.size(), chunkSize - totalRead);
                    auto len = controlSock->recv(buf.data(), toRead);
                    if (len <= 0) break;
                    response.append((char*)buf.data(), len);
                    totalRead += len;
                }
        
                // read CRLF after chunk
                char crlf[2];
                controlSock->recv((uint8_t*)crlf, 2);
            }    
        }
        controlSock->close();    
        return response;
    }
}
