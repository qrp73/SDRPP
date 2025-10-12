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
#define IMGUI_DEFINE_MATH_OPERATORS
#include <fstream>
#include <filesystem>
#include <random>
#include <json.hpp>
#include <core.h>
#include <utils/flog.h>

#include "geomap.h"
#include "simple_widgets.h"
#include "earcut.hpp"

// earcut adapters
namespace mapbox {
    namespace util {
        template <std::size_t I> struct nth<I, ImVec2> {
            static auto get(const ImVec2& t) {
                if constexpr (I == 0) return t.x;
                else return t.y;
            }
        };
    }
}



namespace geomap {

    nlohmann::json geoJSON;
    std::vector<Country> countriesGeo;
    CountryPolygon       terminatorPolygon;
    std::tm              terminatorPolygonTime;

    
    
    nlohmann::json readGeoJSONFile(const std::string& filePath) {
        std::ifstream fileStream(filePath);

        if (!fileStream.is_open()) {
            flog::error("Failed to open the file {}", filePath);
            return nullptr;
        }

        nlohmann::json geoJSON;
        fileStream >> geoJSON;

        return geoJSON;
    }
    
    std::tm getTimeUtc() {
        std::time_t now = std::time(nullptr);  // current time in seconds since epoch
        std::tm utc{};
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&utc, &now);  // thread-safe version on Windows
#else
        gmtime_r(&now, &utc);  // thread-safe version on POSIX
#endif
        return utc;
    }
