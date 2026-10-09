#include "ground_pulse.hpp"
#include "../game/game.hpp"
#include "../game/math.hpp"
#include "../other/diag.hpp"
#include "../ui/theme/theme.hpp"
#include "../ui/cfg.hpp"
#include "imgui.h"

#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>

namespace pulse {

namespace {

struct Ring {
    double birth = 0.0; // ImGui::GetTime() seconds
};

struct EnemyState {
    double last_spawn = 0.0;
    long long last_seen_ms = 0;
    std::vector<Ring> rings;
    // Movement gate: rings exist only while the enemy walks.
    Vector3 lastPos{};
    bool hasPos = false;
    double lastUpdate = 0.0;
    double lastMoveT = -1e9;
};

inline std::unordered_map<uint64_t, EnemyState>& states() {
    static std::unordered_map<uint64_t, EnemyState> s;
    return s;
}

inline ImVec4 palette_vec(int idx) {
    switch (idx) {
        case 1:  return ImVec4(0.20f, 1.00f, 0.45f, 1.f); // Green
        case 2:  return ImVec4(1.00f, 0.25f, 0.30f, 1.f); // Red
        case 3:  return ImVec4(0.70f, 0.45f, 1.00f, 1.f); // Purple
        case 4:  return ImVec4(1.00f, 0.85f, 0.20f, 1.f); // Yellow
        case 5:  return ImVec4(1.00f, 1.00f, 1.00f, 1.f); // White
        case 0:
        default: return ImVec4(0.20f, 0.90f, 1.00f, 1.f); // Cyan
    }
}

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

ImVec4 pulse_color() {
    int idx = cfg::pulse::color_idx;
    if (idx < 0 || idx > 5) idx = 0;
    return palette_vec(idx);
}

void clear() {
    states().clear();
}

void sweep(long long now_ms) {
    auto& s = states();
    if (s.empty()) return;
    for (auto it = s.begin(); it != s.end();) {
        if (now_ms - it->second.last_seen_ms > 2500)
            it = s.erase(it);
        else
            ++it;
    }
}

void draw_pulse(uint64_t key, const Vector3& feet, float distance,
                const matrix& view, const matrix& proj, float globalAlpha) {
    if (!cfg::pulse::enable || key == 0) return;
    if (globalAlpha < 0.01f) return;
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z)) return;
    if (distance > 120.f) return; // subpixel beyond this, save projections

    // ---- settings (sanitized, cheap) ----
    const int ringCount = std::clamp(cfg::pulse::ring_count, 1, 5);
    const float spawnInt = clampf(cfg::pulse::spawn_ms / 1000.f, 0.10f, 1.50f);
    const float animSpd  = clampf(cfg::pulse::anim_speed / 100.f, 0.25f, 3.00f);
    const float expSpeed = clampf(cfg::pulse::expansion / 100.f, 0.50f, 5.00f); // m/s
    float minR = clampf(cfg::pulse::min_r / 100.f, 0.20f, 2.00f);               // m
    float maxR = clampf(cfg::pulse::max_r / 100.f, 1.00f, 6.00f);               // m
    if (maxR <= minR + 0.05f) maxR = minR + 0.50f;
    const float baseAlpha = clampf(cfg::pulse::alpha / 100.f, 0.05f, 1.00f);
    const float thickBase = clampf(cfg::pulse::thickness / 10.f, 1.0f, 5.0f);   // px
    const ImVec4 baseCol = pulse_color();

    const float life = (maxR - minR) / expSpeed; // seconds per ring at animSpd=1
    if (!(life > 0.01f)) return;

    const double now = ImGui::GetTime();
    const double effInterval = spawnInt / animSpd;

    auto& s = states();
    EnemyState& st = s[key]; // cold start: last_spawn == 0
    st.last_seen_ms = diag::now_ms();

