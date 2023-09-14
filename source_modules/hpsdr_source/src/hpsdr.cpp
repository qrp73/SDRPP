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
#include "convert_be.h"
#include <utils/flog.h>

namespace hpsdr {

    Client::Client(const net::Address& addr, dsp::stream<dsp::complex_t> *iqStream) {
        _addr = addr;
        _iqStream = iqStream;
        _isRunning = false;
    }

    void Client::sendStartStop(bool iq, bool bs) {
        uint8_t dgram[64];
        memset(dgram, 0, sizeof(dgram));

        setUInt16_BE(dgram+0, 0xeffe);
        dgram[2] = 0x04;    // data send state send (0x00=stop)
        dgram[3] = 0x00;
        if (iq) dgram[3] |= 0x01;   // enable IQ data
        if (bs) dgram[3] |= 0x02;   // enable BANDSCOPE data

        // Send packet
        _sock->send(dgram, sizeof(dgram));
    }
    
    void Client::start() {
        if (_isRunning) return;
        _sock = net::openudp(_addr);
        _isRunning = true;

        // initialize
        _rxSeqEP4 = 0xffffffff;
        _rxSeqEP6 = 0xffffffff;
        _txSeqEP2 = 0;

        // Start worker
        _workerThread = std::thread(&Client::worker, this);
        
        sendStartStop(true, false);

        // send all control pages
        for (_controlPage = 0; _controlPage < 12; _controlPage++)
        {
            sendAudio();
        }
        _controlPage = 0;
    }

    void Client::stop() {
        if (!_isRunning) return;
        _isRunning = false;

        _iqStream->stopWriter();
        sendStartStop(false, false);
        _sock->close();
        if (_workerThread.joinable()) { _workerThread.join(); }
        _iqStream->clearWriteStop();
    }

    void Client::setSamplerate(HpsdrSampleRate sampleRateId, uint32_t sampleRate) {
        ctrl_SampleRateId = (uint8_t)sampleRateId;
        _sampleRate = sampleRate;
    }

    void Client::setFrequency(uint32_t freq) {
        for (int i=0; i < 9; i++)
            ctrl_NCO[i] = freq;
        // sent page #2 to update frequency on next frame
        _controlPage = 2;
    }

    void Client::setPreamp(bool enable) {
        ctrl_Preamp = enable;
    }

    void Client::setAtten(int gain, bool enable) {
        uint8_t v = gain & 0x3f;
        if (enable) v |= 1 << 6;
        ctrl_Attenuator = v;
    }
    void Client::setDither(bool enable) {
        ctrl_Dither = enable;
    }
    void Client::setRandomizer(bool enable) {
        ctrl_Randomizer = enable;
    }

    // TODO: add filter support
    void Client::autoFilters(double freq) {
#if 0
        uint8_t filt = (freq >= 3000000.0) ? (1 << 6) : 0;
        
        if (freq <= 2000000.0) {
            filt |= (1 << 0);
        }
        else if (freq <= 4000000.0) {
            filt |= (1 << 1);
        }
        else if (freq <= 7300000.0) {
            filt |= (1 << 2);
        }
        else if (freq <= 14350000.0) {
            filt |= (1 << 3);
        }
        else if (freq <= 21450000.0) {
            filt |= (1 << 4);
        }
        else if (freq <= 29700000.0) {
            filt |= (1 << 5);
        }

        // Write only if the config actually changed
        if (filt != lastFilt) {
            lastFilt = filt;

            flog::warn("Setting filters");

            // Set direction and wait for things to be processed
            writeI2C(I2C_PORT_2, 0x20, 0x00, 0x00);

            // Set pins
            writeI2C(I2C_PORT_2, 0x20, 0x0A, filt);
        }
#endif
    }

    void Client::sendAudio() {
        uint8_t dgram[8 + 512 * 2]; // 1032
        memset(dgram, 0, sizeof(dgram));
        
        setUInt16_BE(dgram+0, 0xeffe);
        dgram[2] = 0x01;
        dgram[3] = 0x02;    // EP=2
        setUInt32_BE(dgram+4, _txSeqEP2++);
        
        processFlowToRadio(dgram + 8);
        processFlowToRadio(dgram + 520);
        
        // Send packet
        _sock->send(dgram, sizeof(dgram));
    }
    
    void Client::processFlowToRadio(uint8_t* buffer) {
        buffer[0] = 0x7f;
        buffer[1] = 0x7f;
        buffer[2] = 0x7f;

        processControlToRadio(buffer+3);

        //
        // fill in TX I/Q and audio data
        // 63 samples (16l+16b)
        //
        //for (var n = 8; n < length - 8; n++)
        //    buffer[offset + n] = 0;
        //_ep2samples += 63;

        //var numberOfRx = _radio.GetNumberOfRx();
        //var maxCnt = 12;  // numberOfRx + 2; // CTRL + TXNCO + RXNCO[]
        //if (_txCnt >= maxCnt) _txCnt = 0;
        
        // simple round robin for control data
        _controlPage++;
        if (_controlPage > 11)
            _controlPage = 0;
    }
    
