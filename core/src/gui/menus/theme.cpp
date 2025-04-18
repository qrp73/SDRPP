#include <gui/menus/theme.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/style.h>

namespace thememenu {
    int themeId;
    std::vector<std::string> themeNames;
    std::string themeNamesTxt;
    bool themeAntiAliasedLines = true;
    bool themeAntiAliasedFill = true;

    void init(std::string resDir) {
        // TODO: Not hardcode theme directory
        gui::themeManager.loadThemesFromDir(resDir + "/themes/");
        core::configManager.acquire();
        std::string selectedThemeName = core::configManager.conf["theme"];
        themeAntiAliasedLines = core::configManager.conf.contains("themeAntiAliasedLines") ?
            core::configManager.conf["themeAntiAliasedLines"].get<bool>() :
            ImGui::GetStyle().AntiAliasedLines;
        themeAntiAliasedFill = core::configManager.conf.contains("themeAntiAliasedFill") ?
            core::configManager.conf["themeAntiAliasedFill"].get<bool>() :
            ImGui::GetStyle().AntiAliasedFill;
        core::configManager.release();

        // Select theme by name, if not available, apply Dark theme
        themeNames = gui::themeManager.getThemeNames();
        auto it = std::find(themeNames.begin(), themeNames.end(), selectedThemeName);
        if (it == themeNames.end()) {
            it = std::find(themeNames.begin(), themeNames.end(), "Dark");
            selectedThemeName = "Dark";
        }
        themeId = std::distance(themeNames.begin(), it);
        applyTheme();

        // Apply scaling
        ImGui::GetStyle().ScaleAllSizes(style::uiScale);
        ImGui::GetStyle().AntiAliasedLines = themeAntiAliasedLines;
        ImGui::GetStyle().AntiAliasedFill = themeAntiAliasedFill;

        themeNamesTxt = "";
        for (auto name : themeNames) {
            themeNamesTxt += name;
            themeNamesTxt += '\0';
        }
    }

    void applyTheme() {
        gui::themeManager.applyTheme(themeNames[themeId]);
    }

    void draw(void* ctx) {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        ImGui::LeftLabel("Theme");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo("##theme_select_combo", &themeId, themeNamesTxt.c_str())) {
            applyTheme();
            core::configManager.acquire();
            core::configManager.conf["theme"] = themeNames[themeId];
            core::configManager.release(true);
        }
        if (ImGui::Checkbox("AntiAliased Lines", &themeAntiAliasedLines)) {
            ImGui::GetStyle().AntiAliasedLines = themeAntiAliasedLines;
            core::configManager.acquire();
            core::configManager.conf["themeAntiAliasedLines"] = themeAntiAliasedLines;
            core::configManager.release(true);
        }        
        if (ImGui::Checkbox("AntiAliased Fill", &themeAntiAliasedFill)) {
            ImGui::GetStyle().AntiAliasedFill = themeAntiAliasedFill;
            core::configManager.acquire();
            core::configManager.conf["themeAntiAliasedFill"] = themeAntiAliasedFill;
            core::configManager.release(true);
        }        
    }
}
