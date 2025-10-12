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
#include <vector>
#include "utils/stack_trace.h"
#include "utils/flog.h"

// TODO: test android
#if defined(__linux__)
    #include <dlfcn.h>    // dladdr, dladdr1
    #include <execinfo.h>
    #include <cxxabi.h>
    #include <link.h>
    #include <cstdio>
#elif defined(__APPLE__) || (defined(__ANDROID__) && __ANDROID_API__ >= 21)
    #include <dlfcn.h>    // dladdr, dladdr1
    #include <execinfo.h>
    #include <cxxabi.h>
#elif defined(__CYGWIN__)
    //#include <dlfcn.h>    // dladdr, dladdr1
    #include <cxxabi.h>
#endif

/*
#if defined(__CYGWIN__)
    #warning __CYGWIN__
#endif
#if defined(__linux__)
    #warning __linux__
#endif
#if defined(__APPLE__)
    #warning __APPLE__
#endif
#if defined(__ANDROID__)
    #warning __ANDROID__
#endif
#if defined(_WIN32)
    #warning _WIN32
#endif
#if defined(_WIN64)
    #warning _WIN64
#endif
*/




    // TODO: test android
    // Helper to demangle a symbol name
    std::string stack_trace::demangleName(const char* mangled) {
        if (!mangled) return "";
#if defined(__linux__) || defined(__CYGWIN__) || defined(__APPLE__) || (defined(__ANDROID__) && __ANDROID_API__ >= 21) 
        int status = 0;
        char* realname = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
        std::string result = (status == 0 && realname != nullptr) ? realname : mangled;
        if (realname) free(realname);
        return result;
#else
        return mangled;
#endif
    }



    // https://stackoverflow.com/questions/3899870/how-to-print-a-stack-trace-whenever-a-certain-function-is-called
    std::vector<void*> stack_trace::capture() {
#if defined(__linux__) || defined(__APPLE__) || (defined(__ANDROID__) && __ANDROID_API__ >= 21)
        std::vector<void*> trace(1024);
        int size = backtrace(trace.data(), trace.size());
        trace.resize(size);
        /*if (!trace.empty()) {
            trace.erase(trace.begin());
        }
        if (!trace.empty()) {
            trace.erase(trace.begin());
        }*/
        return trace;
#else
        std::vector<void*> trace(0);
        return trace;
#endif
    }

    std::string stack_trace::toString() const {
        std::string result;
#if defined(__linux__) || defined(__APPLE__) || (defined(__ANDROID__) && __ANDROID_API__ >= 21)
        std::vector<std::string> lines;
        char** pSymbols = backtrace_symbols(_trace.data(), _trace.size());
        lines.reserve(_trace.size());
        for (size_t i=0; i < _trace.size(); i++) {
            lines.emplace_back(pSymbols[i]);
        }            
        free(pSymbols); pSymbols = nullptr;
#endif
        
#if defined(__linux__)
        for (size_t i=0; i < _trace.size(); i++) {
            Dl_info info;
            link_map* map = nullptr;
            if(dladdr1(_trace[i], &info, (void**)&map, RTLD_DL_LINKMAP)==0){
                result += flog::formatSafe("  {}\n", lines[i]);
                continue;
            }
            size_t vma = (size_t)_trace[i] - map->l_addr;
            if(vma > 0) vma -= 1;
            //std::string sname = info.dli_sname ? std::string(info.dli_sname) : "";
            std::string fname = info.dli_fname ? std::string(info.dli_fname) : "";
            
            std::string cmd = flog::formatSafe("addr2line -f -C -e \"{}\" {:x}", fname, vma);
            FILE* fp = popen(cmd.c_str(),"r");
            if(fp) { 
                char func[256]={0}, loc[256]={0};
                bool success = fgets(func,sizeof(func),fp) && fgets(loc,sizeof(loc),fp);
                pclose(fp);
                if(success) {
                    func[strcspn(func,"\r\n")]=0;
                    loc[strcspn(loc,"\r\n")]=0;
                    if(strcmp(loc,"??:?")==0) result += flog::formatSafe("  {}\n", func);
                    else result += flog::formatSafe("  {} at\033[32m {} \033[0m\n", func, loc);
                    continue;
                }
            }
            result += flog::formatSafe("  {}\n", lines[i]);
        }
        result = "\033[0m" + result;
#elif defined(__linux__) || defined(__APPLE__) || (defined(__ANDROID__) && __ANDROID_API__ >= 21)
        for (int i=0; i < lines.size(); i++) {
            std::string& line = lines[i];
            // Parse...
#if defined(__linux__)                
            // linux: "/lib/aarch64-linux-gnu/libc.so.6(__libc_start_main+0x98) [0x7f821d7818]"
            size_t posParen = line.find('(');
            size_t posPlus  = line.find('+', posParen);
            size_t posParenEnd = line.find(')', posPlus);
            if (posParen != std::string::npos &&
                posPlus  != std::string::npos &&
                posParenEnd != std::string::npos &&
                posParen < posPlus)
            {
                std::string module  = line.substr(0, posParen);
                std::string mangled = line.substr(posParen + 1, posPlus - (posParen + 1));
                std::string offset  = line.substr(posPlus + 1, posParenEnd - (posPlus + 1));
                std::string demangled = stack_trace::demangleName(mangled.c_str());
                if (demangled.empty()) demangled = "?";
                result += flog::formatSafe("  {} + {} at\033[32m {} \033[0m\n", demangled, offset, module);
                continue;
            }
#elif defined(__APPLE__)
            // macos: "0   tester                              0x000000010a439b17 _ZN4flog13getStackTraceEv + 551"
            size_t posModule = line.find_first_not_of(' ', line.find(' '));
            size_t posModuleEnd = line.find(' ', posModule);
            size_t posAddr = line.find_first_not_of(' ', posModuleEnd);
            size_t posAddrEnd = line.find(' ', posAddr);
            size_t posMangled = line.find_first_not_of(' ', posAddrEnd);
            size_t posMangledEnd = line.find(' ', posMangled);
            size_t posPlus = line.find('+', posMangledEnd);
            size_t posOffset = line.find_first_not_of(" +", posPlus);
            if (posModule != std::string::npos &&
                posAddr   != std::string::npos &&
                posMangled != std::string::npos &&
                posPlus   != std::string::npos &&
                posOffset != std::string::npos)
            {
                std::string module   = line.substr(posModule, posModuleEnd - posModule);
                std::string addr     = line.substr(posAddr, posAddrEnd - posAddr);
                std::string mangled  = line.substr(posMangled, posMangledEnd - posMangled);
                std::string offset   = line.substr(posOffset);
                std::string demangled = stack_trace::demangleName(mangled.c_str());
                if (demangled.empty()) demangled = "?";
                result += flog::formatSafe("  {} + {} at\033[32m {} \033[0m({})\n", demangled, offset, module, addr);
                continue;
            }
#endif
            result += flog::formatSafe("  {}\n", line);
        }
        result = "\033[0m" + result;
#else
            // TODO: WIN32
#endif
        return result;
    }

