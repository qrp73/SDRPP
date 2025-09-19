#include <volk/volk.h>
#include <stdexcept>
#include <dsp/buffer/buffer.h>
#include <dsp/stream.h>
#include <vector>
#include <map>
#include <algorithm>
#include <utils/flog.h>
#include "wav.h"

using runtime_error = std::runtime_error;

namespace wav {
    const char* MP3_FILE_TYPE           = "MP3 ";
    const char* WAVE_FILE_TYPE          = "WAVE";
    const char* FORMAT_MARKER           = "fmt ";
    const char* DATA_MARKER             = "data";
    const uint32_t FORMAT_HEADER_LEN    = 16;
    const uint16_t SAMPLE_TYPE_PCM      = 1;

    std::map<SampleType, int> SAMP_BITS = {
        { SAMP_TYPE_UINT8, 8 },
        { SAMP_TYPE_INT16, 16 },
        { SAMP_TYPE_INT24, 24 },
        { SAMP_TYPE_INT32, 32 },
        { SAMP_TYPE_FLOAT32, 32 }
    };

    static inline bool isIntegerSampleType(SampleType type) {
        return type == SAMP_TYPE_UINT8 || type == SAMP_TYPE_INT16 ||
            type == SAMP_TYPE_INT24 || type == SAMP_TYPE_INT32;
    }
    
    Writer::Writer(int channels, uint64_t samplerate, Format format, SampleType type) {
        // Validate channels and samplerate
        if (channels < 1) { throw runtime_error("Channel count must be greater or equal to 1"); }
        if (!samplerate) { throw runtime_error("Samplerate must be non-zero"); }
        
        // Initialize variables
        _channels = channels;
        _samplerate = samplerate;
        _format = format;
        _type = type;
    }

    Writer::~Writer() { close(); }

    bool Writer::open(std::string path) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Close previous file
        if (_lame || _flacEncoder || rw.isOpen()) { close(); }

        // Reset work values
        samplesWritten = 0;
        auto bitsPerSample = SAMP_BITS[_type];
        bytesPerSamp = ((int)(bitsPerSample + 7) / 8) * _channels;
        _halfRangeMH = (double)(1L<<(bitsPerSample-1)) - 0.5d;
        
