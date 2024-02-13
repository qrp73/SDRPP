/* 
 * This file is part of the SDRPP distribution (https://github.com/qrp73/SDRPP).
 * Copyright (c) 2024 qrp73.
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
#include "rectangular.h"
#include "hann.h"
#include "hamming.h"
#include "blackman.h"
#include "nuttall.h"
#include "blackman_harris4.h"
#include "blackman_harris7.h"


namespace dsp::window {
    enum windowType {
        RECTANGULAR,
        HAMMING,
        HANN,
        BLACKMAN,
        NUTTALL,
        BLACKMAN_HARRIS4,
        BLACKMAN_HARRIS7,
    };

    inline void createWindow(windowType type, float* buffer, int size, bool isCentered) {
        switch ( type ) {
            case windowType::RECTANGULAR:      for (auto i=0; i < size; i++) buffer[i] = rectangular(i, size);     break;
            case windowType::HANN:             for (auto i=0; i < size; i++) buffer[i] = hann(i, size);            break;
            case windowType::HAMMING:          for (auto i=0; i < size; i++) buffer[i] = hamming(i, size);         break;
            case windowType::BLACKMAN:         for (auto i=0; i < size; i++) buffer[i] = blackman(i, size);        break;
            case windowType::NUTTALL:          for (auto i=0; i < size; i++) buffer[i] = nuttall(i, size);         break;
            case windowType::BLACKMAN_HARRIS4: for (auto i=0; i < size; i++) buffer[i] = blackmanHarris4(i, size); break;
            case windowType::BLACKMAN_HARRIS7: for (auto i=0; i < size; i++) buffer[i] = blackmanHarris7(i, size); break;
        }
        // normalization
        double wscale = 0.0f;
        for (auto i=0; i < size; i++) { 
            wscale += buffer[i];
        }
        wscale = 1.0 / wscale;
        if (!isCentered) {
            for (auto i = 0; i < size; i++) {
                buffer[i]   *= wscale;
            }
        } else {
            for (auto i = 0; i < size; i+=2) {
                buffer[i]   *= -wscale;
                buffer[i+1] *=  wscale;
            }
        }
    }
}