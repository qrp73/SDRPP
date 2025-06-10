#pragma once
#include "../demod.h"
#include <dsp/demod/cw.h>

namespace demod {
    class CW : public Demodulator {
    public:
        CW() {}

        CW(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~CW() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->_name = name;
            this->_config = config;
            //this->_afbwChangeHandler = afbwChangeHandler;

            // Load config
            config->acquire();
            if (config->conf[name][getName()].contains("agcEnabled")) {
                _agcEnabled = config->conf[name][getName()]["agcEnabled"];
            }
            if (config->conf[name][getName()].contains("agcGain")) {
                _agcGain = config->conf[name][getName()]["agcGain"];
            }
            if (config->conf[name][getName()].contains("agcAttack")) {
                _agcAttack = config->conf[name][getName()]["agcAttack"];
            }
            if (config->conf[name][getName()].contains("agcDecay")) {
                _agcDecay = config->conf[name][getName()]["agcDecay"];
            }
            if (config->conf[name][getName()].contains("tone")) {
                _tone = config->conf[name][getName()]["tone"];
            }
            config->release();

            // Define structure
            _demod.init(input, _tone, _agcEnabled, _agcAttack / getIFSampleRate(), _agcDecay / getIFSampleRate(), getIFSampleRate());
            _demod.setAGCGain(pow(10, _agcGain/20));
        }

        void start() { _demod.start(); }

        void stop() { _demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Checkbox(("##_radio_cw_agc_enable_" + _name).c_str(), &_agcEnabled)) {
                _demod.setAGCEnabled(_agcEnabled);
                _config->acquire();
                _config->conf[_name][getName()]["agcEnabled"] = _agcEnabled;
                _config->release(true);
                if (!_agcEnabled) {
                    _agcGain = 20*log10(_demod.getAGCGain());                
                    _config->acquire();
                    _config->conf[_name][getName()]["agcGain"] = _agcGain;
                    _config->release(true);
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("AGC");
            if (_agcEnabled) { 
                ImGui::BeginDisabled();
                _agcGain = 20*log10(_demod.getAGCGain());
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_cw_gain_" + _name).c_str(), &_agcGain, -10.0f, 90.0f, "%.0f dB")) {
                if (!_agcEnabled) {
                    _demod.setAGCGain(pow(10, _agcGain/20));
                    _config->acquire();
                    _config->conf[_name][getName()]["agcGain"] = _agcGain;
                    _config->release(true);
                }
            }
            if (_agcEnabled) ImGui::EndDisabled();
            
            if (!_agcEnabled) ImGui::BeginDisabled();
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_cw_agc_attack_" + _name).c_str(), &_agcAttack, 1.0f, 200.0f)) {
                _demod.setAGCAttack(_agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[_name][getName()]["agcAttack"] = _agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_cw_agc_decay_" + _name).c_str(), &_agcDecay, 1.0f, 20.0f)) {
                _demod.setAGCDecay(_agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[_name][getName()]["agcDecay"] = _agcDecay;
                _config->release(true);
            }
            if (!_agcEnabled) ImGui::EndDisabled();
            ImGui::LeftLabel("Tone Frequency");
            ImGui::FillWidth();
            if (ImGui::InputInt(("Stereo##_radio_cw_tone_" + _name).c_str(), &_tone, 10, 100)) {
                _tone = std::clamp<int>(_tone, 250, 1250);
                _demod.setTone(_tone);
                _config->acquire();
                _config->conf[_name][getName()]["tone"] = _tone;
                _config->release(true);
            }
        }

        void setBandwidth(double bandwidth) {}

        void setInput(dsp::stream<dsp::complex_t>* input) { _demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "CW"; }
        double getIFSampleRate() { return 3000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 500.0; }
        double getMinBandwidth() { return 10.0; }
        double getMaxBandwidth() { return getIFSampleRate() / 2.0; }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 10.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &_demod.out; }

    private:
        ConfigManager* _config = NULL;
        dsp::demod::CW<dsp::stereo_t> _demod;

        std::string _name;

        bool  _agcEnabled = true;
        float _agcGain = 1.0;
        float _agcAttack = 100.0f;
        float _agcDecay = 5.0f;
        int   _tone = 700;

        EventHandler<float> _afbwChangeHandler;
    };
}
