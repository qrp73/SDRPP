#pragma once
#include "cosine.h"

namespace dsp::window {
    inline double hamming(double n, double N) {
        const double coefs[] = { 0.53836, 0.46164 };
        return cosine(n, N, coefs, sizeof(coefs) / sizeof(double));
    }
}
