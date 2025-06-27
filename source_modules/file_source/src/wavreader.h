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

#pragma once
#include <stdint.h>
#include <string.h>
#include <fstream>
#include <utils/flog.h>


enum WAVE_FORMAT {
    PCM = 1,              /* PCM */
    IEEE_FLOAT = 3,       /* IEEE float */
    ALAW = 6,             /* 8-bit ITU-T G.711 A-law */
    MULAW = 7,            /* 8-bit ITU-T G.711 µ-law */
    EXTENSIBLE = 0xFFFE,  /* Determined by SubFormat */
};

class WavReader {
public:
    WavReader(std::string path) {
        _valid = false;
        _fileSize = 0;
        _dataOffset = 0;
        _file = std::ifstream(path.c_str(), std::ios::binary);
        _file.seekg(0, std::ios_base::end);
        _fileSize = _file.tellg();
        _file.seekg(0, std::ios_base::beg);
        uint32_t riff_id;
        uint32_t riff_size;
        uint32_t riff_type;
        _file.read((char*)&riff_id,   sizeof(riff_id));
        _file.read((char*)&riff_size, sizeof(riff_size));
        _file.read((char*)&riff_type, sizeof(riff_type));
        if (memcmp(&riff_id,   "RIFF", 4) != 0 ||
            memcmp(&riff_type, "WAVE", 4) != 0) { 
            throw std::runtime_error("Invalid WAV file");
        }
        reset();
    }

    WAVE_FORMAT getFormat() {
        return (WAVE_FORMAT)_hdr.wFormatTag;
    }

    const char* getFormatName() {
        switch ( getFormat() ) {
            case WAVE_FORMAT::PCM:          return "PCM";
            case WAVE_FORMAT::IEEE_FLOAT:   return "IEEE_FLOAT";
            case WAVE_FORMAT::ALAW:         return "ALAW";
            case WAVE_FORMAT::MULAW:        return "MULAW";
            case WAVE_FORMAT::EXTENSIBLE:   return "EXTENSIBLE";
            default:                        return "UNKNOWN";
        }
    }

    uint16_t getBitDepth() {
        return _hdr.wBitsPerSample;
    }

    uint16_t getChannelCount() {
        return _hdr.wChannels;
    }

    uint32_t getBlockAlign() {
        return _hdr.wBlockAlign;
    }

    uint64_t getSampleCount() {
        std::lock_guard<std::recursive_mutex> lck(_mtx);
        return (uint64_t)((_fileSize-_dataOffset) / _hdr.wBlockAlign);
    }

    uint32_t getSampleRate() {
        return _hdr.dwSamplesPerSec;
    }
    
    bool isValid() {
        return _valid;
    }

