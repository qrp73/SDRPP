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
#include "threading.h"

#if defined(__linux__)
    #include <sys/prctl.h>
    #include <unistd.h>
    #include <sys/syscall.h>
    #include <pthread.h>
#elif defined(__APPLE__)
    #include <pthread.h>
#elif defined(_WIN32)
    #include <windows.h>
#endif

#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>


namespace threading {

    void sleep(int32_t millisecondsTimeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(millisecondsTimeout));
    }
    
    uint32_t getThreadHash() noexcept {
        static std::atomic<uint32_t> counter{0};
        static thread_local uint32_t _threadHash = counter.fetch_add(1, std::memory_order_relaxed);
        return _threadHash;
    }
    
    void thread::onStarting(const std::string& name) {
        threading::getThreadHash();
        (void)name; // suppress unused parameter warning
    }
    void thread::onStarted(const std::string& name) {
        threading::getThreadHash();
        threading::setThreadName(name);
        //flog::debug("thread {:x} {} started", threading::getThreadHash(), name);
    }
    void thread::onFinished(const std::string& name) {
        //flog::debug("thread {:x} {} finished", threading::getThreadHash(), name);
        (void)name; // suppress unused parameter warning
    }
    

//#if defined(__linux__)
//    uint32_t getThreadId() noexcept {
//        static thread_local uint32_t _threadId = static_cast<uint32_t>(gettid());
//        //static thread_local uint32_t _threadId = static_cast<uint32_t>(syscall(SYS_gettid));
//        return _threadId;
//    }    
//#elif defined(_WIN32)
//    uint32_t getThreadId() noexcept {
//        static thread_local uint32_t _threadId = static_cast<uint32_t>(GetCurrentThreadId());
//        return _threadId;
//    }
//#else
//    uint32_t getThreadId() noexcept {
//        return getThreadHash();
//    }
//#endif



//----------------------------------------------------------------------
#if 0//defined(__linux__)

    void setThreadName(const std::string &name) {
        std::string truncatedName = name.substr(0, 15); // max 16 including '\0'
        prctl(PR_SET_NAME, truncatedName.c_str(), 0,0,0);
        //pid_t const tid(gettid());
        //std::ofstream comm("/proc/" + std::to_string(tid) + "/comm");
        //comm << truncatedName;
    }
    std::string getThreadName() {
        char thread_name_buffer[0x100] = { 0 };
        prctl(PR_GET_NAME,thread_name_buffer,0,0,0);
        return std::string(thread_name_buffer);
        //pid_t tid = syscall(SYS_gettid); // get thread ID
        //std::ifstream comm("/proc/" + std::to_string(tid) + "/comm");
        //std::string name;
        //if (comm)
        //    std::getline(comm, name); // read line, removes \n
        //return name;        
    }
    
//----------------------------------------------------------------------
#elif defined(__linux__) || defined(__APPLE__)

    void setThreadName(const std::string &name) {
        std::string truncatedName = name.substr(0, 15); // max 16 including '\0'
        pthread_setname_np(pthread_self(), truncatedName.c_str());
        //pthread_setname_np(truncatedName.c_str());
    }
    std::string getThreadName() {
        char name[256];
        int result = pthread_getname_np(pthread_self(), name, sizeof(name));
        if (result != 0) {
            flog::error("pthread_getname_np() failed: {}", result);
            return "??";
        }
        return std::string(name);
    }
    
//----------------------------------------------------------------------
#elif defined(_WIN32)

    // internal fallback storage for thread name (Win < 10)
#if defined(_MSC_VER) && _MSC_VER < 1900  // VS < 2015
    static __declspec(thread) std::string t_threadName; // storage per thread
#else
    static thread_local std::string t_threadName;       // modern compilers storage per thread
#endif    

    // exception code for RaiseException hack
    const DWORD MS_VC_EXCEPTION = 0x406D1388;
    // structure for RaiseException hack
    #pragma pack(push,8)
    typedef struct tagTHREADNAME_INFO {
        DWORD dwType;     // must be 0x1000
        LPCSTR szName;    // thread name
        DWORD dwThreadID; // thread id (-1 = current thread)
        DWORD dwFlags;    // reserved = 0
    } THREADNAME_INFO;
    #pragma pack(pop)    

#if (_WIN32_WINNT >= 0x0600) // Vista+
    const DWORD WCTMB_FLAGS = WC_ERR_INVALID_CHARS;
    const DWORD MBTWC_FLAGS = MB_ERR_INVALID_CHARS;
