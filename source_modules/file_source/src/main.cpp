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
#include <wavreader.h>
#include <core.h>
#include <gui/widgets/file_select.h>
#include <filesystem>
#include <regex>
#include <gui/tuner.h>
#include <algorithm>
#include <stdexcept>


#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "file_source",
    /* Description:     */ "WAV file source module",
    /* Author:          */ "qrp73",
    /* Version:         */ 0, 1, 2,
    /* Max instances    */ 1
};

ConfigManager config;

class FileSourceModule : public ModuleManager::Instance {
public:
    FileSourceModule(std::string name) : fileSelect("", { "Wav IQ Files (*.wav)", "*.wav", "All Files", "*" }) {
        this->name = name;

        if (core::args["server"].b()) { return; }

        config.acquire();
        fileSelect.setPath(config.conf["path"], true);
        config.release();

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;
        sigpath::sourceManager.registerSource("File", &handler);
        
        updateFmtText();
    }

    ~FileSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("File");
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
        _invSampleRate = 1.0 / sampleRate;
        _posPlaySec = _posPlay * _invSampleRate;
        _posLastSec = _posLast * _invSampleRate;
    }
    void reset() {
        _posPlay = 0;
        _posPlaySec = 0;
        _posLast = 0;
        _posLastSec = 0;
        if (_reader == NULL) {
            return;
        }
        _reader->reset();
        _posLast = _reader->getSampleCount();
        _posLastSec = _posLast * _invSampleRate;
    }
    void updatePos() {
        if (_reader == NULL) {
            _posPlay = 0;
            _posPlaySec = 0;
        } else {
            _posPlay = _reader->getSamplePosition();
            _posPlaySec = _posPlay * _invSampleRate;
        }            
    }
    bool process(uint32_t samples) {
        updatePos();
        if (samples == 0) {
            //flog::info("FileSource: endOfFile"); 
            if (!_isLoop)
                return false;
            reset();
            return true;
        } else if (!stream.swap(samples)) { 
            return false; 
        };
        return true;
    }

    void updateFmtText() {
        if (_reader == NULL || !_reader->isValid()) {
            _fmtText = "FMT: -, - bit, - kHz";
            return;
        }
        char sbuf[128];
        sprintf(sbuf, "FMT: %u/%s, %u bit, %.f kHz", 
            _reader->getFormat(),
            _reader->getFormatName(), 
            _reader->getBitDepth(), 
            _reader->getSampleRate()/1000.0f);
        _fmtText = std::string(sbuf);
    }

    static void menuSelected(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        flog::info("FileSource: menuSelected('{0}')", _this->name);
        core::setInputSampleRate(_this->_sampleRate);
        tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->_centerFreq);
        sigpath::iqFrontEnd.setBuffering(false);
        gui::waterfall.centerFrequencyLocked = true;
    }

    static void menuDeselected(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        flog::info("FileSource: menuDeselected('{0}')", _this->name);
        sigpath::iqFrontEnd.setBuffering(true);
        gui::waterfall.centerFrequencyLocked = false;
    }

    static void start(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        flog::info("FileSource: start('{0}')", _this->name);
        if (_this->_running) { return; }
        if (_this->_reader == NULL) { return; }
        _this->_running = true;
        _this->_workerThread = std::thread(worker, _this);
    }

    static void stop(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        flog::info("FileSource: stop('{0}')", _this->name);
        if (!_this->_running) { return; }
        if (_this->_reader == NULL) { return; }
        _this->stream.stopWriter();
        _this->_workerThread.join();
        _this->stream.clearWriteStop();
        _this->_running = false;
    }

    static void tune(double freq, void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        if (_this->_isLock) {
            //flog::info("FileSource: tune({0}, '{1}')", freq, _this->name);
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
        FileSourceModule* _this = (FileSourceModule*)ctx;

        if (_this->_running) { SmGui::BeginDisabled(); }
        
        if (_this->fileSelect.render("##file_source_" + _this->name)) {
            if (_this->_reader != NULL) {
                _this->_reader->close();
                delete _this->_reader;
                _this->_reader = NULL;
            }
            _this->updateFmtText();
            _this->reset();
            if (_this->fileSelect.pathIsValid()) {
                try {
                    _this->_reader = new WavReader(_this->fileSelect.path);
                    auto reader = _this->_reader;
                    flog::info("FileSource: format={0}/{1}, bitDepth={2}, sampleRate={3}, sampleCount={4}, channels={5}", 
                        (int)reader->getFormat(), 
                        reader->getFormatName(),
                        reader->getBitDepth(), 
                        reader->getSampleRate(),
                        reader->getSampleCount(),
                        reader->getChannelCount());
                    if (reader->getSampleRate() == 0) {
                        reader->close();
                        delete reader;
                        reader = NULL;
                        _this->_reader = reader;
                        throw std::runtime_error("Sample rate should not be zero");
                    }
                    _this->updateFmtText();
                    _this->updateSampleRate(reader->getSampleRate());
                    _this->reset();
                    flog::info("FileSource: core::setInputSampleRate({0})", _this->_sampleRate);
                    core::setInputSampleRate(_this->_sampleRate);
                    std::string filename = std::filesystem::path(_this->fileSelect.path).filename().string();
                    _this->_centerFreq = _this->getFrequency(filename);
                    flog::info("FileSource: tuner::tune({0})", _this->_centerFreq);
                    tuner::tune(tuner::TUNER_MODE_CENTER, "", _this->_centerFreq);
                }
                catch (std::exception& e) {
                    flog::error("Error: {0}", e.what());
                }
            }
            config.acquire();
            config.conf["path"] = _this->fileSelect.path;
            config.release(true);
        }

        if (_this->_running) { SmGui::EndDisabled(); }

        SmGui::BeginDisabled();
        SmGui::FillWidth();
        SmGui::LeftLabel(_this->_fmtText.c_str());
        SmGui::EndDisabled();
        
        ImGui::NewLine();
        SmGui::FillWidth();
        if (SmGui::SliderFloat(CONCAT("Play##_file_source_pos_", _this->name), &_this->_posPlaySec, 0, _this->_posLastSec)) {
            if (_this->_reader != NULL) {
                _this->_reader->seek(_this->_posPlaySec * _this->_reader->getSampleRate());
            }
        }
        
        ImGui::Checkbox("Loop##_file_source", &_this->_isLoop);
        ImGui::Checkbox("Lock frequency##_file_source", &_this->_isLock);
    }
    
    static void worker(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        int blockSize = std::min((int)(_this->_reader->getSampleRate()/200), (int)STREAM_BUFFER_SIZE);
        blockSize = std::max(1, blockSize);
        
        flog::info("FileSource: blockSize={0}", blockSize);
        auto reader = _this->_reader;
        if (reader->getChannelCount() != 2) {
            flog::error("FileSource: not supported channel count: {0}", reader->getChannelCount());
            return;
        }
        if (reader->getSampleCount() < 1) {
            flog::error("FileSource: no samples: {0}", reader->getSampleCount());
            return;
        }
        
        auto fmtCode = reader->getFormat();
        auto fmtBits = reader->getBitDepth();
        
        // Left=I, Right=Q
        if (fmtCode == WAVE_FORMAT::IEEE_FLOAT && fmtBits == 32) {
            // WAV uses f32 format for 32 bit IEEE_FLOAT
            while (true) {
                const size_t read = reader->readSamples(_this->stream.writeBuf, blockSize * sizeof(dsp::complex_t));
                const size_t samples = read / sizeof(dsp::complex_t);
                if (!_this->process(samples)) {
                    break;
                }
            }
        } else if (fmtCode == WAVE_FORMAT::IEEE_FLOAT && fmtBits == 64) {
            // WAV uses f64 format for 64 bit IEEE_FLOAT
            double inBuf[blockSize * 2];
            while (true) {
                const size_t read = reader->readSamples(inBuf, sizeof(inBuf));
                const size_t samples = read / 16;
                volk_64f_convert_32f((float*)_this->stream.writeBuf, inBuf, samples * 2);
                if (!_this->process(samples)) {
                    break;
                }
            }
        } else if (fmtCode == WAVE_FORMAT::PCM && fmtBits == 8) {
            // WAV uses u8 format for 8 bit PCM
            const int32_t vzero = 0x80;
            const float   scale = 0x7f;
            const float   invScale = 1.0 / scale;
            uint8_t inBuf[blockSize * 2];
            while (true) {
                const size_t read = reader->readSamples(inBuf, sizeof(inBuf));
                const size_t samples = read / 2;
                // TODO: there is no volk function for u8 input format
                // volk_8u_s32f_convert_32f((float*)_this->stream.writeBuf, inBuf, (float)0x7f, samples * 2);
                auto pDst = (float*)_this->stream.writeBuf;
                auto pSrc = (uint8_t*)inBuf;
                for (auto i=0; i < read; i++) {
                    *pDst++ = (*pSrc++ - vzero) * invScale;
                }
                if (!_this->process(samples)) {
                    break;
                }
            }
        } else if (fmtCode == WAVE_FORMAT::PCM && fmtBits == 16) {
            // WAV uses i16 format for 16 bit PCM
            const float scale = 0x7fff;
            int16_t inBuf[blockSize * 2];
            while (true) {
                const size_t read = reader->readSamples(inBuf, sizeof(inBuf));
                const size_t samples = read / 4;
                volk_16i_s32f_convert_32f((float*)_this->stream.writeBuf, inBuf, scale, samples * 2);
                if (!_this->process(samples)) {
                    break;
                }
            }
        } else if (fmtCode == WAVE_FORMAT::PCM && fmtBits == 24) {
            // WAV uses i24 format for 24 bit PCM
            const float scale = 0x7fffff;
            const float invScale = 1.0 / scale;
            uint8_t inBuf[blockSize * 2 * 3];
            while (true) {
                const size_t read = _this->_reader->readSamples(inBuf, sizeof(inBuf));
                const size_t samples = read / 6;
                // TODO: there is no volk function for i24 input format
                auto pDst = (float*)_this->stream.writeBuf;
                auto pSrc = (uint8_t*)inBuf;
                for (auto i=0; i < samples * 2; i++) {
                    int32_t v = *pSrc++ | (*pSrc++ << 8) | (*pSrc++ << 16); // read i24
                    v = (v << 8) >> 8;                                      // sign extend
                    *pDst++ = v * invScale;                                 // scale
                }
                if (!_this->process(samples)) {
                    break;
                }
            }
        } else if (fmtCode == WAVE_FORMAT::PCM && fmtBits == 32) {
            // WAV uses i32 format for 32 bit PCM
            const float scale = 0x7fffffff;
            int32_t inBuf[blockSize * 2];
            while (true) {
                const size_t read = _this->_reader->readSamples(inBuf, sizeof(inBuf));
                const size_t samples = read / 8;
                volk_32i_s32f_convert_32f((float*)_this->stream.writeBuf, inBuf, scale, samples * 2);
                if (!_this->process(samples)) {
                    break;
                }
            }
        } else {
            flog::error("FileSource: not supported sample format: {0}/{1}, {2} bit", 
                (int)fmtCode, 
                _this->_reader->getFormatName(),
                fmtBits);
        }
        flog::info("FileSource: stop");
    }
    
    static int64_t getFrequency(std::string filename) {
        std::regex expr("[0-9]+(Hz|kHz|MHz|GHz)");
        std::smatch matches;
        if (!std::regex_search(filename, matches, expr)) {
            return 0;
        }
        if (matches.empty() || matches.size() < 2) {
            return 0;
        }
        //flog::warn("FileSource: \"{0}\", \"{1}\", \"{2}\"", matches[0], matches[1]); 
        std::string freqStr = matches[0].str();
        std::string unitStr = matches[1].str();
        freqStr = freqStr.substr(0, freqStr.size() - unitStr.size());
        if (unitStr == "Hz") {
            char* end;
            return std::strtol(freqStr.c_str(), &end, 10);
        } else if (unitStr == "kHz") {
            char* end;
            int64_t value = std::strtol(freqStr.c_str(), &end, 10);
            return value * 1000L;
        } else if (unitStr == "MHz") {
            char* end;
            int64_t value = std::strtol(freqStr.c_str(), &end, 10);
            return value * 1000000L;
        } else if (unitStr == "GHz") {
            char* end;
            int64_t value = std::strtol(freqStr.c_str(), &end, 10);
            return value * 1000000000L;
        } else {
            return 0;
        }    
    }

    FileSelect fileSelect;
    std::string name;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    WavReader* _reader = NULL;
    std::thread _workerThread;
    bool _running = false;
    bool _enabled = true;
    bool _isLoop = true;
    bool _isLock = true;
    uint64_t _posPlay = 0;
    float _posPlaySec = 0;
    uint64_t _posLast = 0;
    float _posLastSec = 0;
    uint32_t _sampleRate = 1000000;
    float _invSampleRate = 1.0 / 1000000;
    int64_t _centerFreq = 0;
    std::string _fmtText;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["path"] = "";
    config.setPath(core::args["root"].s() + "/file_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new FileSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (FileSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
