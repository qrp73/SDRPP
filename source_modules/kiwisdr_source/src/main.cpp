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
#define IMGUI_DEFINE_MATH_OPERATORS
#ifdef _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#endif

#include <filesystem>
#include <chrono>
#include <fstream>
#include <utils/flog.h>
#include <module.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <config.h>
#include <imgui.h>
#include <gui/gui.h>
#include <gui/smgui.h>

#include "kiwisdr.h"
#include "kiwisdr_map.h"


inline int64_t currentTimeMillis() {
    std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
    auto msec = std::chrono::time_point_cast<std::chrono::milliseconds>(t1).time_since_epoch().count();
    return static_cast<int64_t>(msec);
}


SDRPP_MOD_INFO{
    /* Name:            */ "kiwisdr_source",
    /* Description:     */ "KiwiSDR WebSDR source module for SDR++",
    /* Author:          */ "qrp73; san",
    /* Version:         */ 0, 2, 0,
    /* Max instances    */ 1
};


ConfigManager config;

struct KiwiSDRSourceModule : public ModuleManager::Instance {

    std::string kiwisdrSite = "sk6ag1.ddns.net:8071";
    std::string kiwisdrQTH = "";
    std::string kiwisdrLoc = "";
    //    std::string kiwisdrSite = "kiwi-iva.aprs.fi";
    KiwiSDRClient kiwiSdrClient;
    std::string root;
    KiwiSDRMapSelector selector;
    
    
    KiwiSDRSourceModule(std::string name, const std::string &root) : kiwiSdrClient(), selector(root, &config, "KiwiSDR Source") {
        this->name = name;
        this->root = root;

        config.acquire();
        if (config.conf.contains("kiwisdr_site")) {
            kiwisdrSite = config.conf["kiwisdr_site"];
        }
        if (config.conf.contains("kiwisdr_qth")) {
            kiwisdrQTH = config.conf["kiwisdr_qth"];
        }
        if (config.conf.contains("kiwisdr_loc")) {
            kiwisdrLoc = config.conf["kiwisdr_loc"];
        }
        config.release(false);


        kiwiSdrClient.init(kiwisdrSite);

        // Yeah no server-ception, sorry...
        // Initialize lists
        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        kiwiSdrClient.onConnected = [&]() {
            connected = true;
            tune(lastTuneFrequency, this);
        };

        kiwiSdrClient.onDisconnected = [&]() {
            connected = false;
            running = false;
            gui::mainWindow.setPlayState(false);
        };



        // Load config

        sigpath::sourceManager.registerSource("KiwiSDR", &handler);
    }

