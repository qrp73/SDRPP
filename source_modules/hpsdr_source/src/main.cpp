/* 
 * This file is part of the SDRPP distribution (https://github.com/qrp73/SDRPP).
 * Copyright (c) 2023 qrp73.
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
#include "hpsdr.h"
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <gui/widgets/stepped_slider.h>
#include <dsp/routing/stream_link.h>
#include <utils/optionlist.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "hpsdr_source",
    /* Description:     */ "HPSDR source module",
    /* Author:          */ "qrp73",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class HpsdrSourceModule : public ModuleManager::Instance {
public:
    HpsdrSourceModule(std::string name) {
        this->name = name;

        // Define samplerates
        samplerates.clear();
        _srId = -1;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        sigpath::sourceManager.registerSource("HPSDR", &handler);
    }

    ~HpsdrSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("HPSDR");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    void refresh() {
        char mac[128];
        char buf[128];
        devices.clear();
        auto devList = hpsdr::discover();
        for (auto& d : devList) {
            sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", 
                d.mac[0], d.mac[1], d.mac[2], 
                d.mac[3], d.mac[4], d.mac[5]);
            sprintf(buf, "%s / %s v%d.%d", 
                mac,
                d.getBoardName(),
                d.verMajor, d.verMinor);
            devices.define(mac, buf, d);
        }
    }

    void selectMac(std::string mac) {
        // If the device list is empty, don't select anything
        if (!devices.size()) {
            selectedMac.clear();
            return;
        }

        // If the mac doesn't exist, select the first available one instead
        if (!devices.keyExists(mac)) {
            selectMac(devices.key(0));
            return;
        }

        // Default config
        _isAtt = false;
        _attGain = 0;

        // Load config
        devId = devices.keyId(mac);
        selectedMac = mac;
        config.acquire();

        samplerates.clear();
        _srId = -1;
        //sampleRate = 0;
        if (!selectedMac.empty()) {
            if (!config.conf["devices"][selectedMac].contains("sampleRates")) {
                config.conf["devices"][selectedMac]["sampleRates"] = json::array();
                config.conf["devices"][selectedMac]["sampleRates"].push_back({{"id", 0}, {"value", 48000},  {"text", "48  kHz"}});
                config.conf["devices"][selectedMac]["sampleRates"].push_back({{"id", 1}, {"value", 96000},  {"text", "96  kHz"}});
                config.conf["devices"][selectedMac]["sampleRates"].push_back({{"id", 2}, {"value", 192000}, {"text", "192 kHz"}});
                config.conf["devices"][selectedMac]["sampleRates"].push_back({{"id", 3}, {"value", 384000}, {"text", "384 kHz"}});
            }
            for (auto const& item : config.conf["devices"][selectedMac]["sampleRates"]) {
                int srId    = item["id"];
                auto srText = item["text"];
                int srValue = item["value"];
                samplerates.define(srValue,  srText, (hpsdr::HpsdrSampleRate)srId);
            }
            
            if (config.conf["devices"][selectedMac].contains("sampleRateId")) {
                _srId = config.conf["devices"][selectedMac]["sampleRateId"];
            } else {
                _srId = 2;
            }
        }
        if (config.conf["devices"][selectedMac].contains("preamp")) {
            _isPreamp = config.conf["devices"][selectedMac]["preamp"];
        }
        if (config.conf["devices"][selectedMac].contains("is_att")) {
            _isAtt = config.conf["devices"][selectedMac]["is_att"];
        }
        if (config.conf["devices"][selectedMac].contains("att_gain")) {
            _attGain = config.conf["devices"][selectedMac]["att_gain"];
        }
        if (config.conf["devices"][selectedMac].contains("is_dither")) {
            _isDither = config.conf["devices"][selectedMac]["is_dither"];
        }
        if (config.conf["devices"][selectedMac].contains("is_randomizer")) {
            _isRandomizer = config.conf["devices"][selectedMac]["is_randomizer"];
        }
        config.release();

        // Update host samplerate
        if (_srId >= 0) {
            auto sampleRate = samplerates.key(_srId);
            core::setInputSampleRate(sampleRate);
        }
    }

    static void menuSelected(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::menuSelected(): {0}", _this->name);

        if (_this->firstSelect) {
            _this->firstSelect = false;

            // Refresh
            _this->refresh();

            // Select device
            config.acquire();
            _this->selectedMac = config.conf["device"];
            config.release();
            _this->selectMac(_this->selectedMac);
        }

        if (_this->_srId >= 0) {
            auto sampleRate = _this->samplerates.key(_this->_srId);
            core::setInputSampleRate(sampleRate);
        }
    }

    static void menuDeselected(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::menuDeselected(): {0}", _this->name);
    }

    static void start(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::start()");

        if (_this->running || _this->selectedMac.empty() || _this->_srId < 0) { return; }
        
        _this->dev = hpsdr::open(_this->devices[_this->devId].addr, &_this->stream);

        auto sampleRate = _this->samplerates.key(_this->_srId);
        _this->dev->setSamplerate((hpsdr::HpsdrSampleRate)_this->_srId, sampleRate);
        _this->dev->setFrequency(_this->freq);
        _this->dev->setPreamp(_this->_isPreamp);
        _this->dev->setAtten(_this->_attGain, _this->_isAtt);

        _this->dev->start();
        _this->running = true;
    }

    static void stop(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::stop()");

        if (!_this->running) { return; }
        _this->running = false;
        
        _this->dev->stop();
    }

    static void tune(double freq, void* ctx) {
        flog::info("HpsdrSourceModule::tune(): {0}", freq);
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        if (_this->running) {
            _this->dev->setFrequency(freq);
        }
        _this->freq = freq;
    }

    static void menuHandler(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }

        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_hpsdr_dev_sel_", _this->name), &_this->devId, _this->devices.txt)) {
            _this->selectMac(_this->devices.key(_this->devId));
            if (!_this->selectedMac.empty()) {
                config.acquire();
                config.conf["device"] = _this->devices.key(_this->devId);
                config.release(true);
            }
        }

        if (SmGui::Combo(CONCAT("##_hpsdr_sr_sel_", _this->name), &_this->_srId, _this->samplerates.txt)) {
            if (!_this->selectedMac.empty()) {
                if (_this->_srId >= 0) {
                    auto sampleRate = _this->samplerates.key(_this->_srId);
                    core::setInputSampleRate(sampleRate);

                    config.acquire();
                    config.conf["devices"][_this->selectedMac]["sampleRateId"] = _this->_srId;
                    config.release(true);
                } else {
                    config.acquire();
                    config.conf["devices"][_this->selectedMac].erase("sampleRateId");                
                    config.release(true);
                }
            }
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_hpsdr_refr_", _this->name))) {
            _this->refresh();
            config.acquire();
            std::string mac = config.conf["device"];
            config.release();
            _this->selectMac(mac);
        }

        if (_this->running) { SmGui::EndDisabled(); }

        if (SmGui::Checkbox(CONCAT("Preamp##_hpsdr_preamp_", _this->name), &_this->_isPreamp)) {
            if (_this->running) {
                _this->dev->setPreamp(_this->_isPreamp);
            }
            if (!_this->selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->selectedMac]["preamp"] = _this->_isPreamp;
                config.release(true);
            }
        }
        if (SmGui::Checkbox(CONCAT("Attenuator##_hpsdr_is_att_", _this->name), &_this->_isAtt)) {
            if (_this->running) {
                _this->dev->setAtten(_this->_attGain, _this->_isAtt);
            }
            if (!_this->selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->selectedMac]["is_att"] = _this->_isAtt;
                config.release(true);
            }
        }
        SmGui::SameLine();
        SmGui::FillWidth();
        if (SmGui::SliderInt(CONCAT("##hpsdr_source_att_gain_", _this->name), &_this->_attGain, 0, 63)) {
            if (_this->running) {
                _this->dev->setAtten(_this->_attGain, _this->_isAtt);
            }
            if (!_this->selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->selectedMac]["att_gain"] = _this->_attGain;
                config.release(true);
            }
        }

