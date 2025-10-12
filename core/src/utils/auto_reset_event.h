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
#include <atomic>

//----------------------------------------------------------------------
#if defined(__linux__) || defined(__ANDROID__)
    #include <linux/futex.h>
    #include <sys/syscall.h>
    #include <unistd.h>        // syscall

class auto_reset_event {
private:
    std::atomic<int> _flag{0}; // 0 = not set, 1 = set

    static void futex_wait(std::atomic<int>* addr, int val) {
        int rc = syscall(SYS_futex, reinterpret_cast<int*>(addr), FUTEX_WAIT, val, nullptr, nullptr, 0);
        if (rc >= 0 || errno==EAGAIN || errno == EINTR) {
            // wakeup || notified before sleep || interrupted by signal
            return;
        }
        fprintf(stderr, "futex_wait: rc=%d, errno=%d: %s\n", rc, errno, strerror(errno));
        abort();
    }
    static void futex_wake(std::atomic<int>* addr, int n = 1) {
        int rc = syscall(SYS_futex, reinterpret_cast<int*>(addr), FUTEX_WAKE, n, nullptr, nullptr, 0);
        if (rc >= 0) {
            return;
        }
        fprintf(stderr, "futex_wake: rc=%d, errno=%d: %s\n", rc, errno, strerror(errno));
        abort();
    }

public:
    auto_reset_event() = default;
    ~auto_reset_event() = default;

    // multiple producers, lock-free
    void set() {
        // use acq_rel so producer "sees" any prior consumer reset
        if (_flag.exchange(1, std::memory_order_acq_rel) == 0) {
            // only wake if _flag was zero
            futex_wake(&_flag);
        }
    }

    // single consumer, wait + consume all events
    void wait() {
        while (_flag.load(std::memory_order_acquire) == 0) {
            futex_wait(&_flag, 0);
        }
        // consume all events that arrived before this store
        // make reset visible (use release so subsequent set() can't be lost)
        _flag.store(0, std::memory_order_release);        
    }
};

//----------------------------------------------------------------------
#elif defined(_WIN32)
    #include <windows.h>

#if (_WIN32_WINNT >= 0x0602)
// for Win >= 8 use WaitOnAddress/WakeByAddressSingle
class auto_reset_event {
private:
    std::atomic<int> _flag{0}; // 0 = not set, 1 = set
public:
    auto_reset_event() = default;
    ~auto_reset_event() = default;

    // multiple producers, lock-free
    void set() {
        // use acq_rel so producer "sees" any prior consumer reset
        if (_flag.exchange(1, std::memory_order_acq_rel) == 0) {
            // only wake if _flag was zero
            WakeByAddressSingle(&_flag);
        }
    }

    // single consumer, wait + consume all events
    void wait() {
        while (_flag.load(std::memory_order_acquire) == 0) {
            WaitOnAddress(&_flag, &_flag, sizeof(_flag), INFINITE);
        }
        // consume all events that arrived before this store
        // make reset visible (use release so subsequent set() can't be lost)
        _flag.store(0, std::memory_order_release);        
    }
};
#else
// for Win < 8 fallback to auto-reset event
class auto_reset_event {
private:
    std::atomic<int> _flag{0}; // 0 = not set, 1 = set
    HANDLE           _hEvent = nullptr;
public:
    auto_reset_event() {
        _hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); // auto-reset event
    }
    ~auto_reset_event() {
        if (_hEvent) { 
            CloseHandle(_hEvent);
            _hEvent = nullptr;
        }
    }

    // multiple producers, lock-free
    void set() {
        // use acq_rel so producer "sees" any prior consumer reset
        if (_flag.exchange(1, std::memory_order_acq_rel) == 0) {
            // only wake if _flag was zero
            SetEvent(_hEvent);
        }
    }

    // single consumer, wait + consume all events
    void wait() {
        while (_flag.load(std::memory_order_acquire) == 0) {
            WaitForSingleObject(_hEvent, INFINITE);
        }
        // consume all events that arrived before this store
        // make reset visible (use release so subsequent set() can't be lost)
        _flag.store(0, std::memory_order_release);        
    }
};
#endif

//----------------------------------------------------------------------
#else 
    #warning "futex_event: fallback to mutex"

    #include <mutex>
    #include <condition_variable>

// fallback to mutex
class auto_reset_event {
private:
    std::atomic<int> _flag{0}; // 0 = not set, 1 = set
    std::mutex _mutex;
    std::condition_variable _cond;

public:
    auto_reset_event() = default;
    ~auto_reset_event() = default;

    // multiple producers, not lock-free
    void set() {
        if (_flag.exchange(1, std::memory_order_acq_rel) == 0) {
            // only wake if _flag was zero
            std::lock_guard<std::mutex> lock(_mutex);
            _cond.notify_one();
        }
    }

    // single consumer, wait + consume all events
    void wait() {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this] { return _flag.load(std::memory_order_acquire) != 0; });
        // consume all events that arrived before this store
        // make reset visible (use release so subsequent set() can't be lost)
        _flag.store(0, std::memory_order_release);
    }
};
#endif
