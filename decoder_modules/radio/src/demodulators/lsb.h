#pragma once
#include "../demod.h"
#include <dsp/demod/ssb.h>

namespace demod {
    class LSB : public Demodulator {
    public:
        LSB() {}

        LSB(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~LSB() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->_name = name;
            _config = config;

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
            config->release();

            // Define structure
            _demod.init(input, dsp::demod::SSB<dsp::stereo_t>::Mode::LSB, bandwidth, getIFSampleRate(), _agcEnabled, _agcAttack / getIFSampleRate(), _agcDecay / getIFSampleRate());
            _demod.setAGCGain(pow(10, _agcGain/20));
        }

        void start() { _demod.start(); }

        void stop() { _demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Checkbox(("##_radio_lsb_agc_enable_" + _name).c_str(), &_agcEnabled)) {
                _demod.setAGCEnabled(_agcEnabled);
                _config->acquire();
                _config->conf[_name][getName()]["agcEnabled"] = _agcEnabled;
                _config->release(true);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("AGC");
            if (_agcEnabled) { 
                ImGui::BeginDisabled();
                _agcGain = 20*log10(_demod.getAGCGain());
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_lsb_gain_" + _name).c_str(), &_agcGain, -10.0f, 90.0f, "%.0f dB")) {
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
            if (ImGui::SliderFloat(("##_radio_lsb_agc_attack_" + _name).c_str(), &_agcAttack, 1.0f, 200.0f)) {
                _demod.setAGCAttack(_agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[_name][getName()]["agcAttack"] = _agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_lsb_agc_decay_" + _name).c_str(), &_agcDecay, 1.0f, 20.0f)) {
                _demod.setAGCDecay(_agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[_name][getName()]["agcDecay"] = _agcDecay;
                _config->release(true);
            }
            if (!_agcEnabled) ImGui::EndDisabled();
        }

        void setBandwidth(double bandwidth) { _demod.setBandwidth(bandwidth); }

        void setInput(dsp::stream<dsp::complex_t>* input) { _demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "LSB"; }
        double getIFSampleRate() { return 48000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 2700.0; }
        double getMinBandwidth() { return 500.0; }
        double getMaxBandwidth() { return getIFSampleRate() / 2.0; }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 100.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_UPPER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return true; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &_demod.out; }

    private:
        dsp::demod::SSB<dsp::stereo_t> _demod;

        ConfigManager* _config;

        bool  _agcEnabled = true;
        float _agcGain    = 1.0f;
        float _agcAttack  = 50.0f;
        float _agcDecay   = 5.0f;

        std::string _name;
    };
}
