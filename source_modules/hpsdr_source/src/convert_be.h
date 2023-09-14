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
#include <stdint.h>


inline void setUInt16_BE(uint8_t* buffer, uint16_t value) {
    buffer[0] = (uint8_t)(value >> 8);
    buffer[1] = (uint8_t)(value);
}

inline void setUInt32_BE(uint8_t* buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)(value);
}

inline uint16_t getUInt16_BE(uint8_t* buffer) {
    return (uint16_t)((buffer[0] << 8) + buffer[1]);
}

inline uint32_t getUInt32_BE(uint8_t* buffer) {
    return (uint32_t)(
        (buffer[0] << 24) + 
        (buffer[1] << 16) + 
        (buffer[2] << 8) + 
        buffer[3]);
}

inline int32_t getInt24_BE(uint8_t* buffer) {
    int32_t v = (buffer[0] << 16) | 
        (buffer[1] << 8) | 
        buffer[2];
    // Sign extend
    return (v << 8) >> 8;
}

