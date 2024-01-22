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
#include <cmath>
#include <vector>

class fft_scaler {
    size_t   _outSize;
    double   _offset;
    double   _factor;

public:
    fft_scaler(double viewOffset, double viewBandwidth, double wholeBandwidth, size_t fftSize, size_t outSize) {
        _outSize = outSize;
        const double offsetRatio = viewOffset / (wholeBandwidth / 2.0);
        double width = (viewBandwidth / wholeBandwidth) * fftSize;
        _offset = (((double)fftSize / 2.0) * (offsetRatio + 1)) - (width / 2);
        if (_offset < 0) {
            _offset = 0;
        }
        if (width > fftSize-_offset) {
            width = fftSize-_offset;
        }
        _factor = width / outSize;
    }

    inline void doZoom(const float* data, float* out) {
        auto f0 = _offset;
        if(_factor <= 1.0) {
            for (auto i = 0; i < _outSize; i++) {
                auto f1 = f0 + _factor;
                auto i0 = (int)roundf(f0);
                *out++ = data[i0];
                f0 = f1;
            }
        } else {
            auto i0 = (int)roundf(f0);
            for (auto i = 0; i < _outSize; i++) {
                auto f1 = f0 + _factor;
                auto i1 = (int)roundf(f1);
                auto maxVal = data[i0];
                for (auto j=i0+1; j < i1; j++) {
                    maxVal = std::max(maxVal, data[j]);
                }
                *out++ = maxVal;
                f0 = f1;
                i0 = i1;
            }
        }
    }
};
