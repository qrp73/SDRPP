#include <gui/widgets/volume_meter.h>
#include <algorithm>
#include <gui/style.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace ImGui {
    void SNRMeter(float val, const ImVec2& size_arg = ImVec2(0, 0)) {
        ImGuiWindow* window = GetCurrentWindow();
        ImGuiStyle& style = GImGui->Style;

        ImVec2 min = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), 26);
        ImRect bb(min, min + size);

        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) {
            return;
        }

        val = std::clamp<float>(val, 0, 100);
        float ratio = size.x / 90;
        float it = size.x / 9;
        char buf[32];

        window->DrawList->AddRectFilled(min + ImVec2(0, 1), min + ImVec2(roundf((float)val * ratio), 10 * style::uiScale), IM_COL32(0, 136, 255, 255));
        window->DrawList->AddLine(min, min + ImVec2(0, (10.0f * style::uiScale) - 1), text, style::uiScale);
        window->DrawList->AddLine(min + ImVec2(0, (10.0f * style::uiScale) - 1), min + ImVec2(size.x + 1, (10.0f * style::uiScale) - 1), text, style::uiScale);

        for (int i = 0; i < 10; i++) {
            window->DrawList->AddLine(min + ImVec2(roundf((float)i * it), (10.0f * style::uiScale) - 1), min + ImVec2(roundf((float)i * it), (15.0f * style::uiScale) - 1), text, style::uiScale);
            sprintf(buf, "%d", i * 10);
            ImVec2 sz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(min + ImVec2(roundf(((float)i * it) - (sz.x / 2.0)) + 1, 16.0f * style::uiScale), text, buf);
        }
    }

    void LevelMeter(float level, float levelMax, float snr, const ImVec2& size_arg = ImVec2(0, 0)) {
        ImGuiWindow* window = GetCurrentWindow();
        ImGuiStyle& style = GImGui->Style;

        ImVec2 min = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), 26);
        ImRect bb(min, min + size);

        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) {
            return;
        }

        char buf[32];
        ImVec2 sizev = ImGui::CalcTextSize("-99.9 dB");
        ImVec2 sizeg = size-ImVec2(sizev.x, 0);
        float ratio = sizeg.x / 90;
        float it = sizeg.x / 9;

        float gval = std::clamp<float>(level, -90, 0) + 90;
        float fval = std::clamp<float>(levelMax, -90, 0) + 90;

        window->DrawList->AddRectFilled(min + ImVec2(0, 1), min + ImVec2(roundf((float)gval * ratio), 10 * style::uiScale), IM_COL32(0, 192, 0, 255));
        
        window->DrawList->AddRectFilled(min + ImVec2(roundf((float)fval * ratio), 1), min + ImVec2(roundf((float)fval * ratio)+2, 10 * style::uiScale), IM_COL32(255, 255, 0, 255));

        window->DrawList->AddLine(min, min + ImVec2(0, (10.0f * style::uiScale) - 1), text, style::uiScale);
        window->DrawList->AddLine(min + ImVec2(0, (10.0f * style::uiScale) - 1), min + ImVec2(sizeg.x + 1, (10.0f * style::uiScale) - 1), text, style::uiScale);

        for (int i = 0; i < 10; i++) {
            window->DrawList->AddLine(min + ImVec2(roundf((float)i * it), (10.0f * style::uiScale) - 1), min + ImVec2(roundf((float)i * it), (15.0f * style::uiScale) - 1), text, style::uiScale);
            sprintf(buf, "%d", (i-9) * 10);
            ImVec2 sz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(min + ImVec2(roundf(((float)i * it) - (sz.x / 2.0)) + 1, 16.0f * style::uiScale), text, buf);
        }
        
        sprintf(buf, "%+.1f dB", levelMax);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        window->DrawList->AddText(min + ImVec2(size.x-sz.x+25*style::uiScale, 0), text, buf);
        
        sprintf(buf, "%.1f dB", snr);
        sz = ImGui::CalcTextSize(buf);
        window->DrawList->AddText(min + ImVec2(size.x-sz.x+25*style::uiScale, sz.y), text, buf);
    }
}
