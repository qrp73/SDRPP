#pragma once
#include "../processor.h"
#include "../math/fast_atan2.h"
#include "../math/hz_to_rads.h"
#include "../math/normalize_phase.h"

#define USE_QUAD_FM_DEMOD 1

namespace dsp::demod {
    class Quadrature : public Processor<complex_t, float> {
        using base_type = Processor<complex_t, float>;
    public:
        Quadrature() {}

        Quadrature(stream<complex_t>* in, double deviation) { init(in, deviation); }

        Quadrature(stream<complex_t>* in, double deviation, double samplerate) { init(in, deviation, samplerate); }

        
        virtual void init(stream<complex_t>* in, double deviation) {
            _invDeviation = 1.0 / deviation;
            base_type::init(in);
        }

        virtual void init(stream<complex_t>* in, double deviation, double samplerate) {
            init(in, math::hzToRads(deviation, samplerate));
        }

        void setDeviation(double deviation) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _invDeviation = 1.0 / deviation;
        }

        void setDeviation(double deviation, double samplerate) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            _invDeviation = 1.0 / math::hzToRads(deviation, samplerate);
        }

        inline int process(int count, complex_t* in, float* out) {
            for (int i = 0; i < count; i++) {
#if !defined(USE_QUAD_FM_DEMOD) || (USE_QUAD_FM_DEMOD == 0)
                // Differential Phase FM Demodulation
                float cphase = in[i].phase();
                out[i] = math::normalizePhase(cphase - phase) * _invDeviation;
                phase = cphase;
#else                
                // Quadrature FM Demodulation
                auto y = in[i];
                out[i] = (y * _din.conj()).phase() * _invDeviation;
                _din = y;
#endif                
            }
            return count;
        }

        void reset() {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
#if !defined(USE_QUAD_FM_DEMOD) || (USE_QUAD_FM_DEMOD == 0)
            phase = 0.0f;
#else
            _din.re = 0.0;
            _din.im = 0.0;
#endif
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }

            process(count, base_type::_in->readBuf, base_type::out.writeBuf);

            base_type::_in->flush();
            if (!base_type::out.swap(count)) { return -1; }
            return count;
        }

    protected:
        float _invDeviation;
#if !defined(USE_QUAD_FM_DEMOD) || (USE_QUAD_FM_DEMOD == 0)
        float phase = 0.0f;
#else
       complex_t _din;
#endif
    };
}