    ~KiwiSDRSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("KiwiSDR");
    }

    void postInit() {
    }

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    static void menuSelected(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        core::setInputSampleRate(12000); // fixed for kiwisdr
        flog::info("KiwiSDRSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        flog::info("KiwiSDRSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        if (_this->running) { return; }
        _this->running = true;
        _this->kiwiSdrClient.start();
        _this->setCurrentStreamTime(0); // not started
        _this->nextSend = 0;
        _this->timeSet = false;
        std::thread feeder([=]() {
            double nextSend = 0;
            while (_this->running) {
                _this->kiwiSdrClient.iqDataLock.lock();
                auto bufsize = _this->kiwiSdrClient.iqData.size();
                _this->kiwiSdrClient.iqDataLock.unlock();
                double now = (double)currentTimeMillis();
                if (nextSend == 0) {
                    if (bufsize < 200) {
                        threading::sleep(16); // some sleep
                        continue;      // waiting for initial batch
                    }
                    nextSend = now;
                }
                else {
                    auto delay = nextSend - now;
                    if (delay > 0) {
                        threading::sleep(delay);
                    }
                }
                std::vector<std::complex<float>> toSend;
                int bufferSize = 0;
                _this->kiwiSdrClient.iqDataLock.lock();
                if (_this->kiwiSdrClient.iqData.size() >= 200) {
                    for (int i = 0; i < 200; i++) {
                        toSend.emplace_back(_this->kiwiSdrClient.iqData[i]);
                    }
                    _this->kiwiSdrClient.iqData.erase(_this->kiwiSdrClient.iqData.begin(), _this->kiwiSdrClient.iqData.begin() + 200);
                    bufferSize = _this->kiwiSdrClient.iqData.size();
                }
                _this->kiwiSdrClient.iqDataLock.unlock();
                if (bufferSize > _this->kiwiSdrClient.NETWORK_BUFFER_SIZE) {
                    nextSend += 1000.0 / 120.0;
                }
                else {
                    nextSend += 1000.0 / 60.0;
                }
                int64_t ctm = currentTimeMillis();
                if (!toSend.empty()) {
                    //                    flog::info("{} Sending samples! buf remain = {}", ctm, bufferSize);
                    memcpy(_this->stream.writeBuf, toSend.data(), toSend.size() * sizeof(dsp::complex_t));
                    _this->stream.swap((int)toSend.size());
                }
                else {
                    nextSend = 0;
                    //                    flog::info("{} Underflow of KiwiSDR iq data!", ctm);
                }
                int64_t newStreamTime = currentTimeMillis() - (bufferSize / _this->kiwiSdrClient.IQDATA_FREQUENCY) - 500; // just 500.
                if (!_this->timeSet) {
                    _this->setCurrentStreamTime(newStreamTime);
                    _this->timeSet = true;
                }
                else {
                    if (_this->getCurrentStreamTime() < newStreamTime) {
                        _this->setCurrentStreamTime(newStreamTime);
                    }
                }
            }
        });
        feeder.detach();

        _this->running = true;
        flog::info("KiwiSDRSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->kiwiSdrClient.stop();

        _this->running = false;
        flog::info("KiwiSDRSourceModule '{0}': Stop!", _this->name);
    }

    std::vector<dsp::complex_t> incomingBuffer;

    double nextSend = 0;

    void incomingSample(double i, double q) {
        incomingBuffer.emplace_back(dsp::complex_t{ (float)q, (float)i });
        if (incomingBuffer.size() >= 200) { // 60 times per second
            double now = (double)currentTimeMillis();
            if (nextSend == 0) {
                nextSend = now;
            }
            else {
                auto delay = nextSend - now;
                if (delay > 0) {
                    threading::sleep(delay);
                }
            }
            //            flog::info("Sending samples: {}", incomingBuffer.size());
            nextSend += 1000.0 / 60.0;
            incomingBuffer.clear();
        }
    }


    int64_t lastTuneFrequency = 14100;

    static void tune(double freq, void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        _this->lastTuneFrequency = freq;
        if (_this->running && _this->connected) {
            _this->kiwiSdrClient.tune(_this->lastTuneFrequency, KiwiSDRClient::TUNE_IQ);
        }
        flog::info("KiwiSDRSourceModule '{0}': Tune: {1}!", _this->name, utils::formatFreq(_this->lastTuneFrequency));
    }


    static void menuHandler(void* ctx) {

        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;

        if (core::args["server"].b()) {

        } else {
            // local ui
            ImGui::BeginDisabled(gui::mainWindow.isPlaying());
            if (doFingerButton("Choose on map...")) {
                _this->selector.openPopup();
            }
            ImGui::EndDisabled();

            _this->selector.drawPopup([=](const std::string &hostPort, const std::string &qth, const std::string &loc) {
                _this->kiwisdrSite = hostPort;
                _this->kiwisdrQTH = qth;
                _this->kiwisdrLoc = loc;
                config.acquire();
                config.conf["kiwisdr_site"] = _this->kiwisdrSite;
                config.conf["kiwisdr_qth"] = _this->kiwisdrQTH;
                config.conf["kiwisdr_loc"] = _this->kiwisdrLoc;
                config.release(true);
                _this->kiwiSdrClient.init(_this->kiwisdrSite);
            });
        }




        SmGui::Text(("url: " + _this->kiwisdrSite).c_str());
        SmGui::Text(("QTH: " + _this->kiwisdrQTH).c_str());
        SmGui::Text(("Loc: " + _this->kiwisdrLoc).c_str());
        SmGui::Text(("Status: " + std::string(_this->kiwiSdrClient.connectionStatus)).c_str());

        int64_t cst = _this->getCurrentStreamTime();
        std::time_t t = cst / 1000;
        auto tmm = std::localtime(&t);
        char streamTime[64];
        strftime(streamTime, sizeof(streamTime), "%Y-%m-%d %H:%M:%S", tmm);
        SmGui::Text(("Stream pos: " + std::string(streamTime)).c_str());
        
        static bool agc;
        static bool hang;
        static int thresh;
        static int slope;
        static int decay;
        static int manGain;
        _this->kiwiSdrClient.getAgc(&agc, &hang, &thresh, &slope, &decay, &manGain);
        bool changed = false;
        
        // AGC enable
        changed |= ImGui::Checkbox("AGC", &agc);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Hang", &hang);
        
        
        // Sliders
        float labelWidth = ImGui::CalcTextSize("Threshold").x + 10.0f;
        auto SliderIntLeft = [&](const char* label, const char* id, int* v, int min, int max, const char* fmt) -> bool {
            ImGui::AlignTextToFramePadding();
            ImGui::SetCursorPosX(ImGui::GetCursorStartPos().x);
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorStartPos().x + labelWidth);
            ImGui::FillWidth();
            return ImGui::SliderInt(id, v, min, max, fmt);
        };
        if (agc) {
            //ImGui::BeginDisabled(!agc);
            changed |= SliderIntLeft("Threshold", "##thresh_kiwisdr", &thresh, -130, 0,    "%d dB");
            changed |= SliderIntLeft("Slope",     "##slope_kiwisdr",  &slope,     0, 10,   "%d dB");
            changed |= SliderIntLeft("Decay",     "##decay_kiwisdr",  &decay,    20, 5000, "%d ms");
            //ImGui::EndDisabled();
        } else {
            //ImGui::BeginDisabled(agc);
            changed |= SliderIntLeft("Gain",      "##gain_kiwisdr",   &manGain,  0, 120, "%d dB");
            //ImGui::EndDisabled();        
        }
        
        // Apply if changed
        if (changed) {
            _this->kiwiSdrClient.setAgc(agc, hang, thresh, slope, decay, manGain);
        }
    }

    
    std::atomic<long long> _currentStreamTime = 0;  // unix time millis. 0 means realtime, otherwise simulated time.
    int _secondsAdjustment = 0;                     // adjust for ft8 decode when local time mismatches
    
    int64_t getCurrentStreamTime() {
        auto ctm =  _currentStreamTime.load();
        if (ctm == 0) {
            ctm = currentTimeMillis();
        }
        return ctm + _secondsAdjustment * 1000L;
    }
    void setCurrentStreamTime(int64_t x) {
        _currentStreamTime = x;
    }




    std::string name;
    bool enabled = true;
    bool running = false;
    bool connected = false;
    bool timeSet = false;

    double freq;
    bool serverBusy = false;

    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;

    std::shared_ptr<KiwiSDRClient> client;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(std::string(core::getRoot()) + "/kiwisdr_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    auto root = std::string(core::getRoot());
    return new KiwiSDRSourceModule(name, root);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (KiwiSDRSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
