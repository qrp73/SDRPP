#pragma once
#include "../demod.h"
#include <dsp/demod/am.h>

namespace demod {
    class AM : public Demodulator {
    public:
        AM() {}

        AM(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~AM() { stop(); }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->_name = name;
            _config = config;

            // Load config
            config->acquire();
            if (config->conf[name][getName()].contains("agcMode")) {
                _agcMode = config->conf[name][getName()]["agcMode"];
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
            auto agcMode = _agcMode==0 ? dsp::demod::AM<dsp::stereo_t>::AGCMode::OFF : _agcMode==1 ? dsp::demod::AM<dsp::stereo_t>::AGCMode::CARRIER : dsp::demod::AM<dsp::stereo_t>::AGCMode::AUDIO;
            _demod.init(input, agcMode, bandwidth, _agcAttack / getIFSampleRate(), _agcDecay / getIFSampleRate(), 100.0 / getIFSampleRate(), getIFSampleRate());
            _demod.setAGCGain(pow(10, _agcGain/20));
        }

        void start() { _demod.start(); }

        void stop() { _demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            
            static const char* agcModesTxt[] = { "Off", "Carrier", "Audio" };
            if (_agcMode < 0 || _agcMode >= IM_ARRAYSIZE(agcModesTxt)) _agcMode = 0;
            ImGui::LeftLabel("AGC Mode");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Combo(("##_radio_am_agc_mode_" + _name).c_str(), &_agcMode, agcModesTxt, IM_ARRAYSIZE(agcModesTxt))) {
                if (_agcMode < 0 || _agcMode >= IM_ARRAYSIZE(agcModesTxt)) _agcMode = 0;
                _demod.setAGCMode((dsp::demod::AM<dsp::stereo_t>::AGCMode)_agcMode);
                _agcGain = 20*log10(_demod.getAGCGain());
                _config->acquire();
                _config->conf[_name][getName()]["agcMode"] = _agcMode;
                _config->release(true);
                if (_agcMode == dsp::demod::AM<dsp::stereo_t>::AGCMode::OFF) {
                    _config->acquire();
                    _config->conf[_name][getName()]["agcGain"] = _agcGain;
                    _config->release(true);
                }
            }
            bool agcEnabled = _agcMode != dsp::demod::AM<dsp::stereo_t>::AGCMode::OFF;
            if (agcEnabled) { 
                ImGui::BeginDisabled();
                _agcGain = 20*log10(_demod.getAGCGain());
            }
            ImGui::LeftLabel("Gain");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_gain_" + _name).c_str(), &_agcGain, -10.0f, 90.0f, "%.0f dB")) {
                if (!agcEnabled) {
                    _demod.setAGCGain(pow(10, _agcGain/20));
                    _config->acquire();
                    _config->conf[_name][getName()]["agcGain"] = _agcGain;
                    _config->release(true);
                }
            }
            if (agcEnabled) ImGui::EndDisabled();

            if (!agcEnabled) ImGui::BeginDisabled();
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_agc_attack_" + _name).c_str(), &_agcAttack, 1.0f, 200.0f)) {
                _demod.setAGCAttack(_agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[_name][getName()]["agcAttack"] = _agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_agc_decay_" + _name).c_str(), &_agcDecay, 1.0f, 20.0f)) {
                _demod.setAGCDecay(_agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[_name][getName()]["agcDecay"] = _agcDecay;
                _config->release(true);
            }
            if (!agcEnabled) ImGui::EndDisabled();
        }

        void setBandwidth(double bandwidth) { _demod.setBandwidth(bandwidth); }

        void setInput(dsp::stream<dsp::complex_t>* input) { _demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "AM"; }
        double getIFSampleRate() { return 24000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 12000.0; }
        double getMinBandwidth() { return 1000.0; }
        double getMaxBandwidth() { return getIFSampleRate(); }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 1000.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &_demod.out; }

    private:
        dsp::demod::AM<dsp::stereo_t> _demod;

        ConfigManager* _config = NULL;

        int   _agcMode  = dsp::demod::AM<dsp::stereo_t>::AGCMode::CARRIER;
        float _agcGain = 1.0;
        float _agcAttack = 50.0f;
        float _agcDecay = 5.0f;

        std::string _name;
    };
}
