#include "tracer.hpp"
#include "../ui/theme/theme.hpp"
#include "../ui/cfg.hpp"
#include "../func/visuals.hpp" // esp_preset_color
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <deque>
#include <unordered_map>
#include <algorithm>

namespace tracer {

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void draw_beam(ImVec2 from, ImVec2 to, double startT, double nowT) {
    if (!cfg::fx::tracers) return;
    const float life = clampf(cfg::fx::tracer_life / 1000.f, 0.10f, 0.80f);
    const float t = (float)((nowT - startT) / life);
    if (t < 0.f || t >= 1.f) return;
    if (!std::isfinite(from.x) || !std::isfinite(to.x)) return;

    int idx = cfg::fx::tracer_col;
    if (idx < 0 || idx > 5) idx = 0;
    const ImVec4 c = visuals::esp_preset_color(idx);
    const float fade = (1.f - t) * (1.f - t);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // Glow underlay + core line, full length immediately, fading out.
    dl->AddLine(from, to,
        IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(255 * 0.16f * fade)), 6.f);
    dl->AddLine(from, to,
        IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(255 * 0.60f * fade)), 2.f);
    // Bright head travelling from muzzle to impact.
    const float e = 1.f - (1.f - t) * (1.f - t);
    const ImVec2 hp(from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e);
    dl->AddCircleFilled(hp, 3.f + 2.f * (1.f - t),
        IM_COL32(255, 255, 255, (int)(255 * 0.85f * fade)), 16);
}

void draw_zone(ImVec2 center, float radiusPx, ImU32 col) {
    if (radiusPx < 4.f) return;
    ImGui::GetBackgroundDrawList()->AddCircle(center, radiusPx, col, 48, 2.5f);
}

} // namespace tracer

namespace snow {

// Decorative snowfall over everything (foreground list, draw-only: never
// touches input, so menu/game interaction is unaffected). Static pool,
// no per-frame allocation; dead when disabled (no update at all).
struct Flake { float x, y, spd, ph, sz; };
static std::vector<Flake> flakes;
static double lastT = 0.0;

void draw() {
    if (!cfg::fx::snow) return;
    const double now = ImGui::GetTime();
    float dt = (lastT > 0.0) ? (float)(now - lastT) : 0.016f;
    lastT = now;
    if (!(dt > 0.f) || dt > 0.05f) dt = 0.016f;

    int want = cfg::fx::snow_count;
    if (want < 20) want = 20; if (want > 150) want = 150;
    if ((int)flakes.size() != want) {
        flakes.clear();
        flakes.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            const float h1 = fmodf((float)(i * 2654435761u % 1000) / 1000.f, 1.f);
            const float h2 = fmodf((float)(i * 40503u % 1000) / 1000.f, 1.f);
            flakes.push_back({h1 * g_sw, h2 * g_sh, 30.f + h1 * 60.f,
                              h2 * 6.2831f, 1.f + h1 * 1.5f});
        }
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float t = (float)now;
    for (auto& f : flakes) {
        f.y += f.spd * dt;
        f.x += sinf(t * 1.3f + f.ph) * 12.f * dt;
        if (f.y > g_sh + 4.f) {
            f.y = -4.f;
            f.x = fmodf(f.x * 1.37f + 13.f, g_sw > 1.f ? g_sw : 1.f);
        }
        if (f.x < -4.f) f.x += g_sw; else if (f.x > g_sh + 4000.f) f.x = 0.f;
        const int a = (int)(90 + f.sz * 40);
        dl->AddCircleFilled(ImVec2(f.x, f.y), f.sz,
            IM_COL32(255, 255, 255, a > 255 ? 255 : a), 8);
    }
}

} // namespace snow

