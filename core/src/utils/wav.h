#pragma once
#include <string>
#include <fstream>
#include <stdint.h>
#include <mutex>
#include "riff.h"
#include <FLAC/stream_encoder.h>
#include <lame/lame.h>


namespace wav {    
    #pragma pack(push, 1)
    struct FormatHeader {
        uint16_t codec;
        uint16_t channelCount;
        uint32_t sampleRate;
        uint32_t bytesPerSecond;
        uint16_t bytesPerSample;
        uint16_t bitDepth;
    };
    #pragma pack(pop)

    enum Format {
        FORMAT_WAV,
        FORMAT_FLAC,
        FORMAT_MP3,
    };

    enum SampleType {
        SAMP_TYPE_UINT8,
        SAMP_TYPE_INT16,
        SAMP_TYPE_INT24,
        SAMP_TYPE_INT32,
        SAMP_TYPE_FLOAT32
    };

    enum Codec {
        CODEC_PCM   = 1,
        CODEC_FLOAT = 3
    };

    class Writer {
    public:
        Writer(int channels = 2, uint64_t samplerate = 48000, Format format = FORMAT_WAV, SampleType type = SAMP_TYPE_INT16);
        ~Writer();

        bool open(std::string path);
        bool isOpen();
        void close();

        void setChannels(int channels);
        void setSamplerate(uint64_t samplerate);
        void setFormat(Format format);
        void setSampleType(SampleType type);
        
        std::string getFileExtension();

        size_t getSamplesWritten() { return samplesWritten; }

        void write(float* samples, int count);

    private:
        std::recursive_mutex mtx;
        // MP3
        lame_t _lame = nullptr;
        std::vector<uint8_t> _mp3Buffer;
        // FLAC
        FLAC__StreamEncoder* _flacEncoder = nullptr;
        //WAV
        FormatHeader hdr;
        riff::Writer rw;

        bool isOpenInt();

        int _channels;
        uint64_t _samplerate;
        Format _format;
        SampleType _type;
        size_t bytesPerSamp;
        double _halfRangeMH;

        uint8_t* bufU8 = NULL;
        int16_t* bufI16 = NULL;
        uint8_t* bufI24 = NULL;
        int32_t* bufI32 = NULL;
        size_t samplesWritten = 0;
    };
}
