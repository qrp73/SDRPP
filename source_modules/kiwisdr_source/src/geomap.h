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
#ifdef _WIN32
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#endif
#include <imgui/imgui_internal.h>

#include <json.hpp>
#include <imgui/imgui.h>
#include <stdint.h>
#include "config.h"

using nlohmann::json;

namespace geomap {

    constexpr double pi = 3.14159265358979323846;
    constexpr double earthRadius = 6371.0; // Earth radius in kilometers

    // Converts degrees to radians
    inline double deg2rad(double deg) {
        return deg * pi / 180.0;
    }
    inline double rad2deg(double rad) {
        return rad * 180.0 / pi;
    }

    struct GeoCoordinates {
        double latitude;
        double longitude;
        
        std::string toString() const {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Lat: %.6f, Lon: %.6f", latitude, longitude);
            return std::string(buf);
        }        
    };

    struct CountryPolygon {
        std::vector<ImVec2>   border;       // original polygon
        std::vector<uint32_t> triangles;    // indices into `border`
        ImRect                bbRect;       // bounding box
    };
    
    struct Country {
        std::string                 name;
        int                         colorIndex;
        std::vector<CountryPolygon> polygons;
    };

#if 1
    // Equirectangular projection (plate carrée)
    inline ImVec2 geoToCartesian(const GeoCoordinates& geo) {
        double latRad = deg2rad(geo.latitude);
        double lngRad = deg2rad(geo.longitude);
        double x = lngRad / pi;
        double y = latRad / (pi / 2.0);
        return ImVec2(x, y);
    }
    inline GeoCoordinates cartesianToGeo(const ImVec2& v) {
        GeoCoordinates geo;
        geo.longitude = v.x * 180.0;      // x = lonRad / pi → lon = x * pi → degrees = x * 180
        geo.latitude  = v.y * 90.0;       // y = latRad / (pi/2) → lat = y * (pi/2) → degrees = y * 90
        return geo;
    }
#else
    // Mercator projection
    inline ImVec2 geoToCartesian(const GeoCoordinates& geo) {
        double lat = geo.latitude;
        if (lat > 85.0) lat = 85.0;     // clamp latitude to avoid infinity at poles
        if (lat < -85.0) lat = -85.0;
        double latRad = deg2rad(lat);
        double lngRad = deg2rad(geo.longitude);
        double x = lngRad / pi;
        double y = log(tan(pi/4 + latRad/2)) / pi; // Mercator y
        return ImVec2(x, y);
    }
#endif

    inline std::string geo2qth(const GeoCoordinates& geo) {
        // Add offsets
        double lon = geo.longitude + 180.0;  // 0..360
        double lat = geo.latitude + 90.0;    // 0..180
    
        // Field letters (A-R)
        int lonField = (int)(lon / 20);
        int latField = (int)(lat / 10);
    
        // Square digits (0-9)
        int lonSquare = (int)((lon - lonField*20) / 2);
        int latSquare = (int)(lat - latField*10);
    
        // Subsquare letters (A-X)
        int lonSub = (int)((lon - lonField*20 - lonSquare*2) * 12);
        int latSub = (int)((lat - latField*10 - latSquare*1) * 24);
    
        char buf[7];
        std::sprintf(buf, "%c%c%d%d%c%c",
                     'A' + lonField,
                     'A' + latField,
                     lonSquare,
                     latSquare,
                     'A' + lonSub,
                     'A' + latSub);

        return std::string(buf);
    }


    struct GeoMap {

        ImVec2 scale = ImVec2(1.0, 1.0);
        ImVec2 translate = ImVec2(0.0, 0.0); // in map coordinates
        bool scaleTranslateDirty = false;

        bool   dragActive = false;
        ImVec2 dragStartPos;
        ImVec2 dragStartTranslate;

        ImVec2 wndPos;
        ImVec2 wndSize;
        ImVec2 wndHalfSize;
        
        ImVec2 map2wnd(ImVec2 pos);
        ImVec2 wnd2map(ImVec2 pos);

        void draw();
        void drawPolygon(ImDrawList* drawList, const ImVec2& offset, const CountryPolygon& poly, const ImU32& fillColor, const ImU32& lineColor, float lineWidth);
        void saveTo(ConfigManager &manager, const char* string);
        void loadFrom(ConfigManager& manager, const char* prefix);
    };

};