namespace trail {

// Rainbow trail behind moving enemies: per-object ring buffer of recent
// foot screen positions, drawn as a hue-cycling fading ribbon + glow +
// joint dots. Capped length = built-in quality control.
static std::unordered_map<uint64_t, std::deque<ImVec2>> paths;

void prune(const std::vector<uint64_t>& alive) {
    if (paths.empty()) return;
    for (auto it = paths.begin(); it != paths.end();) {
        if (std::find(alive.begin(), alive.end(), it->first) == alive.end())
            it = paths.erase(it);
        else ++it;
    }
    if (paths.size() > 24) {
        // Defensive cap; alive-list prune above is the normal path.
        auto it = paths.begin();
        std::advance(it, 6);
        paths.erase(paths.begin(), it);
    }
}

void push(uint64_t key, ImVec2 pt) {
    if (!cfg::fx::trail || !key) return;
    if (!std::isfinite(pt.x) || !std::isfinite(pt.y)) return;
    int len = cfg::fx::trail_len;
    if (len < 4) len = 4; if (len > 20) len = 20;
    auto& dq = paths[key];
    if (!dq.empty()) {
        const ImVec2& last = dq.back();
        const float dx = pt.x - last.x, dy = pt.y - last.y;
        if (dx * dx + dy * dy < 1.f) return; // standing still: no new joints
    }
    dq.push_back(pt);
    while ((int)dq.size() > len) dq.pop_front();
}

void draw() {
    if (!cfg::fx::trail || paths.empty()) return;
    const double now = ImGui::GetTime();
    int op = cfg::fx::trail_opacity;
    if (op < 10) op = 10; if (op > 100) op = 100;
    const float opacity = op / 100.f;
    const float width = std::clamp(cfg::fx::trail_width / 10.f, 1.f, 5.f);
    const float speed = std::clamp(cfg::fx::trail_speed / 100.f, 0.1f, 2.f);
    const bool single = (cfg::fx::trail_mode == 1);
    int cidx = cfg::fx::trail_col;
    if (cidx < 0 || cidx > 5) cidx = 0;
    const ImVec4 sc = visuals::esp_preset_color(cidx);
    const float baseHue = fmodf((float)now * 60.f * speed, 360.f);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (auto& kv : paths) {
        const auto& dq = kv.second;
        const int n = (int)dq.size();
        if (n < 2) continue;
        for (int pass = 0; pass < (cfg::fx::trail_glow ? 2 : 1); ++pass) {
            const bool glow = (pass == 0 && cfg::fx::trail_glow);
            const float w = glow ? width * 3.f : width;
            const float am = glow ? 0.25f : 1.f;
            for (int i = 1; i < n; ++i) {
                const float f = (float)i / (float)(n - 1); // 0 tail .. 1 head
                const float a = opacity * f * f * am;
                if (a < 0.02f) continue;
                ImVec4 rgb;
                if (single) rgb = sc;
                else ImGui::ColorConvertHSVtoRGB(fmodf(baseHue + i * 18.f, 360.f) / 360.f, 1.f, 1.f,
                                                rgb.x, rgb.y, rgb.z);
                dl->AddLine(dq[(size_t)(i - 1)], dq[(size_t)i],
                    IM_COL32((int)(rgb.x * 255), (int)(rgb.y * 255), (int)(rgb.z * 255),
                             (int)(a * 255)), w);
            }
        }
        // Joint sparkle dots, sparse by construction.
        for (int i = 2; i < n; i += 3) {
            const float f = (float)i / (float)(n - 1);
            dl->AddCircleFilled(dq[(size_t)i], 1.6f,
                IM_COL32(255, 255, 255, (int)(opacity * f * 200)), 8);
        }
    }
}

} // namespace trail

namespace hitmark {

static double s_start = -1e9;
static double s_until = -1e9;
static int s_dmg = 0;

void notify(int dmg) {
    if (!cfg::fx::hitmarker) return;
    const double now = ImGui::GetTime();
    s_start = now;
    s_until = now + 0.28; // short pop animation
    s_dmg = dmg > 0 ? dmg : 0;
}

void draw() {
    if (!cfg::fx::hitmarker) return;
    const double now = ImGui::GetTime();
    if (now >= s_until) return;
    const float total = (float)(s_until - s_start);
    if (!(total > 0.f)) return;
    const float t = 1.f - (float)((now - s_start) / total); // 1 -> 0
    if (t <= 0.f) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 c(g_sw * 0.5f, g_sh * 0.5f);
    const float off = 7.f + 9.f * (1.f - t); // lines drift outward
    const float len = 11.f;
    const ImU32 col = IM_COL32(255, 255, 255, (int)(255 * t));
    const float th = 2.f;
    dl->AddLine(ImVec2(c.x - off - len, c.y - off - len), ImVec2(c.x - off, c.y - off), col, th);
    dl->AddLine(ImVec2(c.x + off, c.y - off), ImVec2(c.x + off + len, c.y - off - len), col, th);
    dl->AddLine(ImVec2(c.x - off - len, c.y + off + len), ImVec2(c.x - off, c.y + off), col, th);
    dl->AddLine(ImVec2(c.x + off, c.y + off), ImVec2(c.x + off + len, c.y + off + len), col, th);

    if (s_dmg > 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", s_dmg);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + off + len + 4.f),
            IM_COL32(255, 220, 120, (int)(255 * t)), buf);
    }
}

} // namespace hitmark