#if 0
        if (SmGui::Checkbox(CONCAT("Dither##_hpsdr_dither_", _this->name), &_this->_isDither)) {
            if (_this->running) {
                _this->dev->setDither(_this->_isDither);
            }
            if (!_this->selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->selectedMac]["is_dither"] = _this->_isDither;
                config.release(true);
            }
        }
        if (SmGui::Checkbox(CONCAT("Randomizer##_hpsdr_randomizer_", _this->name), &_this->_isRandomizer)) {
            if (_this->running) {
                _this->dev->setRandomizer(_this->_isRandomizer);
            }
            if (!_this->selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->selectedMac]["is_randomizer"] = _this->_isRandomizer;
                config.release(true);
            }
        }
#endif
    }

    std::string name;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    bool running = false;
    std::string selectedMac = "";

    OptionList<std::string, hpsdr::Info> devices;
    OptionList<int, hpsdr::HpsdrSampleRate> samplerates;

    double freq;
    int devId = 0;
    int _srId = -1;
    
    bool _isPreamp = false;
    bool _isAtt = false;
    int _attGain = 0;
    bool _isDither = false;
    bool _isRandomizer = false;

    bool firstSelect = true;

    std::shared_ptr<hpsdr::Client> dev;

};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/hpsdr_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new HpsdrSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (HpsdrSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