    void Client::processControlToRadio(uint8_t* buffer) {
        buffer[0] = ((_controlPage & 0x7f) << 1) | (ctrl_MOX ? 1:0);
        switch (_controlPage) {
            case 0: 
                buffer[1] = (uint8_t)(ctrl_SampleRateId & 3);
                buffer[4] = (uint8_t)(
                    (((ctrl_NumberOfRx - 1) & 7) << 3) |
                    ((ctrl_Duplex ? 1:0) << 2));
                if (ctrl_Preamp)
                    buffer[3] |= 1 << 2;
                else
                    buffer[1] &= (1 << 2) ^ 0xff;
                if (ctrl_Dither)
                    buffer[3] |= 1 << 3;
                else
                    buffer[1] &= (1 << 3) ^ 0xff;
                if (ctrl_Randomizer)
                    buffer[3] |= 1 << 4;
                else
                    buffer[1] &= (1 << 4) ^ 0xff;
                break;
            case 1:  // transmitter NCO
            case 2:  // receiver #1 NCO
            case 3:  // receiver #2 NCO
            case 4:  // receiver #3 NCO
            case 5:  // receiver #4 NCO
            case 6:  // receiver #5 NCO
            case 7:  // receiver #6 NCO
            case 8:  // receiver #7 NCO
            case 9:  // receiver #8 NCO
                setUInt32_BE(buffer+1, ctrl_NCO[_controlPage-1]);
                break;
            case 10: // Hermes attenuator
                buffer[4] = ctrl_Attenuator;
                break;
        }
    }

    const int usableBufLen[9] = {
        0,			// filler
        512 - 0,	// 1 RX
        512 - 0,	// 2 RX
        512 - 4,
        512 - 10,
        512 - 24,
        512 - 10,
        512 - 20,
        512 - 4		// 8 RX
    };

    bool Client::processFlowFromRadio(uint8_t* buffer) {
        
        // Make sure this is a valid frame by checking the sync
        if (buffer[0] != 0x7F || buffer[1] != 0x7F || buffer[2] != 0x7F) {
            flog::warn("ep6 SYNC LOSS");
            return false;
        }
        processControlFromRadio(buffer+3);
        
        // extract each of the receivers samples data
        int numberOfRx = ctrl_NumberOfRx;
        int bufLen = usableBufLen[numberOfRx];
        int channelStep = numberOfRx * 6 + 2;
        for (int r = 0; r < numberOfRx; r++)
        {
            if (r != 0) continue; // only RX#1 is used
            
            int index = _iqBufferIndexes[r];
            for (uint8_t* ptr = buffer + 8 + r * 6; ptr < buffer + bufLen; ptr += channelStep)
            {
                int32_t si = getInt24_BE(ptr+0);
                int32_t sq = getInt24_BE(ptr+3);
                                
                // Convert to float according to AES17 standard [float=value/(pow(2,N)/2 - 1)]
                _iqStream->writeBuf[index].im = (float)si / (float)0x7fffff;
                _iqStream->writeBuf[index].re = (float)sq / (float)0x7fffff;
                                
                index++;
                if (index >= _iqSize)
                {
                    index -= _iqSize;
                    _iqBufferIndexes[r] = index;
                    _iqStream->swap(_iqSize);
                }
            }
            _iqBufferIndexes[r] = index;
        }
        //for (uint8_t* ptr = buffer + 8 + numberOfRx * 6; ptr < buffer + bufLen; ptr += channelStep)
        //{
        //    ProcessFlowFromMic(ptr);    // mic data: 16 bit
        //}
        uint32_t sampleCount = (bufLen - 8) / channelStep;
        _rxSampleCounter += (uint)sampleCount;
        //_ep6samples += (uint)sampleCount;
                        
        // upstream EP2 audio with fixed 48 kHz rate
        uint32_t sampleRate = _sampleRate;
        sampleRate /= 48000;
        if (_rxSampleCounter >= sampleRate * 63 * 2)
        {
            _rxSampleCounter -= sampleRate * 63 * 2;
            sendAudio();
        }
        return true;
    }
    
    void Client::processControlFromRadio(uint8_t* buffer) {
        uint8_t c0 = buffer[0];
#if 0        
        // Check if this is a response
        if (c0 & (1 << 7)) {
            uint8_t reg = (buffer[0] >> 1) & 0x3F;
            flog::warn("Got response! Reg={0}", reg);
        }
#endif
        state_PTT  = c0 & 7;
        switch (c0 >> 3) {
            case 0: {
                uint8_t c1 = buffer[1];
                state_ADCOVR = c1 & 1;
                state_IO = (c1 >> 1) & 0x0f;
                state_SwVer = buffer[4];
                break; 
                }
            case 1:
                state_AIN5 = getUInt16_BE(buffer+1);
                state_AIN1 = getUInt16_BE(buffer+3);
                break;
            case 2:
                state_AIN2 = getUInt16_BE(buffer+1);
                state_AIN3 = getUInt16_BE(buffer+3);
                break;
            case 3:
                state_AIN4 = getUInt16_BE(buffer+1);
                state_AIN6 = getUInt16_BE(buffer+3);
                break;
        }
    }

