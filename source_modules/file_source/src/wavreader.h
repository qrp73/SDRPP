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

    uint32_t getSampleCount() {
        return _hdr.data_size / _hdr.wBlockAlign;
    }

    uint32_t getSampleRate() {
        return _hdr.dwSamplesPerSec;
    }
    
    bool isValid() {
        return _valid;
    }

    void reset() {
        _fmtPresent = false;
        _valid = false;
        std::memset(&_hdr, 0, sizeof(_hdr));
        _file.seekg(sizeof(uint32_t) * 3, std::ios_base::beg);
        
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
                _hdr.fmt_id = chunkId;
                _hdr.fmt_size = chunkSize;
                _file.read((char*)&_hdr.wFormatTag,       sizeof(_hdr.wFormatTag));
                _file.read((char*)&_hdr.wChannels,        sizeof(_hdr.wChannels));
                _file.read((char*)&_hdr.dwSamplesPerSec,  sizeof(_hdr.dwSamplesPerSec));
                _file.read((char*)&_hdr.dwAvgBytesPerSec, sizeof(_hdr.dwAvgBytesPerSec));
                _file.read((char*)&_hdr.wBlockAlign,      sizeof(_hdr.wBlockAlign));
                _file.read((char*)&_hdr.wBitsPerSample,   sizeof(_hdr.wBitsPerSample));
                if (chunkSize == 18)
                {
                    // read any extra values
                    _file.read((char*)&_hdr.__zero,   sizeof(_hdr.__zero));
                    _file.seekg(_hdr.__zero, std::ios_base::cur);
                }
                _fmtPresent = true;
            } else if (memcmp(&chunkId, "data", 4) == 0) {
                _hdr.data_id   = chunkId;
                _hdr.data_size = chunkSize;
                _data_offset = _file.tellg();
                _bytesAvailable = chunkSize;
                break;
            } else {
                flog::warn("skip unknown chunk \"{0}\", size {1}", sbuf, chunkSize);
                // skip unknown chunk type
                _file.seekg(chunkSize, std::ios_base::cur);
            }
        }
        _valid = _fmtPresent && _file.tellg() < _fileSize;
    }

    uint64_t getSamplePosition() {
        if (!_valid) return 0;
        std::streampos offset = _file.tellg() - _data_offset;
        return offset / _hdr.wBlockAlign;
    }
    void seek(uint64_t sampleNumber) {
        if (!_valid) return;
        std::streampos offset = sampleNumber * _hdr.wBlockAlign;
        _file.seekg(_data_offset + offset, std::ios_base::beg);
        _bytesAvailable = _hdr.data_size - offset;
    }

    size_t readSamples(void* data, size_t size) {
        if (!_valid || _bytesAvailable < 1) { 
            //flog::warn("endOfFile reached");
            return 0;
        }
        size_t count = std::min(std::max(size, (size_t)0), _bytesAvailable);
        _file.read((char*)data, count);
        size_t read = _file.gcount();
        _bytesAvailable -= read;
        return read;
    }

    void close() {
        _file.close();
    }

private:
    struct RIFF_HDR_t {
        // Format chunk (24 bytes)
        uint32_t fmt_id;             // "fmt "
        uint32_t fmt_size;//16?
        uint16_t wFormatTag;         // Format category
        uint16_t wChannels;          // Number of channels
        uint32_t dwSamplesPerSec;    // Sampling rate
        uint32_t dwAvgBytesPerSec;   // For buffer estimation
        uint16_t wBlockAlign;        // Data block size
        uint16_t wBitsPerSample;     // Sample size
        uint16_t __zero;

        // Data chunk
        uint32_t data_id;            // "data"
        uint32_t data_size;          // The data size should be file size - 36 bytes.
    };

    bool           _valid = false;
    std::ifstream  _file;
    std::streampos _fileSize;
    RIFF_HDR_t     _hdr;
    bool           _fmtPresent = false;
    size_t         _bytesAvailable = 0;
    std::streampos _data_offset = 0;
};
