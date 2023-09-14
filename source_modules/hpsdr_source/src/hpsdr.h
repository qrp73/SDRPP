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
#pragma once
#include <utils/net.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <memory>
#include <vector>
#include <string>
#include <thread>

#define HERMES_METIS_TIMEOUT    1000

namespace hpsdr {

    enum HpsdrStatus {
        none         = 1,
        not_sending  = 2,
        sending_data = 3,
    };
    
    enum HpsdrBoardId {
        Metis = 0,
        Hermes = 1,
        Griffin = 2,
        Angelia = 4,
        Orion = 5,
        HermesLite = 6,
    };

    enum HpsdrSampleRate {
        SR_48KHZ  = 0,
        SR_96KHZ  = 1,
        SR_192KHZ = 2,
        SR_384KHZ = 3
    };
    
    struct Info {
        net::Address addr;
        HpsdrStatus status;
        uint8_t mac[6];
        uint8_t verMajor;
        uint8_t verMinor;
        HpsdrBoardId boardId;

        const char* getBoardName() {
            switch (boardId) {
                case Metis: return "Metis";
                case Hermes: return "Hermes";
                case Griffin: return "Griffin";
                case Angelia: return "Angelia";
                case Orion: return "Orion";
                case HermesLite: return "HermesLite";
                default: return "Unknown";
            }
        }

        bool operator==(const Info& b) const {
            return !memcmp(mac, b.mac, 6);
        }
    };

    class Client {
    public:
        Client(const net::Address& addr, dsp::stream<dsp::complex_t> *iqStream);

        void close();

        void start();
        void stop();

        void setSamplerate(HpsdrSampleRate sampleRateId, uint32_t sampleRate);
        void setFrequency(uint32_t freq);
        void setPreamp(bool enable);
        void setAtten(int atten, bool enable);
        void setDither(bool enable);
        void setRandomizer(bool enable);
        void autoFilters(double freq);

    private:
        net::Address                    _addr;
        dsp::stream<dsp::complex_t>*    _iqStream;
        bool                            _isRunning;
        std::shared_ptr<net::Socket>    _sock;
        std::thread                     _workerThread;
        
        void worker();

        uint8_t lastFilt = 0;
        
        uint8_t  state_ADCOVR;  // ADC OVERFLOW
        uint8_t  state_PTT;     // PTT,DASH,DOT buttons state
        uint8_t  state_IO;      // IO0,IO1,IO2,IO3 state
        uint8_t  state_SwVer;
        uint16_t state_AIN1;    // Forward Power from Alex or Apollo
        uint16_t state_AIN2;    // Reverse Power from Alex or Apollo
        uint16_t state_AIN3;    // AIN3 from Penny or Hermes
        uint16_t state_AIN4;    // AIN4 from Penny or Hermes
        uint16_t state_AIN5;    // Forward Power from Penelope or Hermes
        uint16_t state_AIN6;    // 13.8v supply on Hermes
        
        uint8_t  ctrl_NumberOfRx   = 1;
        uint8_t  ctrl_SampleRateId = 2;
        uint32_t ctrl_NCO[9]       = {0,0,0,0,0,0,0,0,0}; // TX,RX1,RX2,..,RX8
        bool     ctrl_MOX          = false;
        bool     ctrl_Preamp       = false;   // Preamplifier
        bool     ctrl_Dither       = false;
        bool     ctrl_Randomizer   = false;
        bool     ctrl_Duplex       = true;    // duplex ( 0 = off  1 = on )
        uint8_t  ctrl_Attenuator   = 0;
        
        
        const int _iqSize = 8192;   // iq buffer sample count
        int _iqBufferIndexes[9] = { 0,0,0,0,0,0,0,0,0 };
        uint32_t _rxSeqEP4 = 0xffffffff;
        uint32_t _rxSeqEP6 = 0xffffffff;
        uint32_t _txSeqEP2 = 0;
        uint32_t _rxSampleCounter = 0;
        
        uint32_t _sampleRate;
        int _controlPage = 0;
        
        void sendStartStop(bool iq, bool bs);
        void sendAudio();
        void processFlowToRadio(uint8_t* buffer);
        void processControlToRadio(uint8_t* buffer);
        bool processFlowFromRadio(uint8_t* buffer);
        void processControlFromRadio(uint8_t* buffer);
        void processBandscopeFromRadio(uint8_t* buffer);
    };

    std::vector<Info> discover();
    std::shared_ptr<Client> open(std::string host, int port, dsp::stream<dsp::complex_t> *iqStream);
    std::shared_ptr<Client> open(const net::Address& addr, dsp::stream<dsp::complex_t> *iqStream);
}