    // ---- movement gate: spawn rings only while the enemy walks ----
    // Speed between consecutive frames; standing still drains the pool
    // (alive rings fade out, no new ones). Teleports/respawns are ignored.
    bool moving = true;
    if (cfg::pulse::only_moving) {
        moving = false;
        if (st.hasPos) {
            const double dt = now - st.lastUpdate;
            if (dt > 0.0005 && dt < 1.0) {
                const float dx = feet.x - st.lastPos.x;
                const float dy = feet.y - st.lastPos.y;
                const float dz = feet.z - st.lastPos.z;
                const float moved = sqrtf(dx * dx + dy * dy + dz * dz);
                if (moved < 5.0f && moved >= 0.01f && (moved / (float)dt) >= 0.6f)
                    st.lastMoveT = now;
            }
        } else {
            st.hasPos = true;
        }
        st.lastPos = feet;
        st.lastUpdate = now;
        moving = (now - st.lastMoveT) <= 0.40; // grace between footsteps
    }

    if (moving && st.last_spawn == 0.0) {
        // Pre-seed staggered rings so the pulse is visible instantly
        // instead of waiting for the first spawn intervals.
        st.last_spawn = now;
        st.rings.reserve((size_t)ringCount);
        for (int i = ringCount - 1; i >= 1; --i) {
            const double age = (life / ringCount) * i;
            if ((int)st.rings.size() >= ringCount) break;
            st.rings.push_back(Ring{now - age});
        }
    }

    if (moving && (int)st.rings.size() < ringCount && (now - st.last_spawn) >= effInterval) {
        st.rings.push_back(Ring{now});
        st.last_spawn = now;
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Thickness slightly larger up close, stable at range. One calc per enemy.
    const float thick = thickBase * clampf(12.f / (distance + 0.1f), 0.6f, 1.4f);

    // Fixed segment budget: 28 close / 20 far. No per-frame alloc.
    const int SEG = (distance < 25.f) ? 28 : 20;
    constexpr float PI2 = 6.28318530718f;

    const float rCol = baseCol.x, gCol = baseCol.y, bCol = baseCol.z;

    for (size_t ri = 0; ri < st.rings.size();) {
        const double age = (now - st.rings[ri].birth) * animSpd;
        const float t = (float)(age / life);
        if (t < 0.f) { ++ri; continue; }
        if (t >= 1.f) {
            st.rings[ri] = st.rings.back();
            st.rings.pop_back();
            continue;
        }

        const float radius = minR + (maxR - minR) * t; // linear expansion
        const float fade = (1.f - t) * (1.f - t) * (1.f - t * 0.35f); // smooth-ish falloff
        const float a = baseAlpha * fade * globalAlpha;
        if (a < 0.02f) { ++ri; continue; }

        const ImU32 col = IM_COL32(
            (int)(rCol * 255.f), (int)(gCol * 255.f), (int)(bCol * 255.f),
            (int)(a * 255.f));

        // Screen-space radius estimate for early-out (1 extra projection).
        ImVec2 centerPx;
        if (!world_to_screen(feet, view, proj, centerPx)) { ++ri; continue; }
        const Vector3 edgeW(feet.x + radius, feet.y, feet.z);
        ImVec2 edgePx;
        if (!world_to_screen(edgeW, view, proj, edgePx)) { ++ri; continue; }
        const float dx = edgePx.x - centerPx.x, dy = edgePx.y - centerPx.y;
        if ((dx * dx + dy * dy) < 9.f) { ++ri; continue; } // r < 3px

        // Ground circle on XZ plane at feet.y + 5cm (avoids floor z-fight look).
        const float y = feet.y + 0.05f;
        ImVec2 prev{};
        bool hasPrev = false;
        for (int i = 0; i <= SEG; ++i) {
            const float ang = PI2 * (float)i / (float)SEG;
            const Vector3 wp(feet.x + cosf(ang) * radius, y, feet.z + sinf(ang) * radius);
            ImVec2 sp;
            if (!world_to_screen(wp, view, proj, sp)) { hasPrev = false; continue; }
            if (!hasPrev) {
                prev = sp;
                hasPrev = true;
            } else {
                dl->AddLine(prev, sp, col, thick);
                prev = sp;
            }
        }

        ++ri;
    }
}

} // namespace pulse
