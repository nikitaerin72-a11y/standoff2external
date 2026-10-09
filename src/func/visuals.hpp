#pragma once

#include "imgui.h"

namespace visuals {
    void draw();
    void dbox(const ImVec2& t, const ImVec2& b, float a);
    void dhp(int hp, const ImVec2& t, const ImVec2& b, float box_h, float a);
    void dnick(const char* name, const ImVec2& box_max, float cx, float sz, float a);
    void ddist(float dist, float x, float y, float sz, float a);
    void dsnapline(const ImVec2& foot, float a);
    void draw_text_outlined(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text);

    // Preset palette shared by ESP color selectors (Cyan/Green/Red/Purple/Yellow/White).
    inline ImVec4 esp_preset_color(int idx) {
        switch (idx) {
            case 1:  return ImVec4(0.20f, 1.00f, 0.45f, 1.f);
            case 2:  return ImVec4(1.00f, 0.25f, 0.30f, 1.f);
            case 3:  return ImVec4(0.70f, 0.45f, 1.00f, 1.f);
            case 4:  return ImVec4(1.00f, 0.85f, 0.20f, 1.f);
            case 5:  return ImVec4(1.00f, 1.00f, 1.00f, 1.f);
            case 0:
            default: return ImVec4(0.20f, 0.90f, 1.00f, 1.f);
        }
    }
}