#else
    const DWORD WCTMB_FLAGS = 0;
    const DWORD MBTWC_FLAGS = MB_ERR_INVALID_CHARS;
#endif
    
    // UTF-16 -> UTF-8, return true if success
    static bool utf16_to_utf8(const std::wstring &w, std::string &out) {
        out.clear();
        if (w.empty()) return true; // empty input -> empty output
        int len = WideCharToMultiByte(CP_UTF8, WCTMB_FLAGS, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len == 0) { // error: invalid UTF-16 sequence or other problem
            flog::error("WideCharToMultiByte() failed: {}", GetLastError());
            return false;
        }
        out.assign(len - 1, '\0'); // -1: skip terminating zero
        int written = WideCharToMultiByte(CP_UTF8, WCTMB_FLAGS, w.c_str(), -1, &out[0], len - 1, nullptr, nullptr);
        if (written == 0) {
            flog::error("WideCharToMultiByte() failed: {}", GetLastError());
            out.clear();
            return false;
        }
        return true;
    }
    // UTF-8 -> UTF-16, return true if success
    static bool utf8_to_utf16(const std::string &s, std::wstring &out) {
        out.clear();
        if (s.empty()) return true; // empty input -> empty output
        int len = MultiByteToWideChar(CP_UTF8, MBTWC_FLAGS, s.c_str(), -1, nullptr, 0);
        if (len == 0) { // error: invalid UTF-8 or other problem
            flog::error("MultiByteToWideChar() failed: {}", GetLastError());
            return false;
        }
        out.assign(len - 1, L'\0'); // -1 to skip terminating zero
        int written = MultiByteToWideChar(CP_UTF8, MBTWC_FLAGS, s.c_str(), -1, &out[0], len - 1);
        if (written == 0) {
            flog::error("MultiByteToWideChar() failed: {}", GetLastError());
            out.clear();
            return false;
        }
        return true;
    }

    void setThreadName(const std::string &name) {
        t_threadName = name;

        // TODO: test
        // try Windows 10+ API SetThreadDescription
        HMODULE hKernel = GetModuleHandleW(L"Kernel32.dll");
        if (hKernel) {
            using SetThreadDescription_t = HRESULT(WINAPI*)(HANDLE, PCWSTR);
            auto pSetThreadDescription = reinterpret_cast<SetThreadDescription_t>(GetProcAddress(hKernel, "SetThreadDescription"));
            if (pSetThreadDescription) {
                std::wstring wname;
                if (utf8_to_utf16(name, wname)) {
                    HRESULT hr = pSetThreadDescription(GetCurrentThread(), wname.c_str());
                    if (FAILED(hr)) {
                        flog::error("SetThreadDescription() failed: {}", hr);
                    } else {
                        return;
                    }
                }
            }
        }

        // fallback: old hack via RaiseException (visible in debugger only)
        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = name.c_str();
        info.dwThreadID = static_cast<DWORD>(-1);
        info.dwFlags = 0;
        __try {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    std::string getThreadName() {
        // TODO: test
        // try Windows 10+ API GetThreadDescription
        HMODULE hKernel = GetModuleHandleW(L"Kernel32.dll");
        if (hKernel) {
            using GetThreadDescription_t = HRESULT(WINAPI*)(HANDLE, PWSTR*);
            auto pGetThreadDescription = reinterpret_cast<GetThreadDescription_t>(GetProcAddress(hKernel, "GetThreadDescription"));
            if (pGetThreadDescription) {
                PWSTR wname = nullptr;
                HRESULT hr = pGetThreadDescription(GetCurrentThread(), &wname);
                if (FAILED(hr)) {
                    flog::error("GetThreadDescription() failed: {}", hr);
                }
                if (SUCCEEDED(hr) && wname) {
                    std::wstring wnameStr(wname);
                    std::string name;
                    bool success = utf16_to_utf8(wnameStr, name);
                    LocalFree(wname);
                    if (success) return name;
                }
            }
        }
        return t_threadName;
    }
    
//----------------------------------------------------------------------
#else //#elif defined(__CYGWIN__)

    static thread_local std::string t_threadName;   // internal fallback storage

    void setThreadName(const std::string &name) {
        t_threadName = name;
    }
    std::string getThreadName() {
        return t_threadName;
    }
#endif
}
