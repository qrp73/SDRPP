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
#include "flog.h"
#include <iostream>
#include <typeinfo>
#include <exception>
#include <chrono>

#include <mutex>
#include <atomic>

#include <algorithm>  // for std::sort

#include "utils/auto_reset_event.h"
#include "utils/mpsc_queue.h"
#include "utils/threading.h"


#if defined(_WIN32)
    #include <Windows.h>
#endif

#if defined(__ANDROID__)
    #include <android/log.h>
    #ifndef FLOG_ANDROID_TAG
        #define FLOG_ANDROID_TAG    "flog"
    #endif
#endif 


namespace flog {

    void exception(const std::exception& e) {
        auto ts = std::chrono::steady_clock::now();
        if (auto fex = dynamic_cast<const runtime_error_with_stack*>(&e)) {
            logImpl(flog::Type::TYPE_ERROR, stack_trace::demangleName(typeid(e).name()) + ": " + std::string(e.what()), ts, std::make_shared<stack_trace>(fex->getStackTrace()));
        } else {
            logImpl(flog::Type::TYPE_ERROR, stack_trace::demangleName(typeid(e).name()) + ": " + std::string(e.what()), ts, std::make_shared<stack_trace>(stack_trace()));
        }
    }
    
    void exception() {
        auto ts = std::chrono::steady_clock::now();
        try {
            auto eptr = std::current_exception();
            if (eptr) std::rethrow_exception(eptr);
            logImpl(flog::Type::TYPE_ERROR, "Unknown exception (null exception_ptr)", ts, std::make_shared<stack_trace>(stack_trace()));
        } catch (const std::exception& ex) { 
            exception(ex); 
        } catch (...) { 
            logImpl(flog::Type::TYPE_ERROR, "Unknown / non-std exception caught", ts, std::make_shared<stack_trace>(stack_trace()));
        }
    }

    //------------------------------------------------------------------    

    static const char* getLevel(flog::Type type) {
        switch(type) {
            case flog::Type::TYPE_DEBUG: return "DEBUG";
            case flog::Type::TYPE_INFO:  return "INFO ";
            case flog::Type::TYPE_WARN:  return "WARN ";
            case flog::Type::TYPE_ERROR: return "ERROR";
            default:                     return "?????";
        }
    }
#ifdef _WIN32
    static const WORD getColor(flog::Type type = (flog::Type)-1) {
        switch (type) {
            case flog::Type::TYPE_DEBUG: return FOREGROUND_GREEN | FOREGROUND_BLUE;
            case flog::Type::TYPE_INFO:  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;                    // info  - Bright green
            case flog::Type::TYPE_WARN:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;   // warn  - Bright yellow
            case flog::Type::TYPE_ERROR: return FOREGROUND_RED | FOREGROUND_INTENSITY;                      // error - Bright red
            default:                     return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;        // reset color
        }
    }
#else
    static const char* getColor(flog::Type type = (flog::Type)-1) {
        switch (type) {
            case flog::Type::TYPE_DEBUG: return "\x1B[36m";
            case flog::Type::TYPE_INFO:  return "\x1B[32m";
            case flog::Type::TYPE_WARN:  return "\x1B[33m";
            case flog::Type::TYPE_ERROR: return "\x1B[31m";
            default:                     return "\x1B[0m"; // reset color
        }
    }
#endif
#ifdef __ANDROID__
    static const android_LogPriority getPriority(flog::Type type) {
        switch (type) {
            case flog::Type::TYPE_DEBUG: return ANDROID_LOG_DEBUG;
            case flog::Type::TYPE_INFO:  return ANDROID_LOG_INFO;
            case flog::Type::TYPE_WARN:  return ANDROID_LOG_WARN;
            case flog::Type::TYPE_ERROR: return ANDROID_LOG_ERROR;
            default:                     return ANDROID_LOG_ERROR;
        }
    }
#endif

    
    
    struct LogRecord {
        flog::Type                                          type;
        std::string                                         message;
        std::chrono::time_point<std::chrono::steady_clock>  ts;
        std::shared_ptr<const stack_trace>                  trace;
        uint32_t                                            threadId;
    };

    // clock synchronize ref point
    static auto g_steady_base = std::chrono::steady_clock::now();        // steady clock ref point
    static auto g_system_base = std::chrono::system_clock::now();        // system clock ref point