    void Client::processBandscopeFromRadio(uint8_t* buffer) {
        // TODO: implement
    }
    
    
    

    void Client::worker() {
        uint8_t rbuf[2048];
        while (_isRunning) {
            // Wait for a packet or exit if connection closed
            int len = _sock->recv(rbuf, 2048);
            
            if (len <= 0) {
                break;
            }

            if (len < 8 || getUInt16_BE(rbuf+0) != 0xeffe || rbuf[2] != 0x01) {
                flog::warn("received unknown packet {0} bytes, id={1}, type={2}", len, getUInt16_BE(rbuf+0), rbuf[2]);
                continue;
            }
            
            uint32_t seq = getUInt32_BE(rbuf+4);
            switch ( rbuf[3] ) {
                case 4: // EP4: bandscope
                    if ( seq != (uint32_t)(_rxSeqEP4 + 1) )
                        flog::warn("ep4 packet loss: {0}, {1}", _rxSeqEP4, seq);
                    _rxSeqEP4 = seq;
                    if (len != 1032) {
                        flog::warn("ep4 truncated packet: {0} bytes", len);
                        continue;
                    }
                    processBandscopeFromRadio(rbuf+8);
                    break;
                case 6: // EP6: iq flow
                    if ( seq != (uint32_t)(_rxSeqEP6 + 1))
                        flog::warn("ep6 packet loss: {0}, {1}", _rxSeqEP6, seq);
                    _rxSeqEP6 = seq;
                    if (len != 1032) {
                        flog::warn("ep6 truncated packet: {0} bytes", len);
                        continue;
                    }
                    processFlowFromRadio(rbuf+8) &&
                        processFlowFromRadio(rbuf+8+512);
                    break;
                default:
                    flog::warn("unknown endPoint received={0}", rbuf[3]);
                    break;
            }
        }
    }

    std::vector<Info> discover() {
        auto sock = net::openudp("0.0.0.0", 1024);
        
        // Build discovery packet
        // <0xEFFE><0x02><60 bytes of 0x00>
        uint8_t dgram[63];
        memset(dgram, 0, sizeof(dgram));
        
        setUInt16_BE(dgram+0, 0xeffe);
        dgram[2] = 0x02;

        net::Address baddr("255.255.255.255", 1024);
        flog::info("HPSDR: send discovery for {0}:{1}", baddr.getIPStr(), baddr.getPort());
        sock->send(dgram, sizeof(dgram), &baddr);
        
        std::vector<Info> devices;
        while (true) {
            net::Address addr;
            uint8_t resp[1024];

            // Wait for a response
            auto len = sock->recv(resp, sizeof(resp), false, HERMES_METIS_TIMEOUT, &addr);
            
            // Give up if timeout or error
            if (len <= 0) { 
                break; 
            }
            // Verify that response is valid
            if (len < 11 || getUInt16_BE(resp+0) != 0xeffe) {
                flog::warn("HPSDR: unknown packet {0} bytes from {1}:{2}", len, addr.getIPStr(), addr.getPort());
                continue;
            }
            
            Info info;
            info.addr = addr;
            info.status = (HpsdrStatus)resp[2];
            memcpy(info.mac, resp+3, 6);
            info.verMajor = resp[9] / 10;
            info.verMinor = resp[9] % 10;
            info.boardId = (HpsdrBoardId)resp[10];
            
            char macstr[128];
            sprintf(macstr, "%02x:%02x:%02x:%02x:%02x:%02x", 
                info.mac[0], info.mac[1], info.mac[2], 
                info.mac[3], info.mac[4], info.mac[5]);
            flog::info("HPSDR: recv {0}:{1}, status={2}, mac={3}, board={4}/{5} v{6}.{7}", 
                addr.getIPStr(), addr.getPort(), 
                (uint8_t)info.status,
                macstr, 
                (uint8_t)info.boardId, info.getBoardName(), 
                info.verMajor, info.verMinor);

            devices.push_back(info);
        }
        

        return devices;
    }

    std::shared_ptr<Client> open(std::string host, int port, dsp::stream<dsp::complex_t> *iqStream) {
        return open(net::Address(host, port), iqStream);
    }

    std::shared_ptr<Client> open(const net::Address& addr, dsp::stream<dsp::complex_t> *iqStream) {
        return std::make_shared<Client>(addr, iqStream);
    }
}
