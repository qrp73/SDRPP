#include <functional>
#include <map>
#include <fstream>
#include <filesystem>
#include <config.h>
#include <utils/flog.h>
#include <utils/auto_reset_event.h>


//----------------------------------------------------------------------
#if 0
// immediate save
class worker_service {
    using Worker = std::function<void()>;

public:
    static worker_service& getInstance() {
        static worker_service* _instance = new worker_service();
        return *_instance;
    } 

    // add a worker
    void addWorker(void* key, Worker w) {
        std::unique_lock<std::mutex> lock(_mtx);
        _workers[key] = std::move(w);
        // wake up thread to run new workers immediately
        _event.set();
    }

    // remove a worker
    void removeWorker(void* key) {
        //std::unique_lock<std::mutex> lock(_mtx);
        //_workers.erase(key);
        Worker w;
        {
            std::unique_lock<std::mutex> lock(_mtx);
            auto it = _workers.find(key);
            if (it != _workers.end()) {
                w = it->second;   // copy worker
                _workers.erase(it);
            }
            //flog::debug("cfg:saveWorker: {}", _workers.size());
        }
        // call outside lock
        if (w) {
            try {
                w();
            } catch (const std::exception& ex) {
                flog::exception(ex);
            } catch (...) {
                flog::exception();
            }
        }        
    }

    // wake up the service manually
    void wake() {
        _event.set();
    }

private:
    worker_service() = default;
    ~worker_service() = default;
    
    void start() {
        _isRunning = true;
        _serviceThread = threading::thread("cfg:saveWorker", [this]{ threadProc(); });
    }
    void stop() {
        if (!_isRunning.exchange(false)) return;
        _event.set();        
        if (_serviceThread.joinable()) {
            _serviceThread.join();
        }
        //flog::debug("cfg save finally");
        process();
        //flog::debug("worker_service stopped");
    }

    void process() {
        std::unique_lock<std::mutex> lock(_mtx);
        for (auto& kv : _workers) {
            try {
                kv.second(); // call worker
            } catch (const std::exception& ex) { 
                flog::exception(ex); 
            } catch (...) { 
                flog::exception(); 
            }
        }
    }
    void threadProc() {
        while (_isRunning.load()) {
            _event.wait();
            //flog::debug("cfg save");
            process();
        }
    }

private:
    std::mutex              _mtx;
    auto_reset_event        _event;
    std::map<void*, Worker> _workers;
    threading::thread       _serviceThread;
    std::atomic<bool>       _isRunning{false};
    
    friend class worker_service_starter;
};
#else
// deferred save
class worker_service {
    using Worker = std::function<void()>;

public:
    static worker_service& getInstance() {
        static worker_service* _instance = new worker_service();
        return *_instance;
    } 

    void addWorker(void* key, Worker w) {
        std::unique_lock<std::mutex> lock(_mtx);
        _workers[key] = std::move(w);
        _lastWake = std::chrono::steady_clock::now();
    }

    void removeWorker(void* key) {
        Worker w;
        {
            std::unique_lock<std::mutex> lock(_mtx);
            auto it = _workers.find(key);
            if (it != _workers.end()) {
                w = it->second;
                _workers.erase(it);
            }
            //flog::debug("cfg:saveWorker: {}", _workers.size());
        }
        if (w) {
            try { w(); }
            catch (const std::exception& ex) { flog::exception(ex); }
            catch (...) { flog::exception(); }
        }
    }

    void wake() {
        std::unique_lock<std::mutex> lock(_mtx);
        _lastWake = std::chrono::steady_clock::now();
    }

private:
    worker_service() = default;
    ~worker_service() = default;
    
    void start() {
        _isRunning = true;
        _serviceThread = threading::thread("cfg:saveWorker", [this]{ threadProc(); });
    }
    void stop() {
        if (!_isRunning.exchange(false)) return;
        if (_serviceThread.joinable()) {
            _serviceThread.join();
        }
        process();
        //flog::debug("worker_service stopped");
    }