/*    std::tm getTime(int year, int month, int day, int hour, int minute, int second) {
        std::tm t{};
        t.tm_year = year - 1900;   // years since 1900
        t.tm_mon  = month - 1;     // months 0-11
        t.tm_mday = day;           // day of month 1-31
        t.tm_hour = hour;          // hours 0-23
        t.tm_min  = minute;        // minutes 0-59
        t.tm_sec  = second;        // seconds 0-59
        t.tm_isdst = 0;            // UTC, no DST
        return t;
    }*/
    
    
    
    
    // Simple function to get day of year
    int dayOfYear(int year, int month, int day) {
        static const int monthDays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        int doy = 0;
        for (int m = 0; m < month - 1; ++m) doy += monthDays[m];
        doy += day;
        // Leap year
        if (month > 2 && ((year%4==0 && year%100!=0) || (year%400==0))) doy += 1;
        return doy;
    }
    // Approximate solar declination (radians)
    double solarDeclination(int year, int month, int day) {
        int N = dayOfYear(year, month, day);
        double gamma = 2.0 * pi / 365.0 * (N - 1);
        return 0.006918 - 0.399912*cos(gamma) + 0.070257*sin(gamma)
               - 0.006758*cos(2*gamma) + 0.000907*sin(2*gamma)
               - 0.002697*cos(3*gamma) + 0.00148*sin(3*gamma);
    }    
    // Build Sun terminator polygon for given UTC
    std::vector<GeoCoordinates> buildTerminatorPolygon(const std::tm& utc, bool isDaylight) {
        std::vector<GeoCoordinates> polygon;
        double decl = solarDeclination(utc.tm_year+1900, utc.tm_mon+1, utc.tm_mday);
        
        const int lonStep = 2; // degrees
        double utcHours = utc.tm_hour + utc.tm_min/60.0 + utc.tm_sec/3600.0;
    
        for (int lon = -180; lon <= 180; lon += lonStep) {
            // Local solar hour angle H
            double lst = utcHours + lon/15.0;    // local solar time in hours
            double H = deg2rad((lst - 12.0) * 15.0); // convert to radians
    
            // Avoid poles singularity
            double lat;
            double cosDecl = std::cos(decl);
            if (std::fabs(cosDecl) < 1e-6) {
                lat = (decl > 0) ? 90.0 : -90.0;
            } else {
                lat = rad2deg(std::atan(-std::cos(H)/std::tan(decl)));
            }
    
            // Clamp latitude to [-90, 90]
            if (lat > 90.0) lat = 90.0;
            if (lat < -90.0) lat = -90.0;
    
            polygon.push_back({lat, double(lon)});
        }
    
        // Close polygon along poles for fillable area
        bool northLit = (decl > 0);    // north pole is lighted
        if (!isDaylight) northLit = !northLit;
        auto poleLat = northLit ? 90.0 : -90.0;
        polygon.push_back({ poleLat,  180.0 });
        polygon.push_back({ poleLat, -180.0 });
        polygon.push_back({ polygon.front().latitude, polygon.front().longitude });
        return polygon;
    }
    
    void updateTerminatorPolygon(const std::tm& utc, bool isDaylight) {
        std::vector<GeoCoordinates> geoDaylight = buildTerminatorPolygon(utc, isDaylight);
        CountryPolygon poly;
        for (const auto& coord : geoDaylight) {
            double lon = coord.longitude;
            double lat = coord.latitude;
            poly.border.push_back(geoToCartesian({ lat, lon }));
        }
        // do tesselation (triangulation) of polygon for rendering
        std::vector<std::vector<ImVec2>> polygonVec = { poly.border };
        poly.triangles = mapbox::earcut<uint32_t>(polygonVec);
        // bounding box
        poly.bbRect = ImRect(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
        for (auto& p : poly.border) {
            poly.bbRect.Add(p);
        }
        terminatorPolygon = poly;
        terminatorPolygonTime = utc;
    }

    void checkTerminatorPolygon() {
        const std::tm& utc = getTimeUtc();
        const std::tm& old = terminatorPolygonTime;
        const bool isEqual = utc.tm_year == old.tm_year && utc.tm_mon  == old.tm_mon && utc.tm_mday == old.tm_mday &&
            utc.tm_hour == old.tm_hour && utc.tm_min  == old.tm_min && utc.tm_sec == old.tm_sec;
        if (isEqual) {
            return;
        }
        updateTerminatorPolygon(utc, false);
    }

    void maybeInit() {
        if (!geoJSON.empty())
            return;
            
        const std::tm& utc = getTimeUtc();
        terminatorPolygonTime = utc;
        updateTerminatorPolygon(utc, false);
    
        std::string resDir = core::configManager.conf["resourcesDirectory"];
        const std::string filePath = resDir + "/cty/map.json";
        geoJSON = readGeoJSONFile(filePath);

        int countryNumber = 0;    
        for (const auto& feature : geoJSON["features"]) {
            const auto& props = feature["properties"];    

            Country country;

            // Get country name
            if (props.contains("name")) {
                country.name = props["name"].get<std::string>();
            }
            // Get country color index
            if (props.contains("mapcolor13")) {
                country.colorIndex = props["mapcolor13"].get<int>();
                if (country.colorIndex < 0) country.colorIndex = -country.colorIndex;
            } else {
                country.colorIndex = 0;//countryNumber % 13;
            }            
    
            const auto& geometry = feature["geometry"];
            const auto& type = geometry["type"].get<std::string>();

            if (type == "Polygon") {
                for (const auto& ring : geometry["coordinates"]) {
                    CountryPolygon poly;
                    for (const auto& coord : ring) {
                        double lon = coord[0].get<double>();
                        double lat = coord[1].get<double>();
                        poly.border.push_back(geoToCartesian({ lat, lon }));
                    }
                    // do tesselation (triangulation) of polygon for rendering
                    std::vector<std::vector<ImVec2>> polygonVec = { poly.border };
                    poly.triangles = mapbox::earcut<uint32_t>(polygonVec);
                    // bounding box
                    poly.bbRect = ImRect(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
                    for (auto& p : poly.border) {
                        poly.bbRect.Add(p);
                    }
                    country.polygons.push_back(std::move(poly));
                }
            }
            else if (type == "MultiPolygon") {
                for (const auto& polygon : geometry["coordinates"]) {
                    for (const auto& ring : polygon) {
                        CountryPolygon poly;
                        for (const auto& coord : ring) {
                            double lon = coord[0].get<double>();
                            double lat = coord[1].get<double>();
                            poly.border.push_back(geoToCartesian({ lat, lon }));
                        }
                        // do tesselation (triangulation) of polygon for rendering
                        std::vector<std::vector<ImVec2>> polygonVec = { poly.border };
                        poly.triangles = mapbox::earcut<uint32_t>(polygonVec);
                        // bounding box
                        poly.bbRect = ImRect(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
                        for (auto& p : poly.border) {
                            poly.bbRect.Add(p);
                        }
                        country.polygons.push_back(std::move(poly));
                    }
                }
            }            
            countriesGeo.push_back(std::move(country));
            countryNumber++;
        }
    }

    /*
    std::vector<ImVec4> mapcolor7 = {
        ImVec4(0.85f, 0.37f, 0.37f, 1.0f),  // 0 - red
        ImVec4(0.37f, 0.85f, 0.37f, 1.0f),  // 1 - green
        ImVec4(0.37f, 0.37f, 0.85f, 1.0f),  // 2 - blue
        ImVec4(0.85f, 0.85f, 0.37f, 1.0f),  // 3 - yellow
        ImVec4(0.85f, 0.37f, 0.85f, 1.0f),  // 4 - magenta
        ImVec4(0.37f, 0.85f, 0.85f, 1.0f),  // 5 - cyan
        ImVec4(0.85f, 0.62f, 0.37f, 1.0f)   // 6 - orange
    };
    std::vector<ImVec4> mapcolor8 = {
        ImVec4(0.85f, 0.37f, 0.37f, 1.0f),  // 0 - red
        ImVec4(0.37f, 0.85f, 0.37f, 1.0f),  // 1 - green
        ImVec4(0.37f, 0.37f, 0.85f, 1.0f),  // 2 - blue
        ImVec4(0.85f, 0.85f, 0.37f, 1.0f),  // 3 - yellow
        ImVec4(0.85f, 0.37f, 0.85f, 1.0f),  // 4 - magenta
        ImVec4(0.37f, 0.85f, 0.85f, 1.0f),  // 5 - cyan
        ImVec4(0.85f, 0.62f, 0.37f, 1.0f),  // 6 - orange
        ImVec4(0.62f, 0.85f, 0.37f, 1.0f)   // 7 - light green
    };
    std::vector<ImVec4> mapcolor9 = {
        ImVec4(0.85f, 0.37f, 0.37f, 1.0f),  // 0 - red
        ImVec4(0.37f, 0.85f, 0.37f, 1.0f),  // 1 - green
        ImVec4(0.37f, 0.37f, 0.85f, 1.0f),  // 2 - blue
        ImVec4(0.85f, 0.85f, 0.37f, 1.0f),  // 3 - yellow
        ImVec4(0.85f, 0.37f, 0.85f, 1.0f),  // 4 - magenta
        ImVec4(0.37f, 0.85f, 0.85f, 1.0f),  // 5 - cyan
        ImVec4(0.85f, 0.62f, 0.37f, 1.0f),  // 6 - orange
        ImVec4(0.62f, 0.85f, 0.37f, 1.0f),  // 7 - light green
        ImVec4(0.85f, 0.85f, 0.85f, 1.0f)   // 8 - light grey
    };
    */
    std::vector<ImVec4> mapcolor13 = {
        ImVec4(0.85f, 0.37f, 0.37f, 1.0f),  // 0 - red
        ImVec4(0.37f, 0.85f, 0.37f, 1.0f),  // 1 - green
        ImVec4(0.37f, 0.37f, 0.85f, 1.0f),  // 2 - blue
        ImVec4(0.85f, 0.85f, 0.37f, 1.0f),  // 3 - yellow
        ImVec4(0.85f, 0.37f, 0.85f, 1.0f),  // 4 - magenta
        ImVec4(0.37f, 0.85f, 0.85f, 1.0f),  // 5 - cyan
        ImVec4(0.85f, 0.62f, 0.37f, 1.0f),  // 6 - orange
        ImVec4(0.62f, 0.85f, 0.37f, 1.0f),  // 7 - light green
        ImVec4(0.37f, 0.62f, 0.85f, 1.0f),  // 8 - light blue
        ImVec4(0.85f, 0.37f, 0.62f, 1.0f),  // 9 - pink
        ImVec4(0.62f, 0.37f, 0.85f, 1.0f),  // 10 - purple
        ImVec4(0.37f, 0.85f, 0.62f, 1.0f),  // 11 - mint
        ImVec4(0.85f, 0.85f, 0.85f, 1.0f)   // 12 - light grey
    };

    ImVec2 GeoMap::map2wnd(ImVec2 pos) {
        pos = (pos + translate) * scale;
        return ImVec2(
            wndHalfSize.x + pos.x * wndHalfSize.x,
            wndHalfSize.y - pos.y * wndHalfSize.y
        );
    }
    ImVec2 GeoMap::wnd2map(ImVec2 pos) {
        ImVec2 p;
        p.x = (pos.x - wndHalfSize.x) / wndHalfSize.x;
        p.y = (wndHalfSize.y - pos.y) / wndHalfSize.y;
        return p / scale - translate;
    }

    // Simple point-in-polygon check (Ray Casting)
    static bool pointInPolygon(const ImVec2& p, const std::vector<ImVec2>& poly) {
        bool inside = false;
        size_t n = poly.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            bool intersect = ((poly[i].y > p.y) != (poly[j].y > p.y)) &&
                             (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / 
                             (poly[j].y - poly[i].y) + poly[i].x);
            if (intersect) inside = !inside;
        }
        return inside;
    }
    // Check point inside triangulated polygon using barycentric coords
    static bool pointInPolygonTriangulated(
        const ImVec2& p,
        const std::vector<ImVec2>& verts,
        const std::vector<uint32_t>& triangles)
    {
        for (size_t i = 0; i < triangles.size(); i += 3) {
            const ImVec2& a = verts[triangles[i]];
            const ImVec2& b = verts[triangles[i + 1]];
            const ImVec2& c = verts[triangles[i + 2]];
    
            ImVec2 v0 = c - a;
            ImVec2 v1 = b - a;
            ImVec2 v2 = p - a;
    
            float invDenom = 1.0f / (ImDot(v0, v0) * ImDot(v1, v1) - ImDot(v0, v1) * ImDot(v0, v1));
            float u = (ImDot(v1, v1) * ImDot(v0, v2) - ImDot(v0, v1) * ImDot(v1, v2)) * invDenom;
            float v = (ImDot(v0, v0) * ImDot(v1, v2) - ImDot(v0, v1) * ImDot(v0, v2)) * invDenom;
    
            if (u >= 0.0f && v >= 0.0f && (u + v) <= 1.0f)
                return true;
        }
        return false;
    }
    void GeoMap::drawPolygon(ImDrawList* drawList, const ImVec2& offset, const CountryPolygon& poly, const ImU32& fillColor, const ImU32& lineColor, float lineWidth) {
        // fill polygon
        auto oldFlags = drawList->Flags;
        drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (size_t i = 0; i < poly.triangles.size(); i += 3) {
            drawList->AddTriangleFilled(
                offset + map2wnd(poly.border[poly.triangles[i]]),
                offset + map2wnd(poly.border[poly.triangles[i + 1]]),
                offset + map2wnd(poly.border[poly.triangles[i + 2]]),
                fillColor);
        }
        drawList->Flags = oldFlags;

        // draw polygon outline
        for (size_t i=0; i < poly.border.size()-1; ++i) {
            drawList->AddLine(
                offset + map2wnd(poly.border[i]), 
                offset + map2wnd(poly.border[i + 1]), 
                lineColor, lineWidth);
        }
    }


    void GeoMap::draw() {
        maybeInit();
        checkTerminatorPolygon();

        wndPos = ImGui::GetCursorScreenPos() - ImGui::GetWindowContentRegionMin();
        wndSize = ImGui::GetContentRegionAvail() + ImGui::GetWindowContentRegionMin() * 2;
        wndHalfSize = wndSize / 2;
        if (wndSize.x == 0 || wndSize.y == 0) {
            return;
        }
        const ImVec2 curpos = ImGui::GetCursorPos();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // fill water
        drawList->AddRectFilled(wndPos, wndPos + wndSize, ImColor(10, 30, 60));

        uint32_t count = 0;
        ImVec2 mouseWndPos = ImGui::GetMousePos() - wndPos;
        ImVec2 mouseMapPos = wnd2map(mouseWndPos);
        bool isNotHoveredItem = !ImGui::IsAnyItemHovered();
        std::string hoveredCountry;        
        bool first = true;
        for (auto& country : countriesGeo) {
            const auto& mapcolor = mapcolor13;
            const auto& fillColor = ImColor(mapcolor[country.colorIndex % mapcolor.size()]);
            const auto& lineColor = ImColor(fillColor.Value.x * 0.5f, fillColor.Value.y * 0.5f, fillColor.Value.z * 0.5f, 0.5f);//fillColor.Value.w);
            //const auto& fillColor2 = ImColor(fillColor.Value.x * 1.1f, fillColor.Value.y * 1.1f, fillColor.Value.z * 1.1f, fillColor.Value.w);

            for (auto& poly : country.polygons) {
                auto polyColor = fillColor;
                // Check if mouse is inside polygon
                if (isNotHoveredItem && 
                    poly.bbRect.Contains(mouseMapPos) && 
                    pointInPolygonTriangulated(mouseMapPos, poly.border, poly.triangles)) 
                {
                    hoveredCountry = country.name;
                    //polyColor = fillColor2;
                }
                drawPolygon(drawList, wndPos, poly, polyColor, lineColor, 2);
            }
        }
        /*
        static const std::vector<ImVec2>& userPos = {
            geoToCartesian({ 34.56, 34.56 }),
        };
        for (const auto& mapPos : userPos) {
            const auto& pos = wndPos + map2wnd(mapPos);
            drawList->AddTriangleFilled(
                pos + ImVec2(0, -2),
                pos + ImVec2(2, 2),
                pos + ImVec2(-2, 2),
                ImColor(1.0f, 0.0f, 0.0f, 1.0f));
        }
        */ 
        // draw Sun terminator
        const auto& fillNightColor = ImColor(0.0f, 0.0f, 0.0f, 0.4f);
        const auto& lineNightColor = ImColor(fillNightColor.Value.x * 0.5f, fillNightColor.Value.y * 0.5f, fillNightColor.Value.z * 0.5f, 0.5f);//fillColor.Value.w);
        drawPolygon(drawList, wndPos, terminatorPolygon, fillNightColor, lineNightColor, 2);
        
        
        // Show tooltip if hovering a country
        if (!hoveredCountry.empty() && ImGui::IsWindowHovered()) {
            auto tooltip = hoveredCountry + "\n" + geo2qth(cartesianToGeo(mouseMapPos));
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip.c_str());
            ImGui::EndTooltip();
        }
            
        // disable window move on drag map
        ImGuiID drag_id = ImGui::GetCurrentWindow()->GetID("drag_map");
        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
            ImGui::SetActiveID(drag_id, ImGui::GetCurrentWindow());
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
        }
        if (ImGui::IsMouseReleased(0) && ImGui::GetActiveID() == drag_id) {
            ImGui::ClearActiveID();
        }

        //ImVec2 mousePos = ImGui::GetMousePos() - ImGui::GetWindowPos();
        //mouseWndPos = ImGui::GetMousePos() - ImGui::GetWindowPos();
        // drag map
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            if (!dragActive) {
                dragActive = true;
                dragStartPos = mouseWndPos;
                dragStartTranslate = translate;
            }
            ImVec2 delta = wnd2map(mouseWndPos) - wnd2map(dragStartPos);
            translate = dragStartTranslate + delta;
            scaleTranslateDirty = true;
        } else {
            dragActive = false;
        }
    

        // Zoom with mouse wheel
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                ImVec2 mapBefore = wnd2map(mouseWndPos);
                float zoomStep = 1.2f;
                float zoomFactor = (wheel > 0.0f) ? zoomStep : (1.0f / zoomStep);
                scale *= zoomFactor;
                ImVec2 mapAfter = wnd2map(mouseWndPos);
                translate += mapAfter - mapBefore;
                scaleTranslateDirty = true;
            }
        }        

