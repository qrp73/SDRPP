#pragma once
#include <atomic>
#include <json.hpp>
#include <thread>
#include <string>
#include <mutex>
#include <condition_variable>
#include <utils/threading.h>

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    // disable copy
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    // allow move
    ConfigManager(ConfigManager&&) = default;
    ConfigManager& operator=(ConfigManager&&) = default;    
    
    void setPath(std::string file);
    void load(nlohmann::json def, bool lock = true);
    void save(bool lock = true);
    void enableAutoSave();
    void disableAutoSave();
    void acquire();
    void release(bool modified = false);

    nlohmann::json conf;

private:
    void autoSaveWorker();

    std::string path = "";
    std::atomic<bool> changed{false};
    std::atomic<bool> autoSaveEnabled{false};
    threading::thread autoSaveThread;
    std::mutex mtx;

    std::mutex termMtx;
    std::condition_variable termCond;
    volatile bool termFlag = false;
};
