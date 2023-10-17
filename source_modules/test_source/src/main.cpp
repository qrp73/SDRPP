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
#define NOMINMAX
#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/smgui.h>
#include <gui/widgets/stepped_slider.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/widgets/file_select.h>
#include <filesystem>
#include <regex>
#include <gui/tuner.h>
#include <algorithm>
#include <stdexcept>
#include <utils/optionlist.h>


// Fixed point test vectors

//const int64_t _14bitAES17_0dB[]   { 0x0000,0x0c3f,0x16a0,0x1d8f,0x1fff,0x1d8f,0x16a0,0x0c3e,0x0000,0x33c1,0x2960,0x2271,0x2001,0x2271,0x2960,0x33c1, };
//const int64_t _14bitAES17_m20dB[] { 0x0000,0x0139,0x0243,0x02f5,0x0333,0x02f5,0x0243,0x0139,0x0000,0x3ec7,0x3dbd,0x3d0b,0x3ccd,0x3d0b,0x3dbd,0x3ec7, };
const int64_t _14bitAES17_0dB[]   { 0x3fff,0x0c3e,0x16a0,0x1d8f,0x1fff,0x1d8f,0x16a0,0x0c3e,0x0000,0x33c1,0x295f,0x2270,0x2000,0x2270,0x295f,0x33c1, };
const int64_t _14bitAES17_m20dB[] { 0x3fff,0x0139,0x0243,0x02f4,0x0333,0x02f4,0x0243,0x0139,0x0000,0x3ec6,0x3dbc,0x3d0b,0x3ccc,0x3d0b,0x3dbc,0x3ec6, };
const int64_t _14bitAES17_m40dB[] { 0x3fff,0x001f,0x0039,0x004b,0x0051,0x004b,0x0039,0x001f,0x0000,0x3fe0,0x3fc6,0x3fb4,0x3fae,0x3fb4,0x3fc6,0x3fe0, };
const int64_t _14bitAES17_m60dB[] { 0x3fff,0x0003,0x0005,0x0007,0x0008,0x0007,0x0005,0x0003,0x0000,0x3ffc,0x3ffa,0x3ff8,0x3ff7,0x3ff8,0x3ffa,0x3ffc, };
const int64_t _14bitSomeone_sfdr119_56dB[] { 0,3107,5741,7501,8119,7501,5741,3107,0,-3107,-5741,-7501,-8119,-7501,-5741,-3107, };

const int64_t _14bitSineHamsterNZ4[]         {  422,  3520,  6082,  7718,  8179,  7395,  5485,  2740,  -422, -3520, -6082, -7718, -8179, -7395, -5485, -2740, };
const int64_t _14bitSineHamsterNZ_overflow[] { 1236,  4249,  6615,  7974,  8119,  7028,  4867,  1965, -1236, -4249, -6615, -7974, -8119, -7028, -4867, -1965, };


class TableSource {
private:
    float* _data;
    int  _leng;
    int  _phase;

public:
    float I, Q;
    
    TableSource()
        : _data(NULL), _leng(-1), _phase(0), I(0), Q(0) {
    }
    ~TableSource() {
        free();
    }
    
    void free() {
        if (_data != NULL)
            delete _data;
        _data = NULL;
        _leng = -1;
        _phase = 0;
    }

    void init_dc(float i, float q) {
        free();
        I = i;
        Q = q;
    }
    
    void init(const int bits, const int64_t* data, const int leng) {
        free();
        _data = new float[leng];
        _leng = leng;
        _phase = 0;
        auto shift = sizeof(int64_t) * 8 - bits;
        auto scale = 1.0 / ((1LL << bits)/2 - 1);
        for (auto i=0; i < leng; i++) {
            auto v = data[i];
            v = (v << shift) >> shift;
            _data[i] = v * scale;
        }
    }
    
    void next() {
        if (_leng < 0)
            return;
        I = _data[_phase++];
        Q = 0;
        if (_phase >= _leng)
            _phase = 0;
    }
    