#if 0
        // Create tooltip
        //ImGui::SetNextWindowPos(mousePos + ImVec2(16, 16));
        ImVec2 mapPos = wnd2map(mouseWndPos);
        ImGui::BeginTooltip();
        ImGui::Text("mousPos: %f, %f", mousePos.x, mousePos.y);
        ImGui::Text("wndPos:  %f, %f", wndPos.x, wndPos.y);
        ImGui::Text("wndSize: %f, %f", wndSize.x, wndSize.y);
        ImGui::Text("mapPos:  %.4f, %.4f", mapPos.x, mapPos.y);
        ImGui::EndTooltip();
#endif        
        
        ImGui::SetCursorPos(curpos);
        if (doFingerButton("Zoom In##geomap-zoom-in")) {
            scale = scale * 2;
            scaleTranslateDirty = true;
        }
        ImGui::SameLine();
        if (doFingerButton("Zoom Out##geomap-zoom-out")) {
            scale = scale / 2;
            scaleTranslateDirty = true;
        }
        ImGui::SameLine();
        if (doFingerButton("Reset Map##reset-map")) {
            scale = ImVec2(1.0, 1.0);
            translate = ImVec2(0.0, 0.0);
            scaleTranslateDirty = true;
        }
    }

    void GeoMap::saveTo(ConfigManager& manager, const char* prefix){
        auto pref = std::string(prefix);
        manager.acquire();
        manager.conf[pref+"_scale_x"] = scale.x;
        manager.conf[pref+"_scale_y"] = scale.y;
        manager.conf[pref+"_translate_x"] = translate.x;
        manager.conf[pref+"_translate_y"] = translate.y;
        manager.release(true);
    };

    void GeoMap::loadFrom(ConfigManager& manager, const char* prefix) {
        auto pref = std::string(prefix);
        manager.acquire();
        if (manager.conf.contains(pref+"_scale_x")) {
            scale.x = manager.conf[pref + "_scale_x"];
            scale.y = manager.conf[pref + "_scale_y"];
            translate.x = manager.conf[pref + "_translate_x"];
            translate.y = manager.conf[pref + "_translate_y"];
        }
        manager.release(false);
    };
};
