#pragma once
#include "cosine.h"

namespace dsp::window {
    inline double blackmanHarris4(double n, double N) {
        const double coefs[] = { 0.35875, 0.48829, 0.14128, 0.01168 };
        return cosine(n, N, coefs, sizeof(coefs) / sizeof(double));
    }
}
