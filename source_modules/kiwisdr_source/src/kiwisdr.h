/* 
 * This file is based on code from project SDRPlusPlusBrown (https://github.com/sannysanoff/SDRPlusPlusBrown)
 * originally licensed under the GNU General Public License, version 3 (GPLv3).
 *
 * Modified and extended by qrp73, 2025.
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
#include <complex>
#include <atomic>
#include <thread>
#include <core.h>
#include <atomic>
#include <dsp/types.h>
#include <utils/flog.h>
#include <utils/threading.h>
#include <utils/freq_formatting.h>
#include <utils/proto/websock.h>
#include <utils/proto/xhr.h>



inline int64_t parse_khz_to_hz(const std::string& value) {
    size_t dot = value.find('.');
    std::string int_part = value.substr(0, dot);
    std::string frac_part = (dot != std::string::npos) ? value.substr(dot + 1) : "0";
    // pad frac_part to 3 digits (since we're converting .XYZ kHz to XYZ Hz)
    while (frac_part.length() < 3) frac_part += '0';
    if (frac_part.length() > 3) frac_part = frac_part.substr(0, 3);
    int64_t freq_hz = std::stoll(int_part) * 1000 + std::stoll(frac_part);
    return freq_hz;
}


struct KiwiSDRClient {
    net::websock::WSClient wsClient;
    std::string hostPort;
    bool connected;
    char connectionStatus[100];
    std::atomic<bool> running;
    std::vector<int64_t> times;

    std::function<void()>                       onConnected     = []() {};
    std::function<void()>                       onDisconnected  = []() {};
    std::function<void(const std::string&)>     onError         = [](const auto&) {};
    std::vector<std::complex<float>>            iqData;
    std::mutex                                  iqDataLock;


    inline int64_t currentTimeMillis() {
        std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
        auto msec = std::chrono::time_point_cast<std::chrono::milliseconds>(t1).time_since_epoch().count();
        return static_cast<int64_t>(msec);
    }
    

    virtual ~KiwiSDRClient() {
        running = false;
        flog::info("KiwiSDRClient: destructor");
        running = false;
    }

    int IQDATA_FREQUENCY = 12000;
    int NETWORK_BUFFER_SECONDS = 1;//2;
    int NETWORK_BUFFER_SIZE = NETWORK_BUFFER_SECONDS * IQDATA_FREQUENCY;

    
    std::map<std::string, std::string> _keyValues;

    void keyValue_onReceived(const std::string& key, const std::string& value) {
        auto it = _keyValues.find(key);
        if (it == _keyValues.end() || it->second != value) {
            _keyValues[key] = value;
            
            // skip json configs
            if ((key.length() >= 3 && key.compare(key.length() - 3, 3, "cfg") == 0) ||
                key == "last_community_download") 
            {
                return;
            }            
            flog::debug("  {:s} = {:s}", key, value);
            if (key == "freq_offset") {
                serverFrequencyOffset = parse_khz_to_hz(value);
                tune(currentFrequency, currentModulation);
                flog::info("  serverFrequencyOffset = {:s}", utils::formatFreq(serverFrequencyOffset));
            }
            if (key == "kiwi_kick") {
                flog::info("  kiwi_kick: {:s}\n", value);
            }
        }    
    }

    void msg_onReceived(const std::string& msg) {
        std::istringstream iss(msg);
        std::string token;
        // Skip "MSG"
        iss >> token;
        while (iss >> token) {
            size_t eq = token.find('=');
            if (eq != std::string::npos) {
                std::string key = token.substr(0, eq);
                std::string value = token.substr(eq + 1);
                keyValue_onReceived(key, net::xhr::url_decode(value));
            }
        }    
    }
    void snd_onReceived(const std::string& msg) {
        auto ctm = currentTimeMillis();
        times.emplace_back(ctm);
        int lastSecondCount = 0;
        for (int q = times.size() - 1; q >= 0; q--) {
            if (times[q] < ctm - 1000) {
                break;
            }
            lastSecondCount++;
        }
        while (!times.empty() && times.front() < ctm - 2000) {
            times.erase(times.begin());
        }
        snprintf(connectionStatus, sizeof connectionStatus, "Receiving. %d kB/sec (%d)", (lastSecondCount * ((int)msg.size())) / 1024, lastSecondCount);
        int IQ_HEADER_SIZE = 20;
        int REAL_HEADER_SIZE = 10;
        if (currentModulation == TUNE_REAL && msg.size() == 1024 + REAL_HEADER_SIZE) { // REAL data
            auto scan = msg.data();
            scan += REAL_HEADER_SIZE;

            if (scan != msg.data() + REAL_HEADER_SIZE) {
                flog::info("ERROR scan != msg.data() + REAL_HEADER_SIZE\n");
                abort(); // homegrown assert.
            }

            int16_t* ptr = (int16_t*)scan;
            snprintf(connectionStatus, sizeof connectionStatus, "Storing real..");
            iqDataLock.lock();
            for (int z = 0; z < 512; z++) {
                int16_t* iqsample = &ptr[z];
                char* fourbytes = (char*)iqsample;
                std::swap(fourbytes[0], fourbytes[1]);
#if 1
                float i = iqsample[0] / 32768.0; // iqsample[0] / 32767.0;
#else
                float i = (iqsample[0] + 0.5f) / (32768.0f - 0.5f);
#endif
                float q = 0;
                iqData.emplace_back(i, q);
            }
            while (iqData.size() > NETWORK_BUFFER_SIZE * 1.5) {
                iqData.erase(iqData.begin(), iqData.begin() + 200);
            }
            iqDataLock.unlock();
            snprintf(connectionStatus, sizeof connectionStatus, "Cont Recv. %d kB/sec (%d)", (lastSecondCount * ((int)msg.size())) / 1024, lastSecondCount);
        }
        if (currentModulation == TUNE_IQ && msg[3] == 0x08 && msg.size() == 2048 + IQ_HEADER_SIZE) { // IQ data
            auto scan = msg.data();
            scan += 4;
            auto sequence = *(int32_t*)scan;
            scan += 4;
            char one = *(char*)scan;
            scan += 1;
            auto info1 = *(int16_t*)scan;
            scan += 2;
            char zero = *(char*)scan;
            scan += 1;
            auto timestamp = *(int32_t*)scan;
            scan += 4;
            auto* arg2 = (int32_t*)scan;
            scan += 4;

            if (scan != msg.data() + IQ_HEADER_SIZE) {
                flog::info("ERROR scan != msg.data() + IQ_HEADER_SIZE\n");
                abort(); // homegrown assert.
            }

            int16_t* ptr = (int16_t*)scan;
            iqDataLock.lock();
            for (int z = 0; z < 512; z++) {
                int16_t* iqsample = &ptr[2 * z];
                char* fourbytes = (char*)iqsample;
                std::swap(fourbytes[0], fourbytes[1]);
                std::swap(fourbytes[2], fourbytes[3]);
#if 1
                float i = iqsample[0] / 32768.0; // iqsample[0] / 32767.0;
                float q = iqsample[1] / 32768.0; // iqsample[1] / 32767.0;
#else
                float i = (iqsample[0] + 0.5f) / (32768.0f - 0.5f);
                float q = (iqsample[1] + 0.5f) / (32768.0f - 0.5f);
#endif
                iqData.emplace_back(i, q);
            }
            while (iqData.size() > NETWORK_BUFFER_SIZE * 1.5) {
                iqData.erase(iqData.begin(), iqData.begin() + 200);
            }
            iqDataLock.unlock();
            //flog::info("{} Got sound: bytes={} sequence={} info1={} timestamp={} , {} samples, buflen now = {} (erased {})", currentTimeMillis(), msg.size(), sequence, info1, timestamp, (msg.size() - HEADER_SIZE) / 4, buflen, erased);
        }
    }

    

    void init(const std::string& hostport) {
        
        this->hostPort = hostport;
        strcpy(connectionStatus, "Not connected");

        wsClient.onDisconnected = [this]() {
            connected = false;
            this->onDisconnected();
            snprintf(connectionStatus, sizeof connectionStatus, "Disconnected");
        };

        wsClient.onConnected = [this]() {
            // x.sendString("SET mod=usb low_cut=300 high_cut=2700 freq=14100.000");
            wsClient.sendString("SET auth t=kiwi p=#");
            wsClient.sendString("SET AR OK in=" + std::to_string(IQDATA_FREQUENCY) + " out=48000");
            //wsClient.sendString("SERVER DE CLIENT sdr++brown SND");
            wsClient.sendString("SERVER DE CLIENT openwebrx.js SND");
            //wsClient.sendString("SET dbug_v=0,0,0,0,0");
            //wsClient.sendString("SET mod=am low_cut=-4000 high_cut=4000 freq=1035");
            wsClient.sendString("SET mod=iq low_cut=-6000 high_cut=6000 freq=1035.000");
            //setAgc(_agc, _hang, _threshold, _slope, _decay, _manualGain); //setAgc(0, 0, -100, 6, 1000, 50);
            //wsClient.sendString("SET agc=0 hang=0 thresh=-100 slope=6 decay=1000 manGain=50");
            //wsClient.sendString("SET browser=Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36");
            wsClient.sendString("SET browser=Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36");
            wsClient.sendString("SET compression=1");
            //wsClient.sendString("SET nb algo=0");
            //wsClient.sendString("SET nb type=0 en=0");
            //wsClient.sendString("SET nb type=1 en=1");
            //wsClient.sendString("SET nb type=2 en=0");
            //wsClient.sendString("SET nr algo=0");
            //wsClient.sendString("SET compression=0");
            wsClient.sendString("SET squelch=0 param=0.00");
            wsClient.sendString("SET keepalive");
            //wsClient.sendString("SET ident_user=SWL");
            
            connected = true;
            setAgc(_agc, _hang, _threshold, _slope, _decay, _manualGain); //setAgc(0, 0, -100, 6, 1000, 50);
            
            if (this->onConnected) {
                onConnected();
            }
            strcpy(connectionStatus, "Connected, waiting data...");
            //            wsClient.sendString("SET mod=iq low_cut=-5000 high_cut=5000 freq=14074.000");
        };
        wsClient.onTextMessage = [this](const std::string& msg) {
            flog::warn("kiwisdr.onTextMessage: {}", msg);
        };

        wsClient.onBinaryMessage = [this](const std::string& msg) {
        //wsClient.onBinaryMessage = [&](const std::vector<uint8_t>& payload) {
        //    const std::string& msg = std::string(payload.begin(), payload.end());
            
            std::string start = "???";
            if (msg.size() > 3) {
                start = msg.substr(0, 3);
            }
            if (start == "MSG") {
                msg_onReceived(msg);
            }
            else if (start == "SND") {
                snd_onReceived(msg);
            }
            else {
                //flog::warn("kiwisdr.onBinaryMessage: unknown message {}: {} bytes", start, msg.size(), start);
            }
        };
        auto lastPing = currentTimeMillis();
        wsClient.onEveryReceive = [this, lastPing] () mutable {
            auto ts = currentTimeMillis();
            if ((ts - lastPing) > 3000) {
                wsClient.sendString("SET keepalive");
                lastPing = ts;
            }
        };
    }


    enum Modulation {
        TUNE_IQ = 1,
        TUNE_REAL = 2,           // only real data, -3 .. +3 kHz
    };

    int64_t     serverFrequencyOffset = 0;
    int64_t     currentFrequency = 0;
    Modulation currentModulation = Modulation::TUNE_IQ;
    
    bool        _agc        = true;
    bool        _hang       = false;
    int32_t     _threshold  = -100;
    int32_t     _slope      = 6;
    int32_t     _decay      = 1000;
    int32_t     _manualGain = 30;

    void tune(int64_t freq, Modulation mod) {
        currentFrequency = freq;
        freq -= serverFrequencyOffset;
        char buf[1024];
        switch (mod) {
        case Modulation::TUNE_IQ:
            currentModulation = mod;
            snprintf(buf, sizeof buf, "SET mod=iq low_cut=-7000 high_cut=7000 freq=%.3f", freq / 1000.0);
            break;
        case Modulation::TUNE_REAL:
            currentModulation = mod;
            snprintf(buf, sizeof(buf), "SET mod=usb low_cut=0 high_cut=8000 freq=%.3f", (freq - 3000) / 1000.0);
            break;
        }
        //flog::debug("ws: {:s}", buf);
        wsClient.sendString(buf);
    }
    
    void setAgc(bool agc, bool hang, int thresh, int slope, int decay, int manGain) {
        _agc        = agc  != 0;
        _hang       = hang != 0;
        _threshold  = std::clamp<int>(thresh, -130, 0);     // -100 dB
        _slope      = std::clamp<int>(slope,     0, 10);    // +6 dB
        _decay      = std::clamp<int>(decay,    20, 5000);  // 1000 msec
        _manualGain = std::clamp<int>(manGain,   0, 120);   // +50 dB
        if (connected) {
            char buf[1024];
            snprintf(buf, sizeof buf, "SET agc=%d hang=%d thresh=%d slope=%d decay=%d manGain=%d", agc?1:0, hang?1:0, _threshold, _slope, _decay, _manualGain);
            //flog::debug("ws: {:s}", buf);
            wsClient.sendString(buf);
        }
    }
    void getAgc(bool* agc, bool* hang, int* thresh, int* slope, int* decay, int* manGain) {
        if (agc != nullptr)     *agc = _agc;
        if (hang != nullptr)    *hang = _hang;
        if (thresh != nullptr)  *thresh = _threshold;
        if (slope != nullptr)   *slope = _slope;
        if (decay != nullptr)   *decay = _decay;
        if (manGain != nullptr) *manGain = _manualGain;
    }

    void stop() {
        strcpy(connectionStatus, "Disconnecting..");
        wsClient.stopSocket();
        strcpy(connectionStatus, "Disconnecting2..");
        while (running) {
            threading::sleep(100);
        }
        strcpy(connectionStatus, "Disconnected.");
    }

    void start() {
        strcpy(connectionStatus, "Connecting..");
        running = true;
        std::thread looper([&]() {
            threading::setThreadName("kiwisdr.wscli");
            flog::info("calling x.connectAndReceiveLoop..");
            try {
                iqDataLock.lock();
                iqData.clear();
                iqDataLock.unlock();
                _keyValues.clear();
                serverFrequencyOffset = 0LL;
                
                uint64_t ts = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
                try {
                    auto vert = net::xhr::request_xhr("http://"+hostPort+"/VER"); //"http://wessex.hopto.org:8075/VER"
                    nlohmann::json verj = nlohmann::json::parse(vert);
                    if (verj.contains("ts")) {
                        if (verj["ts"].is_number_unsigned()) {
                            ts = verj["ts"].get<uint64_t>();
                        } else if (verj["ts"].is_string()) {
                            try {
                                ts = std::stoull(verj["ts"].get<std::string>());
                            } catch (const std::exception& e) {
                                flog::exception(e);
                            } catch (...) {
                                flog::exception();
                            }
                        }
                    }
                } catch (...) {
                    flog::exception();
                }
                
                wsClient.connectAndReceiveLoop("ws://"+hostPort+"/kiwi/"+std::to_string(ts)+"/SND");
                
                flog::info("x.connectAndReceiveLoop exited.");
                strcpy(connectionStatus, "Disconnected");
                running = false;
            } catch (const std::exception& e) {
                flog::exception(e);
                strcpy(connectionStatus, ("Error: "+std::string(e.what())).c_str());
                running = false;
                this->onError(e.what());
            } catch (...) {
                flog::exception();
                strcpy(connectionStatus, "Error: unknown exception");
                running = false;
                this->onError("unknown exception");
            }
        });
        looper.detach();
    }
};
