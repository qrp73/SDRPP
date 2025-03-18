#include <volk/volk.h>
#include <stdexcept>
#include <dsp/buffer/buffer.h>
#include <dsp/stream.h>
#include <vector>
#include <map>
#include <algorithm>
#include <utils/flog.h>
#include "wav.h"


namespace wav {
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
        if (channels < 1) { throw std::runtime_error("Channel count must be greater or equal to 1"); }
        if (!samplerate) { throw std::runtime_error("Samplerate must be non-zero"); }
        
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
        if (_flacEncoder || rw.isOpen()) { close(); }

        // Reset work values
        samplesWritten = 0;
        bytesPerSamp = (SAMP_BITS[_type] / 8) * _channels;
        auto bitsPerSample = SAMP_BITS[_type];
        _halfRangeM1 = pow(2, bitsPerSample - 1) - 1.0;
        
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
        if (this->isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }

        // Validate channel count
        if (channels < 1) { throw std::runtime_error("Channel count must be greater or equal to 1"); }
        _channels = channels;
    }

    void Writer::setSamplerate(uint64_t samplerate) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (this->isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }

        // Validate samplerate
        if (!samplerate) { throw std::runtime_error("Samplerate must be non-zero"); }
        _samplerate = samplerate;
    }

    void Writer::setFormat(Format format) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (this->isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }
        _format = format;
    }

    void Writer::setSampleType(SampleType type) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (this->isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }
        _type = type;
    }
    
    std::string Writer::getFileExtension() {
        switch (_format) {
            case FORMAT_FLAC: return ".flac";
            case FORMAT_WAV: return ".wav";
            default: return ".wav";
        }
    }

    // fast rounding double to int
    static inline int64_t roundToLong(double value)
    {
        value += 6755399441055744.0;
        int64_t v = *(int64_t*)&value;
        v <<= 13;
        v >>= 13;
        return v;
    }

    void Writer::write(float* samples, int count) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        
        if (_format == FORMAT_FLAC && _flacEncoder) {
            std::vector<int32_t> pcmSamples(count * _channels);
            for (int i = 0; i < count * _channels; i++) {
                pcmSamples[i] = (int32_t)roundToLong(std::clamp(samples[i], -1.0f, 1.0f) * _halfRangeM1);
            }
            if (!FLAC__stream_encoder_process_interleaved(_flacEncoder, pcmSamples.data(), count)) {
                flog::error("FLAC__stream_encoder_process_interleaved() failed");
            }
            samplesWritten += count;
            return;
        }
        
        if (_format == FORMAT_WAV && rw.isOpen()) {
            // Select different writer function depending on the chose depth
            int tcount = count * _channels;
            int tbytes = count * bytesPerSamp;
            switch (_type) {
                case SAMP_TYPE_UINT8:
                    // Volk doesn't support unsigned ints yet :/
                    for (int i = 0; i < tcount; i++) {
                        bufU8[i] = (samples[i] * 0x7f) + 0x80;
                    }
                    rw.write(bufU8, tbytes);
                    break;
                case SAMP_TYPE_INT16:
                    volk_32f_s32f_convert_16i(bufI16, samples, 0x7fff, tcount);
                    rw.write((uint8_t*)bufI16, tbytes);
                    break;
                case SAMP_TYPE_INT24:
                    {
                        auto pDst = (uint8_t*)bufI24;
                        for (auto i = 0; i < tcount; i++) {
                            int32_t v = (int32_t)(samples[i] * 0x7fffff);
                            pDst[0] = (uint8_t)v;
                            pDst[1] = (uint8_t)(v >> 8);
                            pDst[2] = (uint8_t)(v >> 16);
                            pDst += 3;
                        }
                        rw.write((uint8_t*)bufI24, tbytes);
                    }
                    break;
                case SAMP_TYPE_INT32:
                    volk_32f_s32f_convert_32i(bufI32, samples, 0x7fffffff, tcount);
                    rw.write((uint8_t*)bufI32, tbytes);
                    break;
                case SAMP_TYPE_FLOAT32:
                    rw.write((uint8_t*)samples, tbytes);
                    break;
                default:
                    break;
            }
            samplesWritten += count;    // Increment sample counter
        }
    }
}