        if (_format == FORMAT_MP3) {
            if (_channels != 1 && _channels != 2) {
                flog::error("not supported channel count for MP3: {}", (int)_channels);
                return false;
            }
            _lame = lame_init();
            if (!_lame) {
                flog::error("lame_init() failed");
                return false;
            }
            lame_set_write_id3tag_automatic(_lame, 1);
            lame_set_in_samplerate(_lame, _samplerate);
            lame_set_num_channels(_lame, _channels);
            // https://sourceforge.net/p/lame/svn/HEAD/tree/trunk/lame/include/lame.h
            lame_set_VBR(_lame, vbr_default);
            lame_set_VBR_q(_lame, 5);  // VBR quality level.  0=highest  9=lowest [0=32.5kB/s ;2=24.5kB/s; 4=20kB/s; 5=17kB/s; 7=12.8kB/s; 9=10.5kB/s]
            //lame_set_VBR(_lame, vbr_off);
            //lame_set_brate(_lame, 192);
            lame_set_quality(_lame, 2); // 0=best (very slow); 2=near-best quality, not too slow; 5=good quality, fast; 7=ok quality, really fast; 9=worst;
            if (lame_init_params(_lame) < 0) {
                flog::error("lame_init_params() failed");
                lame_close(_lame);
                _lame = nullptr;
                return false;
            }
            _mp3Buffer.resize(1.25 * STREAM_BUFFER_SIZE + 7200);
            _mp3file = fopen(path.c_str(), "wb");
            if (!_mp3file) {
                flog::error("fopen() failed: {} [\"{}\"]", strerror(errno), path);
                lame_close(_lame);
                _lame = nullptr;
                return false;
            }
            return true;            
        }        
        if (_format == FORMAT_FLAC) {
            if (!isIntegerSampleType(_type)) {
                flog::error("not supported sample format for FLAC: {}", (int)_type);
                return false;
            }
            _flacEncoder = FLAC__stream_encoder_new();
            if (!_flacEncoder) {
                flog::error("FLAC__stream_encoder_new() failed");
                return false;
            }
            FLAC__stream_encoder_set_channels(_flacEncoder, _channels);
            FLAC__stream_encoder_set_sample_rate(_flacEncoder, _samplerate);
            FLAC__stream_encoder_set_bits_per_sample(_flacEncoder, bitsPerSample);
            auto status = FLAC__stream_encoder_init_file(_flacEncoder, path.c_str(), nullptr, nullptr);
            if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
                flog::error("FLAC__stream_encoder_init_file() failed: {}", (int)status);
                FLAC__stream_encoder_delete(_flacEncoder);
                _flacEncoder = nullptr;
                return false;
            }
            return true;
        }
        if (_format == FORMAT_WAV) {
            // Fill header
            hdr.codec = isIntegerSampleType(_type) ? CODEC_PCM : CODEC_FLOAT;
            hdr.channelCount = _channels;
            hdr.sampleRate = _samplerate;
            hdr.bitDepth = bitsPerSample;
            hdr.bytesPerSample = bytesPerSamp;
            hdr.bytesPerSecond = bytesPerSamp * _samplerate;
    
            // Precompute sizes and allocate buffers
            switch (_type) {
                case SAMP_TYPE_UINT8:
                    bufU8 = dsp::buffer::alloc<uint8_t>(STREAM_BUFFER_SIZE * _channels);
                    break;
                case SAMP_TYPE_INT16:
                    bufI16 = dsp::buffer::alloc<int16_t>(STREAM_BUFFER_SIZE * _channels);
                    break;
                case SAMP_TYPE_INT24:
                    bufI24 = dsp::buffer::alloc<uint8_t>(STREAM_BUFFER_SIZE * _channels * 3);
                    break;
                case SAMP_TYPE_INT32:
                    bufI32 = dsp::buffer::alloc<int32_t>(STREAM_BUFFER_SIZE * _channels);
                    break;
                case SAMP_TYPE_FLOAT32:
                    break;
                default:
                    flog::error("not supported sample format for WAV: {}", (int)_type);
                    return false;
            }
    
            // Open file
            if (!rw.open(path, WAVE_FILE_TYPE)) { 
                flog::error("open() failed");
                return false; 
            }
    
            // Write format chunk
            rw.beginChunk(FORMAT_MARKER);
            rw.write((uint8_t*)&hdr, sizeof(FormatHeader));
            rw.endChunk();
    
            // Begin data chunk
            rw.beginChunk(DATA_MARKER);
            return true;
        }
        return false;
    }

    bool Writer::isOpenInt() {
        if (_format == FORMAT_MP3) return _lame != nullptr;
        if (_format == FORMAT_FLAC) return _flacEncoder != nullptr;
        if (_format == FORMAT_WAV) return rw.isOpen();
        return false;
    }

    bool Writer::isOpen() {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        return isOpenInt();
    }

    void Writer::close() {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        
        if (_lame && _mp3file) {
            int bytes = lame_encode_flush(_lame, _mp3Buffer.data(), _mp3Buffer.size());
            if (bytes > 0) fwrite(_mp3Buffer.data(), 1, bytes, _mp3file);
            lame_close(_lame);
            _lame = nullptr;
            if (fclose(_mp3file) != 0) {
                flog::error("fclose() failed: {}", strerror(errno));
            }
            _mp3file = nullptr;
        }
        if (_flacEncoder) {
            FLAC__stream_encoder_finish(_flacEncoder);
            FLAC__stream_encoder_delete(_flacEncoder);
            _flacEncoder = nullptr;
            return;
        }
        if (rw.isOpen()) {
            rw.endChunk();  // Finish data chunk
            rw.close();     // Close the file
            // Free buffers
            if (bufU8) {
                dsp::buffer::free(bufU8);
                bufU8 = NULL;
            }
            if (bufI16) {
                dsp::buffer::free(bufI16);
                bufI16 = NULL;
            }
            if (bufI24) {
                dsp::buffer::free(bufI24);
                bufI24 = NULL;
            }
            if (bufI32) {
                dsp::buffer::free(bufI32);
                bufI32 = NULL;
            }
            return;
        }
    }

    void Writer::setChannels(int channels) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (this->isOpenInt()) { throw runtime_error("Cannot change parameters while file is open"); }

        // Validate channel count
        if (channels < 1) { throw runtime_error("Channel count must be greater or equal to 1"); }
        _channels = channels;
    }

    void Writer::setSamplerate(uint64_t samplerate) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (this->isOpenInt()) { throw runtime_error("Cannot change parameters while file is open"); }

        // Validate samplerate
        if (!samplerate) { throw runtime_error("Samplerate must be non-zero"); }
        _samplerate = samplerate;
    }

    void Writer::setFormat(Format format) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (this->isOpenInt()) { throw runtime_error("Cannot change parameters while file is open"); }
        _format = format;
    }

    void Writer::setSampleType(SampleType type) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (this->isOpenInt()) { throw runtime_error("Cannot change parameters while file is open"); }
        _type = type;
    }
    
    std::string Writer::getFileExtension() {
        switch (_format) {
            case FORMAT_MP3:  return ".mp3";
            case FORMAT_FLAC: return ".flac";
            case FORMAT_WAV:  return ".wav";
            default: return ".wav";
        }
    }

    void Writer::write(float* samples, int count) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        
        if (_format == FORMAT_MP3 && _lame && _mp3file) {
            std::vector<int16_t> pcmSamples(count * _channels);
            int sampelCount = count * _channels;
            for (int i = 0; i < sampelCount; i++) {
                pcmSamples[i] = (int16_t)lroundf(std::clamp(samples[i], -1.0f, 1.0f) * (32768.0f - 0.5f) - 0.5f);
            }
            int bytes = 0;
            if (_channels == 1) {
                bytes = lame_encode_buffer(_lame, pcmSamples.data(), nullptr, count, _mp3Buffer.data(), _mp3Buffer.size());
            } else {
                bytes = lame_encode_buffer_interleaved(_lame, pcmSamples.data(), count, _mp3Buffer.data(), _mp3Buffer.size());
            }            
            if (bytes > 0) fwrite(_mp3Buffer.data(), 1, bytes, _mp3file);
            samplesWritten += count;
            return;
        }        
        if (_format == FORMAT_FLAC && _flacEncoder) {
            std::vector<int32_t> pcmSamples(count * _channels);
            int sampelCount = count * _channels;
            for (int i = 0; i < sampelCount; i++) {
                pcmSamples[i] = (int32_t)lroundf(std::clamp(samples[i], -1.0f, 1.0f) * _halfRangeMH - 0.5d);
            }
            if (!FLAC__stream_encoder_process_interleaved(_flacEncoder, pcmSamples.data(), count)) {
                flog::error("FLAC__stream_encoder_process_interleaved() failed");
            }
            samplesWritten += count;
            return;
        }
        
        if (_format == FORMAT_WAV && rw.isOpen()) {
            // Select different writer function depending on the chose depth
            int sampelCount = count * _channels;
            int bytesCount = count * bytesPerSamp;
            switch (_type) {
                case SAMP_TYPE_UINT8:
                    for (int i = 0; i < sampelCount; i++) {
                        bufU8[i] = (uint8_t)lroundf(std::clamp(samples[i], -1.0f, 1.0f) * (128.0f - 0.5f) - 0.5f + 128);
                    }
                    rw.write(bufU8, bytesCount);
                    break;
                case SAMP_TYPE_INT16:
                    //volk_32f_s32f_convert_16i(bufI16, samples, 0x7fff, tcount);
                    for (int i = 0; i < sampelCount; i++) {
                        bufI16[i] = (int16_t)lroundf(std::clamp(samples[i], -1.0f, 1.0f) * (32768.0f - 0.5f) - 0.5f);
                    }
                    rw.write((uint8_t*)bufI16, bytesCount);
                    break;
                case SAMP_TYPE_INT24:
                    {
                        auto pDst = (uint8_t*)bufI24;
                        for (int i = 0; i < sampelCount; i++) {
                            int32_t i24 = (int32_t)lroundf(std::clamp(samples[i], -1.0f, 1.0f) * (8388608.0f - 0.5f) - 0.5f);
                            pDst[0] = (uint8_t)i24;
                            pDst[1] = (uint8_t)(i24 >> 8);
                            pDst[2] = (uint8_t)(i24 >> 16);
                            pDst += 3;
                        }
                        rw.write((uint8_t*)bufI24, bytesCount);
                    }
                    break;
                case SAMP_TYPE_INT32:
                    //volk_32f_s32f_convert_32i(bufI32, samples, 0x7fffffff, tcount);
                    for (int i = 0; i < sampelCount; i++) {
                        bufI32[i] = (int32_t)lroundf(std::clamp(samples[i], -1.0f, 1.0f) * (2147483648.0d - 0.5d) - 0.5d);
                    }
                    rw.write((uint8_t*)bufI32, bytesCount);
                    break;
                case SAMP_TYPE_FLOAT32:
                    rw.write((uint8_t*)samples, bytesCount);
                    break;
                default:
                    break;
            }
            samplesWritten += count;    // Increment sample counter
        }
    }
}
