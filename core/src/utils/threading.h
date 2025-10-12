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
#include <thread>
#include <functional>
#include <stdint.h>
#include <utils/flog.h>


namespace threading {

    void sleep(int32_t millisecondsTimeout);
    
    uint32_t getThreadHash() noexcept;

    void setThreadName(const std::string &name);
    std::string getThreadName();
    

    class thread {
    private:
        std::thread _t;
        
        static void onStarting(const std::string& name);
        static void onStarted(const std::string& name);
        static void onFinished(const std::string& name);
    
    public:
        // move only
        thread(thread&&) = default;
        thread& operator=(thread&&) = default;

        thread() = default;
        
        template<typename F, typename... Args>
        thread(const std::string& name, F&& f, Args&&... args) {
            // wrap user function
            auto wrapped = [name, f=std::forward<F>(f)](auto&&... innerArgs) mutable {
                onStarted(name);
                try {
                    std::invoke(f, std::forward<decltype(innerArgs)>(innerArgs)...);
                } catch (const std::exception& ex) {
                    flog::exception(ex);
                } catch (...) {
                    flog::exception();
                }
                onFinished(name);
            };    
            onStarting(name);
            _t = std::thread(wrapped, std::forward<Args>(args)...);
        }
    
        // join
        bool joinable() const { return _t.joinable(); }
        void join() { _t.join(); }
    
        // detach
        void detach() { _t.detach(); }
    
        // get native thread
        std::thread::native_handle_type native_handle() { return _t.native_handle(); }
    };    
}
