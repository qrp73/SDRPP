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
            this->name = name;
            _config = config;

            // Load config
            config->acquire();
            if (config->conf[name][getName()].contains("agcMode")) {
                _agcMode = config->conf[name][getName()]["agcMode"];
            }
            if (config->conf[name][getName()].contains("agcAttack")) {
                _agcAttack = config->conf[name][getName()]["agcAttack"];
            }
            if (config->conf[name][getName()].contains("agcDecay")) {
                _agcDecay = config->conf[name][getName()]["agcDecay"];
            }
            config->release();

            // Define structure
            demod.init(input, _agcMode==0 ? dsp::demod::AM<dsp::stereo_t>::AGCMode::OFF : _agcMode==1 ? dsp::demod::AM<dsp::stereo_t>::AGCMode::CARRIER : dsp::demod::AM<dsp::stereo_t>::AGCMode::AUDIO, bandwidth, _agcAttack / getIFSampleRate(), _agcDecay / getIFSampleRate(), 100.0 / getIFSampleRate(), getIFSampleRate());
        }

        void start() { demod.start(); }

        void stop() { demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            
            static const char* agcModesTxt[] = { "Off", "Carrier", "Audio" };
            if (_agcMode < 0 || _agcMode >= IM_ARRAYSIZE(agcModesTxt)) _agcMode = 0;
            ImGui::LeftLabel("AGC Mode");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Combo(("##_radio_am_agc_mode_" + name).c_str(), &_agcMode, agcModesTxt, IM_ARRAYSIZE(agcModesTxt))) {
                switch (_agcMode) {
                    case 0: demod.setAGCMode(dsp::demod::AM<dsp::stereo_t>::AGCMode::OFF); break;
                    case 1: demod.setAGCMode(dsp::demod::AM<dsp::stereo_t>::AGCMode::CARRIER); break; 
                    case 2: demod.setAGCMode(dsp::demod::AM<dsp::stereo_t>::AGCMode::AUDIO); break;
                }
                _config->acquire();
                _config->conf[name][getName()]["agcMode"] = _agcMode;
                _config->release(true);
            }
            if (_agcMode == 0) ImGui::BeginDisabled();
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_agc_attack_" + name).c_str(), &_agcAttack, 1.0f, 200.0f)) {
                demod.setAGCAttack(_agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcAttack"] = _agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_agc_decay_" + name).c_str(), &_agcDecay, 1.0f, 20.0f)) {
                demod.setAGCDecay(_agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcDecay"] = _agcDecay;
                _config->release(true);
            }
            if (_agcMode == 0) ImGui::EndDisabled();
        }

        void setBandwidth(double bandwidth) { demod.setBandwidth(bandwidth); }

        void setInput(dsp::stream<dsp::complex_t>* input) { demod.setInput(input); }

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
        dsp::stream<dsp::stereo_t>* getOutput() { return &demod.out; }

    private:
        dsp::demod::AM<dsp::stereo_t> demod;

        ConfigManager* _config = NULL;

        int   _agcMode  = 1;
        float _agcAttack = 50.0f;
        float _agcDecay = 5.0f;

        std::string name;
    };
}