    // signle threaded log write procedure
    static void logRecord(const LogRecord& rec) {
        auto system_ts = g_system_base + (rec.ts - g_steady_base);              // transfer steady clock to system clock domain
        // convert to utc
        std::time_t time_u = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(system_ts));
        
        // thread safe convert utc time_u -> local time time_l
        std::tm     time_l{};
#ifdef _WIN32
        localtime_s(&time_l, &time_u);   // Windows: tm*, const time_t*
#else
        localtime_r(&time_u, &time_l);   // POSIX: const time_t*, tm*
#endif
        std::string msg = rec.message;
        if (rec.trace) {
            msg += "\n";
            msg += rec.trace->toString();
        }

#if 0
        // date and time
        auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(system_ts.time_since_epoch()).count() % 1000;
        //auto time_us = std::chrono::duration_cast<std::chrono::microseconds>(system_ts.time_since_epoch()).count() % 1000000;
        auto txtTime = flog::formatSafe(
            "{0:04d}-{1:02d}-{2:02d}T{3:02d}:{4:02d}:{5:02d}.{6:03d}",
            time_l.tm_year + 1900, time_l.tm_mon + 1, time_l.tm_mday,
            time_l.tm_hour, time_l.tm_min, time_l.tm_sec, time_ms);
#else
        // time only
        auto time_us = std::chrono::duration_cast<std::chrono::microseconds>(system_ts.time_since_epoch()).count() % 1000000;
        std::string frac_str = flog::formatSafe("{:06d}", time_us);
        //auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(system_ts.time_since_epoch()).count()  % 1000000000;
        //std::string frac_str = flog::formatSafe("{:09d}", time_ns);
        //for (int i = 3; i < ns_str.size(); i += 4) {
        //    ns_str.insert(i, "-");    // insert separators every 3 digits
        //}        
        auto txtTime = flog::formatSafe(
            "{0:02d}:{1:02d}:{2:02d}.{3:s}",
            time_l.tm_hour, time_l.tm_min, time_l.tm_sec, frac_str);
#endif

#if defined(_WIN32)
        // Get output handle and return if invalid
        HANDLE conHndl = GetStdHandle(STD_ERROR_HANDLE);
        if (!conHndl || conHndl == INVALID_HANDLE_VALUE) { return; }

        static CONSOLE_SCREEN_BUFFER_INFO console_info { 0 };
        static WORD bg = 0;
        if (console_info.wAttributes == 0) {
            GetConsoleScreenBufferInfo (conHndl, &console_info);
            bg = (console_info.wAttributes & ~7);
        }
        // Print beginning of log line
        SetConsoleTextAttribute(conHndl, bg | getColor());

        // Print format string
        fprintf(outStream, "[%s][", txtTime.c_str());

        // Switch color to the log color, print log type and 
        SetConsoleTextAttribute(conHndl, bg | getColor(rec.type));
        fputs(getLevel(rec.type), outStream);