    void reset() {
        std::lock_guard<std::recursive_mutex> lck(_mtx);
        _valid = false;
        _fileSize = 0;
        _dataOffset = 0;
        std::memset(&_hdr, 0, sizeof(_hdr));
        _file.clear(); // reset failbit/eofbit
        _file.seekg(0, std::ios_base::end);
        _fileSize = _file.tellg();
        _file.seekg(sizeof(uint32_t) * 3, std::ios_base::beg);
        bool isFmtPresent = false;
        bool isDataPresent = false;
        while (_file.tellg() < _fileSize) {
            uint32_t chunkId;
            uint32_t chunkSize;
            _file.read((char*)&chunkId,   sizeof(chunkId));
            _file.read((char*)&chunkSize, sizeof(chunkSize));
            char sbuf[1024];
            memset(sbuf, 0, sizeof(sbuf));
            memcpy(sbuf, &chunkId, 4);
            //flog::debug("chunk \"{0}\", size {1}", sbuf, chunkSize);
            if (memcmp(&chunkId, "fmt ", 4) == 0) {
                const auto fmt_Size = chunkSize;
                if (fmt_Size < 16 || (fmt_Size > 16 && fmt_Size < 18)) {
                    throw std::runtime_error("Invalid fmt chunk size " + std::to_string(fmt_Size));
                }                
                _file.read((char*)&_hdr.wFormatTag,       sizeof(_hdr.wFormatTag));
                _file.read((char*)&_hdr.wChannels,        sizeof(_hdr.wChannels));
                _file.read((char*)&_hdr.dwSamplesPerSec,  sizeof(_hdr.dwSamplesPerSec));
                _file.read((char*)&_hdr.dwAvgBytesPerSec, sizeof(_hdr.dwAvgBytesPerSec));
                _file.read((char*)&_hdr.wBlockAlign,      sizeof(_hdr.wBlockAlign));
                _file.read((char*)&_hdr.wBitsPerSample,   sizeof(_hdr.wBitsPerSample));
                if (fmt_Size > 16) {
                    uint16_t fmt_ExtraSize; // cbSize
                    _file.read((char*)&fmt_ExtraSize, sizeof(fmt_ExtraSize));
                    if (_hdr.wFormatTag == WAVE_FORMAT::EXTENSIBLE && fmt_ExtraSize >= 22) { // WAVE_FORMAT_EXTENSIBLE
                        uint16_t validBitsPerSample;
                        uint32_t channelMask;
                        uint32_t subFormat_Data1;
                        uint16_t subFormat_Data2;
                        uint16_t subFormat_Data3;
                        uint64_t subFormat_Data4;
                        _file.read((char*)&validBitsPerSample, sizeof(validBitsPerSample));
                        _file.read((char*)&channelMask, sizeof(channelMask));
                        _file.read((char*)&subFormat_Data1, sizeof(subFormat_Data1));
                        _file.read((char*)&subFormat_Data2, sizeof(subFormat_Data2));
                        _file.read((char*)&subFormat_Data3, sizeof(subFormat_Data3));
                        _file.read((char*)&subFormat_Data4, sizeof(subFormat_Data4));
                        if (subFormat_Data1 == 1) {
                            _hdr.wFormatTag = WAVE_FORMAT::PCM;
                        } else if (subFormat_Data1 == 2) {
                            _hdr.wFormatTag = WAVE_FORMAT::IEEE_FLOAT;
                        } else {
                            flog::warn("validBitsPerSample = {0}", validBitsPerSample);
                            flog::warn("channelMask        = {0}", channelMask);
                            flog::warn("subFormat.Data1    = {0}", subFormat_Data1);
                            flog::warn("subFormat.Data2    = {0}", subFormat_Data2);
                            flog::warn("subFormat.Data3    = {0}", subFormat_Data3);
                            flog::warn("subFormat.Data4    = {0}", subFormat_Data4);
                            throw std::runtime_error("Unknown format type for WAVE_FORMAT_EXTENSIBLE");
                        }
                    } else {
                        // Skip extra bytes
                        _file.seekg(fmt_ExtraSize, std::ios_base::cur);
                    }
                }
                isFmtPresent = true;
            } else if (memcmp(&chunkId, "data", 4) == 0) {
                isDataPresent = true;
                break;
            } else {
                flog::warn("skip unknown chunk \"{0}\", size {1}", sbuf, chunkSize);
                // skip unknown chunk type
                _file.seekg(chunkSize, std::ios_base::cur);
            }
        }
        _dataOffset = isDataPresent ? _file.tellg() : _fileSize;
        _valid = isFmtPresent && isDataPresent;
    }

    uint64_t getSamplePosition() {
        if (!_valid) return 0;
        std::lock_guard<std::recursive_mutex> lck(_mtx);
        std::streampos offset = _file.tellg() - _dataOffset;
        return offset / _hdr.wBlockAlign;
    }
    void seek(uint64_t sampleNumber) {
        if (!_valid) return;
        std::lock_guard<std::recursive_mutex> lck(_mtx);
        sampleNumber = std::min(sampleNumber, getSampleCount());
        std::streampos offset = sampleNumber * _hdr.wBlockAlign;
        _file.seekg(_dataOffset + offset, std::ios_base::beg);
    }

    size_t readSamples(void* data, size_t size) {
        std::lock_guard<std::recursive_mutex> lck(_mtx);
        size_t bytesAvailable = (_fileSize - _dataOffset) - _file.tellg();
        if (bytesAvailable < 0) { 
            bytesAvailable = 0;
        }
        if (!_valid || bytesAvailable < 1) { 
            //flog::warn("endOfFile reached");
            return 0;
        }
        size_t count = std::clamp(size, (size_t)0, bytesAvailable);
        _file.read((char*)data, count);
        size_t read = _file.gcount();
        return read;
    }

    void close() {
        std::lock_guard<std::recursive_mutex> lck(_mtx);
        _file.close();
    }

private:
    struct RIFF_HDR_t {
        uint16_t wFormatTag;         // Format category
        uint16_t wChannels;          // Number of channels
        uint32_t dwSamplesPerSec;    // Sampling rate
        uint32_t dwAvgBytesPerSec;   // For buffer estimation
        uint16_t wBlockAlign;        // Data block size
        uint16_t wBitsPerSample;     // Sample size
    };

    std::recursive_mutex _mtx;
    bool           _valid;
    std::ifstream  _file;
    std::streampos _fileSize;
    std::streampos _dataOffset;
    RIFF_HDR_t     _hdr;
};
