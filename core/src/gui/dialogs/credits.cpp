#include <gui/dialogs/credits.h>
#include <imgui.h>
#include <gui/icons.h>
#include <gui/style.h>
#include <config.h>
#include <credits.h>
#include <version.h>

namespace credits {
    ImFont* bigFont;
    ImVec2 imageSize(128.0f, 128.0f);

    void init() {
        imageSize = ImVec2(128.0f * style::uiScale, 128.0f * style::uiScale);
    }

    void show() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImVec2 dispSize = ImGui::GetIO().DisplaySize;
        ImVec2 center = ImVec2(dispSize.x / 2.0f, dispSize.y / 2.0f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::OpenPopup("Credits");
        ImGui::BeginPopupModal("Credits", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);

        ImGui::Columns(2, "HeaderColumns", true);
        ImGui::PushFont(style::hugeFont);
        ImGui::TextUnformatted("SDRPP");
        ImGui::PopFont();
        //ImGui::SameLine();
        //ImGui::Image(icons::LOGO, imageSize);
        ImGui::NextColumn();
        ImGui::TextUnformatted("SDRPP v" VERSION_STR " (Built at " __TIME__ ", " __DATE__ ")");
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextUnformatted("https://github.com/qrp73/SDRPP");
        ImGui::Columns(1, "HeaderColumns", true);

        ImGui::TextUnformatted("SDRPP is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published");
        ImGui::TextUnformatted("by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.");
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextUnformatted("Portions of this software was written by the following authors:");
        ImGui::Spacing();
        ImGui::Columns(3, "CreditColumns", true);
        for (int i = 0; i < sdrpp_credits::authorsCount; i++) {
            ImGui::BulletText("%s", sdrpp_credits::authors[i]);
            if ((i % 10)==9) ImGui::NextColumn();
        }
        ImGui::Columns(1, "CreditColumnsEnd", true);

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextUnformatted("This software using the following libraries:");
        ImGui::Spacing();
        ImGui::Columns(3, "LibrariesColumns", true);
        for (int i = 0; i < sdrpp_credits::librariesCount; i++) {
            ImGui::BulletText("%s", sdrpp_credits::libraries[i]);
            if ((i % 4)==3) ImGui::NextColumn();
        }
        ImGui::Columns(1, "LibrariesColumnsEnd", true);


        ImGui::EndPopup();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}
