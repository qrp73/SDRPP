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

#include <imgui.h>
#include <string>
#include <gui/style.h>

inline bool getFingerButtonHeight() {
    //return style::baseFont->FontSize * 3;
    return style::baseFont->LegacySize * 3;
}

inline bool doFingerButton(const std::string &title) {
    const ImVec2& labelWidth = ImGui::CalcTextSize(title.c_str(), nullptr, true, -1);
    if (title[0] == '>') {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
    }
    //auto rv = ImGui::Button(title.c_str(), ImVec2(labelWidth.x + style::baseFont->FontSize, style::baseFont->FontSize * 3));
    auto rv = ImGui::Button(title.c_str(), ImVec2(labelWidth.x + style::baseFont->LegacySize, style::baseFont->LegacySize * 3));
    if (title[0] == '>') {
        ImGui::PopStyleColor();
    }
    return rv;
};

inline void doRightText(const std::string &title) {
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize((title+"W").c_str()).x, 0));
    ImGui::SameLine();
    ImGui::TextUnformatted(title.c_str());
}
