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
#include <vector>
#include <memory>
#include <exception>
#include <utility>
#include <chrono>
#include "utils/stack_trace.h"

// --- Select implementation ---
// TODO: test
//#if __cplusplus >= 202002L && __has_include(<format>)
//    #include <format>
//    namespace fmt_impl = std;
//    //#pragma message("Using std::format (C++20)")
//#elif __has_include(<fmt/core.h>)
#if __has_include(<fmt/core.h>)
    #include <fmt/core.h>
    namespace fmt_impl = fmt;
    //#pragma message("Using fmtlib")
#else
    #error "No suitable format library available (std::format or fmt::format required)"
#endif



namespace flog {

    enum Type { TYPE_DEBUG, TYPE_INFO, TYPE_WARN, TYPE_ERROR };
    void logImpl(flog::Type type, std::string msg, const std::chrono::time_point<std::chrono::steady_clock> steady_ts, const std::shared_ptr<const stack_trace> trace = nullptr);

    void exception(const std::exception& e);
    void exception();                           // for catch(...)

    // --- format wrapper ---
    template <typename... Args>
    inline std::string formatSafe(fmt_impl::format_string<Args...> fmt_str, Args&&... args) {
        try {
            return fmt_impl::format(fmt_str, std::forward<Args>(args)...);
        } catch (const std::exception& ex) { 
            flog::exception(ex); 
            fmt::string_view sv = fmt_str;
            return "[flog::formatSafe() exception, fmt_str=\"" + std::string(sv.data(), sv.size()) + "\"]";
        } catch (...) { 
            flog::exception(); 
            fmt::string_view sv = fmt_str;
            return "[flog::formatSafe() exception, fmt_str=\"" + std::string(sv.data(), sv.size()) + "\"]";
        }
    }
    
    // --- Public template log functions ---
    template <typename... Args>
    inline void debug(fmt_impl::format_string<Args...> fmt, Args&&... args) {
        auto ts = std::chrono::steady_clock::now();
        auto msg = flog::formatSafe(fmt, std::forward<Args>(args)...);
        logImpl(flog::Type::TYPE_DEBUG, msg, ts);
    }

    template <typename... Args>
    inline void info(fmt_impl::format_string<Args...> fmt, Args&&... args) {
        auto ts = std::chrono::steady_clock::now();
        auto msg = flog::formatSafe(fmt, std::forward<Args>(args)...);
        logImpl(flog::Type::TYPE_INFO, msg, ts);
    }

    template <typename... Args>
    inline void warn(fmt_impl::format_string<Args...> fmt, Args&&... args) {
        auto ts = std::chrono::steady_clock::now();
        auto msg = flog::formatSafe(fmt, std::forward<Args>(args)...);
        logImpl(flog::Type::TYPE_WARN, msg, ts);
    }

    template <typename... Args>
    inline void error(fmt_impl::format_string<Args...> fmt, Args&&... args) {
        auto ts = std::chrono::steady_clock::now();
        auto msg = flog::formatSafe(fmt, std::forward<Args>(args)...);
        logImpl(flog::Type::TYPE_ERROR, msg, ts);
    }
    
    inline void debug(fmt_impl::string_view msg) {
        auto ts = std::chrono::steady_clock::now();
        logImpl(flog::Type::TYPE_DEBUG, std::string(msg.data(), msg.size()), ts);
    }
    inline void info (fmt_impl::string_view msg) {
        auto ts = std::chrono::steady_clock::now();
        logImpl(flog::Type::TYPE_INFO,  std::string(msg.data(), msg.size()), ts);
    }
    inline void warn (fmt_impl::string_view msg) {
        auto ts = std::chrono::steady_clock::now();
        logImpl(flog::Type::TYPE_WARN,  std::string(msg.data(), msg.size()), ts);
    }
    inline void error(fmt_impl::string_view msg) {
        auto ts = std::chrono::steady_clock::now();
        logImpl(flog::Type::TYPE_ERROR, std::string(msg.data(), msg.size()), ts);
    }
} // namespace flog
