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
        this->_name = name;

        // Define samplerates
        _sampleRates.clear();
        _srId = -1;

        _handler.ctx = this;
        _handler.selectHandler = menuSelected;
        _handler.deselectHandler = menuDeselected;
        _handler.menuHandler = menuHandler;
        _handler.startHandler = start;
        _handler.stopHandler = stop;
        _handler.tuneHandler = tune;
        _handler.stream = &_stream;

        sigpath::sourceManager.registerSource("HPSDR", &_handler);
    }

    ~HpsdrSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("HPSDR");
    }

    void postInit() {}

    void enable() {
        _enabled = true;
    }

    void disable() {
        _enabled = false;
    }

    bool isEnabled() {
        return _enabled;
    }

private:
    void refresh() {
        char mac[128];
        char buf[128];
        _devices.clear();
        auto devList = hpsdr::discover();
        for (auto& d : devList) {
            sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", 
                d.mac[0], d.mac[1], d.mac[2], 
                d.mac[3], d.mac[4], d.mac[5]);
            sprintf(buf, "%s / %s v%d.%d", 
                mac,
                d.getBoardName(),
                d.verMajor, d.verMinor);
            _devices.define(mac, buf, d);
        }
    }

    void selectMac(std::string mac) {
        // If the device list is empty, don't select anything
        if (!_devices.size()) {
            _selectedMac.clear();
            _sampleRates.clear();
            return;
        }

        // If the mac doesn't exist, select the first available one instead
        if (!_devices.keyExists(mac)) {
            selectMac(_devices.key(0));
            return;
        }

        // Default config
        _isAtt = false;
        _attGain = 0;

        // Load config
        _devId = _devices.keyId(mac);
        _selectedMac = mac;

        config.acquire();

        _sampleRates.clear();
        _srId = -1;

        auto cfgMac = config.conf["devices"][_selectedMac];

        if (!_selectedMac.empty()) {
            if (!cfgMac.contains("sampleRates")) {
                auto defRates = json::array();
                defRates.push_back({{"id", 0}, {"value", 48000},  {"text", "48  kHz"}});
                defRates.push_back({{"id", 1}, {"value", 96000},  {"text", "96  kHz"}});
                defRates.push_back({{"id", 2}, {"value", 192000}, {"text", "192 kHz"}});
                defRates.push_back({{"id", 3}, {"value", 384000}, {"text", "384 kHz"}});
                cfgMac["sampleRates"] = defRates;
            }
            for (auto const& item : cfgMac["sampleRates"]) {
                int srId    = item["id"];
                auto srText = item["text"];
                int srValue = item["value"];
                _sampleRates.define(srValue,  srText, (hpsdr::HpsdrSampleRate)srId);
            }
            
            if (cfgMac.contains("sampleRateId"))
                _srId = cfgMac["sampleRateId"];
            else
                _srId = 2;
        }
        if (cfgMac.contains("preamp")) {
            _isPreamp = cfgMac["preamp"];
        }
        if (cfgMac.contains("is_att")) {
            _isAtt = cfgMac["is_att"];
        }
        if (cfgMac.contains("att_gain")) {
            _attGain = cfgMac["att_gain"];
        }
        if (cfgMac.contains("is_dither")) {
            _isDither = cfgMac["is_dither"];
        }
        if (cfgMac.contains("is_randomizer")) {
            _isRandomizer = cfgMac["is_randomizer"];
        }
        config.release();

        // Update host samplerate
        if (_srId >= 0) {
            auto sampleRate = _sampleRates.key(_srId);
            core::setInputSampleRate(sampleRate);
        }
    }

    static void menuSelected(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::menuSelected(): {0}", _this->_name);

        if (_this->_firstSelect) {
            _this->_firstSelect = false;

            // Refresh
            _this->refresh();

            // Select device
            config.acquire();
            _this->_selectedMac = config.conf["device"];
            config.release();
            _this->selectMac(_this->_selectedMac);
        }

        if (_this->_srId >= 0) {
            auto sampleRate = _this->_sampleRates.key(_this->_srId);
            core::setInputSampleRate(sampleRate);
        }
    }

    static void menuDeselected(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::menuDeselected(): {0}", _this->_name);
    }

    static void start(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::start()");

        if (_this->_running || _this->_selectedMac.empty() || _this->_srId < 0) { return; }
        
        _this->dev = hpsdr::open(_this->_devices[_this->_devId].addr, &_this->_stream);

        auto sampleRate = _this->_sampleRates.key(_this->_srId);
        _this->dev->setSamplerate((hpsdr::HpsdrSampleRate)_this->_srId, sampleRate);
        _this->dev->setFrequency(_this->_freq);
        _this->dev->setPreamp(_this->_isPreamp);
        _this->dev->setAtten(_this->_attGain, _this->_isAtt);

        _this->dev->start();
        _this->_running = true;
    }

    static void stop(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        flog::info("HpsdrSourceModule::stop()");

        if (!_this->_running) { return; }
        _this->_running = false;
        
        _this->dev->stop();
    }

    static void tune(double freq, void* ctx) {
        flog::info("HpsdrSourceModule::tune(): {0}", freq);
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;
        if (_this->_running) {
            _this->dev->setFrequency(freq);
        }
        _this->_freq = freq;
    }

    static void menuHandler(void* ctx) {
        HpsdrSourceModule* _this = (HpsdrSourceModule*)ctx;

        if (_this->_running) { SmGui::BeginDisabled(); }

        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_hpsdr_dev_sel_", _this->_name), &_this->_devId, _this->_devices.txt)) {
            _this->selectMac(_this->_devices.key(_this->_devId));
            if (!_this->_selectedMac.empty()) {
                config.acquire();
                config.conf["device"] = _this->_devices.key(_this->_devId);
                config.release(true);
            }
        }

        if (_this->_selectedMac.empty()) { SmGui::BeginDisabled(); }
        if (SmGui::Combo(CONCAT("##_hpsdr_sr_sel_", _this->_name), &_this->_srId, _this->_sampleRates.txt)) {
            if (!_this->_selectedMac.empty()) {
                if (_this->_srId >= 0) {
                    auto sampleRate = _this->_sampleRates.key(_this->_srId);
                    core::setInputSampleRate(sampleRate);

                    config.acquire();
                    config.conf["devices"][_this->_selectedMac]["sampleRateId"] = _this->_srId;
                    config.release(true);
                } else {
                    config.acquire();
                    config.conf["devices"][_this->_selectedMac].erase("sampleRateId");                
                    config.release(true);
                }
            }
        }
        if (_this->_selectedMac.empty()) { SmGui::EndDisabled(); }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_hpsdr_refr_", _this->_name))) {
            _this->refresh();
            config.acquire();
            std::string mac = config.conf["device"];
            config.release();
            _this->selectMac(mac);
        }

        if (_this->_running) { SmGui::EndDisabled(); }

        if (_this->_selectedMac.empty()) { SmGui::BeginDisabled(); }

        if (SmGui::Checkbox(CONCAT("Preamp##_hpsdr_preamp_", _this->_name), &_this->_isPreamp)) {
            if (_this->_running) {
                _this->dev->setPreamp(_this->_isPreamp);
            }
            if (!_this->_selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->_selectedMac]["preamp"] = _this->_isPreamp;
                config.release(true);
            }
        }
        if (SmGui::Checkbox(CONCAT("Attenuator##_hpsdr_is_att_", _this->_name), &_this->_isAtt)) {
            if (_this->_running) {
                _this->dev->setAtten(_this->_attGain, _this->_isAtt);
            }
            if (!_this->_selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->_selectedMac]["is_att"] = _this->_isAtt;
                config.release(true);
            }
        }
        SmGui::SameLine();
        SmGui::FillWidth();
        if (SmGui::SliderInt(CONCAT("##hpsdr_source_att_gain_", _this->_name), &_this->_attGain, 0, 63)) {
            if (_this->_running) {
                _this->dev->setAtten(_this->_attGain, _this->_isAtt);
            }
            if (!_this->_selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->_selectedMac]["att_gain"] = _this->_attGain;
                config.release(true);
            }
        }

