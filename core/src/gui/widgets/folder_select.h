#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <stdint.h>
#include <string>
#include <thread>
#include <utils/threading.h>

class FolderSelect {
public:
    FolderSelect(std::string defaultPath);
    bool render(std::string id);
    void setPath(std::string path, bool markChanged = false);
    bool pathIsValid();

    std::string expandString(std::string input);

    std::string path = "";


private:
    void worker();
    threading::thread workerThread;
    std::string root = "";

    bool pathValid = false;
    bool dialogOpen = false;
    char strPath[2048];
    bool pathChanged = false;
};