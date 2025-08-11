#pragma once
#include <string>
#include <stdint.h>
#include <inttypes.h>   // PRIu64


namespace utils {
    inline std::string formatFreq(int64_t freq) {
        char str[128];
        const char* unit = " Hz";
        uint64_t scale = 1;
        uint64_t absFreq = freq < 0 ? -freq:freq;
        const char* sign = (freq < 0) ? "-" : "";
        /*if (absFreq >= 1000000000) {
            unit = " GHz";
            scale = 1000000000;
        } else*/ if (absFreq >= 1000000) {
            unit = " MHz";
            scale = 1000000;
        } else if (absFreq >= 1000) {
            unit = " kHz";
            scale = 1000;
        }
        // Trim trailing zeros from fractional part
        uint64_t intPart = absFreq / scale;
        uint64_t fracPart = absFreq % scale;
        
        if (scale == 1 || fracPart == 0) {
            snprintf(str, sizeof(str), "%s%" PRIu64 "%s", sign, intPart, unit);
        } else {
            // Remove trailing zeros and compute fracWidth
            uint64_t s = scale;
            while (fracPart % 10 == 0) {
                fracPart /= 10;
                s /= 10;
            }
            int fracWidth = 0;
            while (s > 1) {
                s /= 10;
                fracWidth++;
            }
            snprintf(str, sizeof(str), "%s%" PRIu64 ".%0*" PRIu64 "%s", sign, intPart, fracWidth, fracPart, unit);
        }    
        return std::string(str);
    }
}
