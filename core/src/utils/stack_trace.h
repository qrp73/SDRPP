/* 
 * This file is part of the SDRPP distribution (https://github.com/qrp73/SDRPP).
 * Copyright (c) 2025 qrp73.
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
#include <string>
#include <stdexcept>



    class stack_trace {
    private:
        std::vector<void*> _trace;
        static std::vector<void*> capture();
    public:
        stack_trace() : _trace(capture()) {}
        std::string toString() const;
        
        static std::string demangleName(const char* mangled);
    };
    
    
    class runtime_error_with_stack : public std::runtime_error {
    private:
        stack_trace _trace;
    public:
        explicit runtime_error_with_stack(const std::string& msg)
            : std::runtime_error(msg), _trace() {}
    
        const stack_trace& getStackTrace() const {
            return _trace;
        }
    };
