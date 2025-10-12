/* 
 * This file is based on code from project SDRPlusPlusBrown (https://github.com/sannysanoff/SDRPlusPlusBrown)
 * originally licensed under the GNU General Public License, version 3 (GPLv3).
 *
 * Modified and extended by qrp73, 2025.
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

#define IMGUI_DEFINE_MATH_OPERATORS
#include <core.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <gui/style.h>
#include <filesystem>
#include <fstream>
#include "utils/proto/http.h"
#include <utils/threading.h>
#include <utils/freq_formatting.h>

#include "kiwisdr.h"
#include "simple_widgets.h"
#include "geomap.h"


namespace ImGui {
    inline void Text2(const char* fmt, ...) {
        char buf[1024];

        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 text_size = ImGui::CalcTextSize(buf);

        // Draw background
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos,
            ImVec2(pos.x + text_size.x, pos.y + text_size.y),
            IM_COL32(0, 0, 0, 128)
        );

        ImGui::TextUnformatted(buf);
    }
}

template<typename T> 
class AsyncTask {
public:
    // func takes stop flag and argument of type T
    AsyncTask(std::function<void(std::atomic<bool>&, T)> func,
              std::function<void(const std::string&)> onError = nullptr)
        : _func(std::move(func)), _onError(std::move(onError)),
          _running(false), _stopRequested(false), _disposed(false) {}

    ~AsyncTask() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_disposed.exchange(true)) return;
        requestStop();
        join();
    }

    void start(T arg) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_disposed) return;
        if (_running.exchange(true)) return; // already running
        _stopRequested = false;
        try {
            if (_thread.joinable()) _thread.join();
            _thread = std::thread([this, arg]() {
                threading::setThreadName("kiwisdr::AsyncTask");
                struct RunningFlagReset {
                    std::atomic<bool>& running;
                    ~RunningFlagReset() { running.exchange(false); }
                } reset{_running};
                try {
                    _func(_stopRequested, arg);
                } catch (const std::exception& ex) {
                    flog::exception(ex);
                    if (_onError) _onError(ex.what());
                } catch (...) {
                    flog::exception();
                    if (_onError) _onError("unknown exception");
                }
            });
        } catch (const std::exception& ex) {
            flog::exception(ex);
            if (_onError) _onError(ex.what());
        } catch (...) {
            flog::exception();
            if (_onError) _onError("Unknown error");
        }
    }

    void requestStop() {
        _stopRequested = true;
    }

    void join() {
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    bool isRunning() const {
        return _running;
    }

private:
    std::function<void(std::atomic<bool>&, T)> _func;
    std::function<void(const std::string&)> _onError;
    std::atomic<bool> _running;
    std::atomic<bool> _stopRequested;
    std::atomic<bool> _disposed;
    std::recursive_mutex _mutex;
    std::thread _thread;
};



struct KiwiSDRMapSelector {

    geomap::GeoMap          _geoMap;
    std::shared_ptr<json>   _serversList;
    std::string             _serverListError;
    std::string             _serverTestStatus;
    std::string             _serverTestError;
    bool                    _showPopup = false;
    std::string             _root;

    struct ServerEntry {
        ImVec2      gps; // -1 .. 1 etc
        std::string qth;
        std::string name;
        std::string loc;
        std::string url;
        std::string antenna;
        std::string bands;
        std::string sdr_hw;
        std::string sw_version;
        float       maxSnr;
        float       secondSnr;
        int         users, usersmax;
        int64_t     minFreq, maxFreq;
        bool        selected = false;
    };

    std::vector<ServerEntry> servers;
    std::string lastTestedServer;
    std::string lastTestedServerQTH;
    std::string lastTestedServerLoc;
    std::mutex lastTestedServerMutex;
    const std::string configPrefix;

    ConfigManager* config;

    AsyncTask<const ServerEntry&>   taskTest;
    AsyncTask<const int&>           taskLoad;

    KiwiSDRMapSelector(const std::string& root, ConfigManager *config, const std::string& configPrefix) 
        : configPrefix(configPrefix),
        taskTest(
            [this](std::atomic<bool>& stop, const ServerEntry& s) { this->taskTestProc(stop, s); },
            [this](const std::string& err) { this->taskErrorProc(err); }),
        taskLoad(
            [this](std::atomic<bool>& stop, const int& arg) { this->taskLoadProc(stop, arg); },
            [this](const std::string& err) { this->taskErrorProc(err); })
    {
        this->_root = root;
        this->config = config;
        json def = json({});
        config->load(def);
        _geoMap.loadFrom(*config, configPrefix.c_str()); // configPrefix is like "mapselector1_"
    }

    void openPopup() {
        if (!_showPopup) {
            _serversList.reset();
        }
        _showPopup = true;
    }

    void taskErrorProc(const std::string& err) { 
        _serverTestStatus = err.c_str(); 
    }

    void taskLoadProc(std::atomic<bool>& stopRequest, const int&) {
        servers.clear();
        _serversList = downloadServersList();
        if (_serversList) {
            int totallyParsed = 0;
            for (const auto& entry : *_serversList) {
                ServerEntry serverEntry;

                // Check if all required fields are present
                if (entry.contains("gps") && entry.contains("name") && entry.contains("url") &&
                    entry.contains("snr") && entry.contains("users") && entry.contains("users_max") && entry.contains("offline")) {

                    if (entry["offline"].get<std::string>() == "no") {
                        std::string gps_str = entry["gps"].get<std::string>();
                        geomap::GeoCoordinates geo = {0.0, 0.0};

                        std::stringstream ss(gps_str);
                        ss.imbue(std::locale::classic());          // force '.' as decimal separator

                        char discard;
                        ss >> discard          // '('
                           >> geo.latitude
                           >> discard          // ','
                           >> geo.longitude
                           >> discard;         // ')'

                        if (!ss) {
                            flog::warn("Parsing geo coordinates failed: \"{}\"", gps_str);
                        } else {
                            serverEntry.gps = geomap::geoToCartesian(geo);
                            serverEntry.qth = geomap::geo2qth(geo);
                            serverEntry.name = entry["name"].get<std::string>();
                            serverEntry.loc = entry["loc"].get<std::string>();
                            serverEntry.url = entry["url"].get<std::string>();
                            if (entry.contains("sdr_hw")) {
                                serverEntry.sdr_hw = entry["sdr_hw"].get<std::string>();
                            }
                            if (entry.contains("sw_version")) {
                                serverEntry.sw_version = entry["sw_version"].get<std::string>();
                            }
                            if (entry.contains("antenna")) {
                                serverEntry.antenna = entry["antenna"].get<std::string>();
                            }
                            if (entry.contains("bands")) {
                                serverEntry.bands = entry["bands"].get<std::string>();
                            }
                            sscanf(entry["snr"].get<std::string>().c_str(), "%f,%f", &serverEntry.maxSnr, &serverEntry.secondSnr);
                            serverEntry.users = atoi(entry["users"].get<std::string>().c_str());
                            serverEntry.usersmax = atoi(entry["users_max"].get<std::string>().c_str());
                            
                            // bands = "0-30000000"
                            size_t dashPos = serverEntry.bands.find('-');
                            if (dashPos != std::string::npos) {
                                serverEntry.minFreq = std::stoll(serverEntry.bands.substr(0, dashPos));         // extract min
                                serverEntry.maxFreq = std::stoll(serverEntry.bands.substr(dashPos + 1));        // extract max
                            } else {
                                serverEntry.minFreq = 0;
                                serverEntry.maxFreq = 0;
                            }
                            servers.push_back(serverEntry);
                            totallyParsed++;
                        }
                    }
                }
            }
            flog::info("Parsed {} servers",totallyParsed);
        }

        std::sort(servers.begin(), servers.end(), [](const ServerEntry& a, const ServerEntry& b) {
            return a.maxSnr < b.maxSnr;
        });


        //_loadingList = false;
    }

    inline int64_t currentTimeMillis() {
        std::chrono::system_clock::time_point t1 = std::chrono::system_clock::now();
        auto msec = std::chrono::time_point_cast<std::chrono::milliseconds>(t1).time_since_epoch().count();
        return static_cast<int64_t>(msec);
    }

    void taskTestProc(std::atomic<bool>& stopRequest, const ServerEntry& s) {
        KiwiSDRClient testClient;
        bool plannedDisconnect = false;
        if (s.url.find("http://") == 0) {
            auto hostPort = s.url.substr(7);
            auto qth = s.qth;
            auto loc = s.loc;
            auto lastSlash = hostPort.find("/");
            if (lastSlash != std::string::npos) {
                hostPort = hostPort.substr(0, lastSlash);
            }
            _serverTestStatus = "Testing server " + hostPort + "...";
            testClient.init(hostPort);
            bool connected = 0;
            bool disconnected = 0;
            auto start = currentTimeMillis();
            testClient.onConnected = [&]() {
                connected = true;
                _serverTestStatus = "Connected to server " + hostPort + " ...";
                start = currentTimeMillis();
                testClient.tune(14074000, KiwiSDRClient::TUNE_IQ);
            };
            testClient.onDisconnected = [&]() {
                disconnected = true;
                if (plannedDisconnect) {
                    _serverTestStatus = "Got some data. Server OK: " + s.url;
                    lastTestedServerMutex.lock();
                    lastTestedServer = hostPort;
                    lastTestedServerQTH = qth;
                    lastTestedServerLoc = loc;
                    lastTestedServerMutex.unlock();
                }
                else {
                    _serverTestStatus = "Disconnect, no data. Server NOT OK: " + s.url;
                }
            };
            testClient.onError = [&](const std::string& msg) {
                _serverTestStatus = "Connect failed: " + msg;
                disconnected = true;
            };
            testClient.start();
            start = currentTimeMillis();
            while (true) {
                if (disconnected) {
                    break;
                }
                testClient.iqDataLock.lock();
                auto bufsize = testClient.iqData.size();
                testClient.iqDataLock.unlock();
                threading::sleep(100);
                if (bufsize > 0) {
                    plannedDisconnect = true;
                    break;
                }
                if (connected && currentTimeMillis() > start + 5000) {
                    flog::info("taskTestProc(): connected but data timeout");
                    break;
                }
            }
            testClient.stop();
            if (connected) {
                while (!disconnected) {
                    threading::sleep(100);
                }
                flog::info("Disconnected ok");
            }
            else {
                threading::sleep(1000);
            }
        }
        else {
            _serverTestStatus = "Non-http url " + s.url;
        }
        //testInProgress = false;
    }
    
    void drawServerButtons() {
        auto sz = style::baseFont->LegacySize;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        auto mousePos = ImGui::GetMousePos() - _geoMap.wndPos;
        const ServerEntry* hoveredServer = nullptr;
        bool isWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        for (auto& s : servers) {
            auto colorFill = ImColor(0.3f, 0.3f, 0.3f);
            if (s.maxSnr > 22) {
                colorFill = ImColor(0.0f, 1.0f, 0.0f);
            }
            else if (s.maxSnr > 12) {
                colorFill = ImColor(0.6f, 0.6f, 0.6f);
            }
            if (s.maxFreq > 32000000) { // extended freq range
                colorFill = ImColor(0.6f, 0.4f, 1.0f);  // light violet
            }
            if (s.users >= s.usersmax) {
                colorFill = ImColor(0.8f, 0.0f, 0.0f);
            }
            auto colorRect = s.selected ? ImColor(1.0f, 1.0f, 0.0f) : ImColor(0.0f, 0.0f, 0.0f);
            auto dest = _geoMap.map2wnd(s.gps);
            auto rectMin = dest - ImVec2(sz / 2, sz / 2);
            auto rectMax = dest + ImVec2(sz / 2, sz / 2);
            drawList->AddRectFilled(
                _geoMap.wndPos + rectMin,
                _geoMap.wndPos + rectMax,
                colorFill, sz / 4.0f);
            drawList->AddRect(
                _geoMap.wndPos + rectMin,
                _geoMap.wndPos + rectMax,
                colorRect, sz / 4.0f);
            if (isWindowHovered &&
                mousePos.x >= rectMin.x && mousePos.x <= rectMax.x &&
                mousePos.y >= rectMin.y && mousePos.y <= rectMax.y) 
            {
                hoveredServer = &s;
            }
        }
        // server hover
        if (hoveredServer != nullptr) {
            //std::string bandsEx = hoveredServer->bands;
            //int64_t f1 = 0, f2 = 0;
            //if (sscanf(bandsEx.c_str(), "%lld-%lld", &f1, &f2) == 2) {
            //    bandsEx = utils::formatFreq(f1) + " - " + utils::formatFreq(f2);
            //}
            std::string bandsEx = utils::formatFreq(hoveredServer->minFreq) + " - " + utils::formatFreq(hoveredServer->maxFreq);
            ImGui::SetTooltip(
                "%s\n"
                "Bands: %s\n"
                "Setup: %s\n"
                "QTH:   %s\n"
                "URL:   %s\n"
                "Antenna: %s\n"
                "Users:   %d/%d\n"
                "SNR:     %.0f/%.0f dB", 
                hoveredServer->name.c_str(), 
                bandsEx.c_str(), 
                hoveredServer->sw_version.c_str(),
                hoveredServer->qth.c_str(),
                hoveredServer->url.c_str(),
                hoveredServer->antenna.c_str(),
                hoveredServer->users, hoveredServer->usersmax, 
                hoveredServer->maxSnr, hoveredServer->secondSnr);
            // server click
            if (ImGui::IsMouseClicked(0) && ImGui::GetMouseClickedCount(0) == 1) {
                // find index of hoveredServer
                auto it = std::find_if(servers.begin(), servers.end(), [&](const auto& s) { return &s == hoveredServer; });
                if (it != servers.end()) {
                    // deselect all before moving
                    for (auto& s : servers) {
                        s.selected = false;
                    }
                    auto moved = *it;             // make a copy
                    moved.selected = true;        // set selected
                    servers.erase(it);            // erase original
                    servers.emplace_back(moved);  // move to end                
                }
            }
        }
    }

    void drawOverlay() {
        ImGui::Text2("Loaded %d servers", servers.size());
        for (auto& s : servers) {
            if (s.selected) {
                ImGui::Text2("%s", s.name.c_str());
                ImGui::Text2("%s", s.loc.c_str());
                if (!s.bands.empty()) {
                    ImGui::Text2("BND: %s", s.bands.c_str());
                }
                if (!s.antenna.empty()) {
                    ImGui::Text2("ANT: %s", s.antenna.c_str());
                }
                if (s.maxSnr > 0) {
                    ImGui::Text2("SNR: %d", (int)s.maxSnr);
                }
                if (s.usersmax > 0) {
                    ImGui::Text2("USR: %d/%d", s.users, s.usersmax);
                }
                ImGui::Text2("URL: %s", s.url.c_str());
            }
        }
        if (!_serverTestStatus.empty()) {
            ImGui::Text2("%s", _serverTestStatus.c_str());
        }
        if (!_serverTestError.empty()) {
            ImGui::Text2("Server test error: %s", _serverTestError.c_str());
        }
    }

    void drawPopup(std::function<void(const std::string&, const std::string&, const std::string&)> onSelected) {
        if (!_showPopup) return;
        //ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.1f);
        //if (ImGui::BeginPopupModal((configPrefix+": The KiwiSDR Map").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImVec2 ws = ds;// * 0.9f;
        ImVec2 wp = (ds-ws) * 0.5f;
        ImGui::SetNextWindowPos(wp, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ws, ImGuiCond_Appearing);
        if (ImGui::Begin("KiwiSDR Map", &_showPopup, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            
            // Set the modal dialog's width and height
            //const ImVec2 ws = ImGui::GetIO().DisplaySize * 0.75f;
            //ImGui::SetWindowSize(ws);
            
            ImGui::BeginChild("##geomap-kiwisdr", ImGui::GetContentRegionAvail() - ImVec2(0, 100), true, 0);
            _geoMap.draw();
            if (_geoMap.scaleTranslateDirty) {
                _geoMap.saveTo(*config, configPrefix.c_str());
                _geoMap.scaleTranslateDirty = false;
            }
            if (!_serversList) {
                if (!_serverListError.empty()) {
                    ImGui::Text2("%s", _serverListError.c_str());
                }
                else {
                    ImGui::Text2("Loading KiwiSDR servers list..");
                    
                    if (!taskLoad.isRunning()) {
                        taskLoad.start(0);
                    }
                }
            }
            else {
                drawServerButtons();
                drawOverlay();
            }

            ImGui::EndChild();
            // Display some text in the modal dialog
            //            ImGui::Text("This is a modal dialog box with specified width and height.");

            // Close button
            if (doFingerButton("Cancel")) {
                _showPopup = false;
            }
            auto it = std::find_if(servers.begin(), servers.end(), [](auto& s){ return s.selected; });
            if (it != servers.end()) {
                const auto& server = *it;
                
                ImGui::BeginDisabled(taskTest.isRunning());
                ImGui::SameLine();
                auto doTest = doFingerButton("TEST");
                ImGui::EndDisabled();
                if (doTest) {
                    flog::debug("TEST-SERVER");
                    lastTestedServerMutex.lock();
                    lastTestedServer = "";
                    lastTestedServerMutex.unlock();
                    if (!taskTest.isRunning()) {
                        taskTest.start(server);
                    }
                }
            }
            lastTestedServerMutex.lock();
            if (lastTestedServer != "") {
                ImGui::SameLine();
                if (doFingerButton("Use tested server: " + lastTestedServer)) {
                    onSelected(lastTestedServer, lastTestedServerQTH, lastTestedServerLoc);
                    _showPopup = false;
                }
            }
            lastTestedServerMutex.unlock();
        }
        ImGui::End();
    }

    std::shared_ptr<json> downloadServersList() {
        // http://rx.linkfanel.net/kiwisdr_com.js
        try {

            std::string jsoncache = _root + "/kiwisdr_source.receiverlist.json";

            auto status = std::filesystem::status(jsoncache);
            if (exists(status)) {
                const std::filesystem::file_time_type last_write_time = std::filesystem::last_write_time(jsoncache);
                auto last_write_time_sys_clock = std::chrono::time_point_cast<std::chrono::system_clock::duration>(last_write_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());

                if (std::chrono::system_clock::now() - last_write_time_sys_clock < std::chrono::hours(1)) {
                    std::ifstream ifs(jsoncache);
                    std::string content((std::istreambuf_iterator<char>(ifs)),
                                        (std::istreambuf_iterator<char>()));
                    return std::make_shared<json>(json::parse(content));
                }
            }

            std::string host = "rx.linkfanel.net";
            auto controlSock = net::connect(host, 80);
            auto controlHttp = net::http::Client(controlSock);

            // Make request
            net::http::RequestHeader rqhdr(net::http::METHOD_GET, "/kiwisdr_com.js", host);
            controlHttp.sendRequestHeader(rqhdr);
            net::http::ResponseHeader rshdr;
            controlHttp.recvResponseHeader(rshdr, 5000);

            flog::debug("Response from {}: {}", host, rshdr.getStatusString());
            std::vector<uint8_t> data(2000000, 0);
            std::string response;
            while (true) {
                auto len = controlSock->recv(data.data(), data.size());
                if (len < 1) {
                    break;
                }
                response += std::string((char*)data.data(), len);
                threading::sleep(1);
            }
            controlSock->close();
            auto BEGIN = "var kiwisdr_com =";
            auto END = "},\n]\n;";
            auto beginIx = response.find(BEGIN);
            if (beginIx == std::string::npos) {
                throw std::runtime_error("Invalid response from server");
            }
            auto endIx = response.find_last_of(END);
            if (endIx == std::string::npos) {
                throw std::runtime_error("Invalid response from server");
            }
            response = response.substr(beginIx + strlen(BEGIN), endIx - strlen(END) - (beginIx + strlen(BEGIN)));
            response += "}]"; // fix trailing comma unsupported by parser

            FILE* toSave = fopen(jsoncache.c_str(), "wt");
            if (toSave) {
                fwrite(response.c_str(), 1, response.size(), toSave);
                fclose(toSave);
            }

            return std::make_shared<json>(json::parse(response));
        }
        catch (const std::exception& e) {
            flog::exception(e);
            _serverListError = e.what();
            return std::shared_ptr<json>();
        }
        catch (...) {
            flog::exception();
            _serverListError = "unknown exception";
            return std::shared_ptr<json>();
        }
    }
};
