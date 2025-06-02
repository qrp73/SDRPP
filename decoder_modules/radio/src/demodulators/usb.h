#pragma once
#include "../demod.h"
#include <dsp/demod/ssb.h>
#include <dsp/convert/mono_to_stereo.h>

namespace demod {
    class USB : public Demodulator {
    public:
        USB() {}

        USB(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~USB() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;
            _config = config;

            // Load config
            config->acquire();
            if (config->conf[name][getName()].contains("agcEnabled")) {
                _agcEnabled = config->conf[name][getName()]["agcEnabled"];
            }
            if (config->conf[name][getName()].contains("agcAttack")) {
                _agcAttack = config->conf[name][getName()]["agcAttack"];
            }
            if (config->conf[name][getName()].contains("agcDecay")) {
                _agcDecay = config->conf[name][getName()]["agcDecay"];
            }
            config->release();

            // Define structure
            demod.init(input, dsp::demod::SSB<dsp::stereo_t>::Mode::USB, bandwidth, getIFSampleRate(), _agcEnabled, _agcAttack / getIFSampleRate(), _agcDecay / getIFSampleRate());
        }

        void start() { demod.start(); }

        void stop() { demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Checkbox(("##_radio_usb_agc_enable_" + name).c_str(), &_agcEnabled)) {
                demod.setAGCEnabled(_agcEnabled);
                _config->acquire();
                _config->conf[name][getName()]["agcEnabled"] = _agcEnabled;
                _config->release(true);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("AGC");
            if (!_agcEnabled) ImGui::BeginDisabled();
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_usb_agc_attack_" + name).c_str(), &_agcAttack, 1.0f, 200.0f)) {
                demod.setAGCAttack(_agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcAttack"] = _agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_usb_agc_decay_" + name).c_str(), &_agcDecay, 1.0f, 20.0f)) {
                demod.setAGCDecay(_agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcDecay"] = _agcDecay;
                _config->release(true);
            }
            if (!_agcEnabled) ImGui::EndDisabled();
        }

        void setBandwidth(double bandwidth) { demod.setBandwidth(bandwidth); }

        void setInput(dsp::stream<dsp::complex_t>* input) { demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "USB"; }
        double getIFSampleRate() { return 24000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 2700.0; }
        double getMinBandwidth() { return 500.0; }
        double getMaxBandwidth() { return getIFSampleRate() / 2.0; }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 100.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_LOWER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return true; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &demod.out; }

    private:
        dsp::demod::SSB<dsp::stereo_t> demod;

        ConfigManager* _config;

        bool  _agcEnabled = true;
        float _agcAttack = 50.0f;
        float _agcDecay = 5.0f;

        std::string name;
    };
}