    void setSource(int index) {
        switch (index) {
            case 0: init_dc(0, 0); break;
            case 1: init_dc(+1, 0); break;
            case 2: init_dc(-1, 0); break;
            case 3: init(14, _14bitAES17_0dB, 16); break;
            case 4: init(14, _14bitAES17_m20dB, 16); break;
            case 5: init(14, _14bitAES17_m40dB, 16); break;
            case 6: init(14, _14bitAES17_m60dB, 16); break;
            case 7: init(14, _14bitSomeone_sfdr119_56dB, 16); break;
            case 8: init(14, _14bitSineHamsterNZ4, 16); break;
            case 9: init(14, _14bitSineHamsterNZ_overflow, 16); break;
        }
    }
};



#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "test_source",
    /* Description:     */ "Test source module for DSP testing",
    /* Author:          */ "qrp73",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class TestSourceModule : public ModuleManager::Instance {
public:
    TestSourceModule(std::string name) {//: fileSelect("", { "Wav IQ Files (*.wav)", "*.wav", "All Files", "*" }) {
        flog::warn("TestSource: ctor()");
        this->_name = name;

        if (core::args["server"].b()) { return; }

        if (!config.conf.contains("sampleRates")) {
            auto defRates = json::array();
            defRates.push_back({{"id", 0}, {"value", 44100},   {"text", "44 100"}});
            defRates.push_back({{"id", 1}, {"value", 48000},   {"text", "48 000"}});
            defRates.push_back({{"id", 2}, {"value", 96000},   {"text", "96 000"}});
            defRates.push_back({{"id", 3}, {"value", 192000},  {"text", "192 000"}});
            defRates.push_back({{"id", 4}, {"value", 384000},  {"text", "384 000"}});
            defRates.push_back({{"id", 5}, {"value", 640000},  {"text", "640 000"}});
            defRates.push_back({{"id", 6}, {"value", 768000},  {"text", "768 000"}});
            defRates.push_back({{"id", 7}, {"value", 960000},  {"text", "960 000"}});
            defRates.push_back({{"id", 8}, {"value", 1000000}, {"text", "1 000 000"}});
            defRates.push_back({{"id", 9}, {"value", 1048576}, {"text", "1 048 576"}});
            config.acquire();
            config.conf["sampleRates"] = defRates;
            config.release(true);
            config.disableAutoSave();
            config.save();
            flog::warn("TestSource: ctor()save()");
        }
        _waveTypeId = 0;
        _waveTypes.define(0, "DC0", 0);
        _waveTypes.define(1, "DC+", 1);
        _waveTypes.define(2, "DC-", 2);
        _waveTypes.define(3, "14bit 0 dB", 3);
        _waveTypes.define(4, "14bit -20 dB", 4);
        _waveTypes.define(5, "14bit -40 dB", 5);
        _waveTypes.define(6, "14bit -60 dB", 6);
        _waveTypes.define(7, "14bit SFDR=119.56 dB (Someone)", 7);
        _waveTypes.define(8, "14bit SineHamsterNZ4", 8);
        _waveTypes.define(9, "14bit SineHamsterNZ overflow", 9);
        

        config.acquire();
        for (auto const& item : config.conf["sampleRates"]) {
            int id      = item["id"];
            auto srText = item["text"];
            int srValue = item["value"];
            _sampleRates.define(srValue, srText, id);
        }
        if (config.conf.contains("sampleRateId")) {
            _srId = config.conf["sampleRateId"];
            _sampleRate = _sampleRates.key(_srId);
            core::setInputSampleRate(_sampleRate);
        }
        if (config.conf.contains("waveTypeId")) {
            _waveTypeId = config.conf["waveTypeId"];
            _src.setSource(_waveTypeId);
        }
        config.release();

        _handler.ctx = this;
        _handler.selectHandler = menuSelected;
        _handler.deselectHandler = menuDeselected;
        _handler.menuHandler = menuHandler;
        _handler.startHandler = start;
        _handler.stopHandler = stop;
        _handler.tuneHandler = tune;
        _handler.stream = &_stream;
        sigpath::sourceManager.registerSource("TEST", &_handler);
    }

    ~TestSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("TEST");
        _src.free();
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
    void updateSampleRate(uint32_t sampleRate) {
        _sampleRate = sampleRate;
    }

    bool process(uint32_t samples) {
        if (samples == 0) {
            return false;
        } else if (!_stream.swap(samples)) { 
            return false; 
        };
        return true;
    }

    static void menuSelected(void* ctx) {
        TestSourceModule* _this = (TestSourceModule*)ctx;
        flog::info("TestSource: menuSelected('{0}')", _this->_name);
        core::setInputSampleRate(_this->_sampleRate);
        tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->_centerFreq);
        sigpath::iqFrontEnd.setBuffering(false);
        gui::waterfall.centerFrequencyLocked = true;
    }

    static void menuDeselected(void* ctx) {
        TestSourceModule* _this = (TestSourceModule*)ctx;
        flog::info("TestSource: menuDeselected('{0}')", _this->_name);
        sigpath::iqFrontEnd.setBuffering(true);
        gui::waterfall.centerFrequencyLocked = false;
    }

    static void start(void* ctx) {
        TestSourceModule* _this = (TestSourceModule*)ctx;
        flog::info("TestSource: start('{0}')", _this->_name);
        if (_this->_running) { return; }
        _this->_running = true;
        _this->_workerThread = std::thread(worker, _this);
    }

    static void stop(void* ctx) {
        TestSourceModule* _this = (TestSourceModule*)ctx;
        flog::info("TestSource: stop('{0}')", _this->_name);
        if (!_this->_running) { return; }
        _this->_stream.stopWriter();
        _this->_workerThread.join();
        _this->_stream.clearWriteStop();
        _this->_running = false;
    }

    static void tune(double freq, void* ctx) {
        TestSourceModule* _this = (TestSourceModule*)ctx;
        if (_this->_isLock) {
            //flog::info("FileSource: tune({0}, '{1}')", freq, _this->_name);
            double centerFreq = _this->_centerFreq;
            if (freq != centerFreq) {
                //flog::info("FileSource: tuner::tune({0})", centerFreq);
                tuner::tune(tuner::TUNER_MODE_CENTER, "", centerFreq);
            }
        } else {
            _this->_centerFreq = (int64_t)freq;
        }
    }

    static void menuHandler(void* ctx) {
        TestSourceModule* _this = (TestSourceModule*)ctx;

        //if (_this->_running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_test_sr_sel_", _this->_name), &_this->_srId, _this->_sampleRates.txt)) {
            if (_this->_srId >= 0) {
                _this->_sampleRate = _this->_sampleRates.key(_this->_srId);
                core::setInputSampleRate(_this->_sampleRate);

                config.acquire();
                config.conf["sampleRateId"] = _this->_srId;
                config.release(true);
            } else {
                config.acquire();
                config.conf.erase("sampleRateId");
                config.release(true);
            }
        }
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_test_type_sel_", _this->_name), &_this->_waveTypeId, _this->_waveTypes.txt)) {
            if (_this->_waveTypeId >= 0) {
                config.acquire();
                config.conf["waveTypeId"] = _this->_waveTypeId;
                config.release(true);
                _this->_src.setSource(_this->_waveTypeId);
            } else {
                config.acquire();
                config.conf.erase("waveTypeId");
                config.release(true);
            }
        }

        //if (_this->_running) { SmGui::EndDisabled(); }



        ImGui::Checkbox("Lock frequency##_test_source", &_this->_isLock);
    }
    
    static void worker(void* ctx) {
        TestSourceModule* _this = (TestSourceModule*)ctx;
        int blockSize = std::min((int)(_this->_sampleRate/200), (int)STREAM_BUFFER_SIZE);
        blockSize = std::max(1, blockSize);
        flog::info("TestSource: blockSize={0}", blockSize);

        // Left=I, Right=Q
        while (true) {
            auto pDst = (float*)_this->_stream.writeBuf;
            auto src = &_this->_src;
            for (auto i=0; i < blockSize; i++) {
                *pDst++ = src->I;
                *pDst++ = src->Q;
                src->next();
            }
            if (!_this->process(blockSize)) {
                break;
            }
        }
        flog::info("TestSource: stop");
    }
    
    std::string _name;
    dsp::stream<dsp::complex_t> _stream;
    SourceManager::SourceHandler _handler;
    std::thread _workerThread;
    bool _running = false;
    bool _enabled = true;
    bool _isLock = true;
    
    OptionList<int, int> _waveTypes;
    int _waveTypeId;
    OptionList<int, int> _sampleRates;
    int _srId;

    uint32_t _sampleRate = 1048576;
    int64_t _centerFreq = 0;
    
    TableSource _src;
};

MOD_EXPORT void _INIT_() {
    flog::warn("TestSource: _INIT_()");
    json def = json({});
    config.setPath(core::args["root"].s() + "/test_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new TestSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (TestSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