        // Switch back to default color and print rest of log string
        SetConsoleTextAttribute(conHndl, bg | getColor());
        fprintf(outStream, "] %s\n", msg.c_str());
#elif defined(__ANDROID__)
        // Print format string
        //__android_log_print(ANDROID_LOG_WARN, FLOG_ANDROID_TAG, "%s\n", rec.message.c_str());
        __android_log_print(getPriority(type), FLOG_ANDROID_TAG, "%s[%s][%s%s%s] %s\n",
                getColor(), txtTime.c_str(), getColor(type), getLevel(type), getColor(), msg.c_str());
#else
        if (rec.type == flog::Type::TYPE_DEBUG) {
            msg = "\033[90m" + msg + "\033[0m"; // show debug text with gray color
        }
        // Print format string
        fprintf(stderr, 
            "%s[%s][%s%s%s][%x] %s\n",
            getColor(),
            txtTime.c_str(), 
            getColor(rec.type), getLevel(rec.type), getColor(), 
            rec.threadId,
            msg.c_str());
#endif        
    }

    //------------------------------------------------------------------

    class logger_async {

    private:
        auto_reset_event            _logEvent;
        mpsc_queue<LogRecord>       _logQueue;
        std::mutex                  _logQueueConsumerMutex;
        threading::thread           _logThread;
        std::atomic<bool>           _logThreadRunning{false};
        std::atomic<bool>           _backpressure_active{false};  // flag to block enqueue when queue is overloaded

        static constexpr size_t     HIGH_WATERMARK = 1'000'000;   // backpressure limit threshold
        static constexpr size_t     LOW_WATERMARK  = 1'000;       // backpressure clear threshold

        void startLogThread() {
            if (_logThread.joinable()) {
                flog::warn("logger thread already running");
                return;
            }
            _logThread = threading::thread("flog:logThread", [this] { 
                try {
                    _logThreadRunning = true;
                    while (_logThreadRunning.load()) {
                        _logEvent.wait();
                        logProcess();
                    }
                    logProcess(); // final process before exit
                } catch (const std::exception& ex) { 
                    _logThreadRunning = false;                        
                    flog::exception(ex); 
                } catch (...) { 
                    _logThreadRunning = false;
                    flog::exception(); 
                }
            });
        }
        void stopLogThread() {
            _logThreadRunning = false;
            _logEvent.set();
            if (_logThread.joinable()) {
                _logThread.join();
            }
            logProcess();   // final safeguard processing
            flog::debug("logger stopped");
        }

        void logProcess() {
            // lock to ensure only one thread processes the queue at a time (invariant MPSC) 
            // also ensures that logRecord calls are made from a single thread,
            std::lock_guard<std::mutex> lock(_logQueueConsumerMutex);
            
            // collect all records from the concurrent queue into a local vector
            std::vector<LogRecord> batch;
            LogRecord rec;
            while (_logQueue.try_dequeue(rec)) {
                batch.push_back(std::move(rec));
            }
            if (batch.empty()) {
                return; // queue empty
            }
            // sort records by timestamp to ensure chronological order
            std::sort(batch.begin(), batch.end(), [](const LogRecord& a, const LogRecord& b) {
                return a.ts < b.ts;
            });
            // process records in sorted order
            for (auto& r : batch) {
                logRecord(r);
            }
        }
        
        logger_async() {
            threading::getThreadHash();   // make sure hash for the main thread is allocated early
        }
        ~logger_async() = default;
    public:
        static logger_async& getInstance() {
            static logger_async* _instance = new logger_async();
            return *_instance;
        }

        void logImpl(flog::Type type, std::string msg, const std::chrono::time_point<std::chrono::steady_clock> steady_ts, const std::shared_ptr<const stack_trace> trace) {
            auto threadId = threading::getThreadHash();
            
            // backpressure limit check to avoid memory overflow
            size_t qsize = _logQueue.size();
            if (_backpressure_active.load(std::memory_order_relaxed)) {
                if (qsize <= LOW_WATERMARK) {
                    // queue has dropped below low watermark, allow enqueue again
                    _backpressure_active.store(false, std::memory_order_relaxed);
                    _logQueue.enqueue({ flog::Type::TYPE_WARN, "Logger backpressure cleared", steady_ts, nullptr, threadId });
                } else {
                    // queue still above high watermark, skip this log entry
                    return;
                }
            } else if (qsize >= HIGH_WATERMARK) {
                // queue reached high watermark, activate backpressure
                _backpressure_active.store(true, std::memory_order_relaxed);
                // enqueue a warning message about backpressure
                _logQueue.enqueue({ flog::Type::TYPE_WARN, "Logger backpressure limit reached", steady_ts, nullptr, threadId });
                _logEvent.set();
                return; // block further enqueue
            }
            
            
            _logQueue.enqueue({ type, std::move(msg), steady_ts, trace, threadId });
            _logEvent.set();
            if (!_logThreadRunning.load()) {
                logProcess();
            }        
        }
        friend class logger_starter;
    };
    class logger_starter {
    public:
        logger_starter()  { logger_async::getInstance().startLogThread(); }
        ~logger_starter() { logger_async::getInstance().stopLogThread(); }
    };
    static logger_starter g_loggerStarter;

    void logImpl(flog::Type type, std::string msg, const std::chrono::time_point<std::chrono::steady_clock> steady_ts, const std::shared_ptr<const stack_trace> trace) {
        logger_async::getInstance().logImpl(type, msg, steady_ts, trace);
    }

} // namespace flog