    void process() {
        std::unique_lock<std::mutex> lock(_mtx);
        for (auto& kv : _workers) {
            try { kv.second(); }
            catch (const std::exception& ex) { flog::exception(ex); }
            catch (...) { flog::exception(); }
        }
    }

    void threadProc() {
        while (_isRunning.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            auto now = std::chrono::steady_clock::now();
            bool shouldRun = false;
            {
                std::unique_lock<std::mutex> lock(_mtx);
                if (now - _lastWake >= std::chrono::seconds(1)) { // process when no request for more than 1 sec
                    _lastWake = now;
                    shouldRun = true;
                }
            }
            if (shouldRun) {
                process();
            }
        }
    }

private:
    std::mutex              _mtx;
    std::map<void*, Worker> _workers;
    threading::thread       _serviceThread;
    std::atomic<bool>       _isRunning{false};
    std::chrono::steady_clock::time_point _lastWake{std::chrono::steady_clock::now()};
    
    friend class worker_service_starter;
};
#endif

class worker_service_starter {
public:
    worker_service_starter()  { worker_service::getInstance().start(); }
    ~worker_service_starter() { worker_service::getInstance().stop(); }
};
static worker_service_starter g_workerServiceStarter;



//----------------------------------------------------------------------
ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
    disableAutoSave();
}

void ConfigManager::setPath(std::string file) {
    path = std::filesystem::absolute(file).string();
}

void ConfigManager::load(nlohmann::json def, bool lock) {
    if (lock) { mtx.lock(); }
    if (path == "") {
        flog::error("Config manager tried to load file with no path specified");
        return;
    }
    if (!std::filesystem::exists(path)) {
        flog::warn("Config file '{0}' does not exist, creating it", path);
        conf = def;
        save(false);
    }
    if (!std::filesystem::is_regular_file(path)) {
        flog::error("Config file '{0}' isn't a file", path);
        return;
    }

    try {
        std::ifstream file(path.c_str());
        file >> conf;
        file.close();
    }
    catch (const std::exception& e) {
        flog::error("Config file '{}' is corrupted, resetting it: {}", path, e.what());
        conf = def;
        save(false);
    }
    if (lock) { mtx.unlock(); }
}

inline void writeJson(std::string path, nlohmann::json conf) {
    //std::ofstream file(path.c_str());
    //file << conf.dump(4);
    //file.close();

    // Read existing JSON
    nlohmann::json oldConf;
    {
        std::ifstream in(path.c_str());
        if (in) {
            try {
                in >> oldConf;
            } catch (...) {
                flog::exception();
            }
        }
    }
    // Compare
    if (oldConf == conf) {
        //flog::debug("skip {}", path);
        return; // no changes, skip writing
    }
    //flog::debug("save {}", path);
    // Write new JSON
    std::ofstream out(path.c_str());
    if (out) {
        out << conf.dump(4);
    } else {
        flog::warn("writeJson() failed: {}", path);
    }
}

void ConfigManager::save(bool lock) {
    if (lock) {
        std::lock_guard<std::mutex> lock(mtx);
        writeJson(path, conf);
    } else {
        writeJson(path, conf);
    }
}

void ConfigManager::enableAutoSave() {
    if (autoSaveEnabled.exchange(true)) return;
    worker_service::getInstance().addWorker(this, [this] {
        if (changed.exchange(false)) {
            //flog::debug("test {}", path);
            std::lock_guard<std::mutex> lock(mtx);
            save(false);
        }
    });
}

void ConfigManager::disableAutoSave() {
    if (!autoSaveEnabled.exchange(false)) return;
    worker_service::getInstance().removeWorker(this);
}

void ConfigManager::acquire() {
    mtx.lock();
}

void ConfigManager::release(bool modified) {
    if (modified) {
        changed.store(true);
        worker_service::getInstance().wake();
    }
    mtx.unlock();
}