#if 0
        if (SmGui::Checkbox(CONCAT("Dither##_hpsdr_dither_", _this->_name), &_this->_isDither)) {
            if (_this->_running) {
                _this->dev->setDither(_this->_isDither);
            }
            if (!_this->_selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->_selectedMac]["is_dither"] = _this->_isDither;
                config.release(true);
            }
        }
        if (SmGui::Checkbox(CONCAT("Randomizer##_hpsdr_randomizer_", _this->_name), &_this->_isRandomizer)) {
            if (_this->_running) {
                _this->dev->setRandomizer(_this->_isRandomizer);
            }
            if (!_this->_selectedMac.empty()) {
                config.acquire();
                config.conf["devices"][_this->_selectedMac]["is_randomizer"] = _this->_isRandomizer;
                config.release(true);
            }
        }
#endif
        if (_this->_selectedMac.empty()) { SmGui::EndDisabled(); }
    }

    std::string _name;
    bool _enabled = true;
    bool _running = false;
    std::string _selectedMac = "";

    dsp::stream<dsp::complex_t>  _stream;
    SourceManager::SourceHandler _handler;

    OptionList<std::string, hpsdr::Info> _devices;
    OptionList<int, hpsdr::HpsdrSampleRate> _sampleRates;

    double _freq;
    int    _devId = 0;
    int    _srId = -1;
    
    bool   _isPreamp = false;
    bool   _isAtt = false;
    int    _attGain = 0;
    bool   _isDither = false;
    bool   _isRandomizer = false;

    bool _firstSelect = true;

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
