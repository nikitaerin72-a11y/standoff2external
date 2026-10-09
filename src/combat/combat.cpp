#include "combat.hpp"
#include "../ui/cfg.hpp"
#include "../input/inject.hpp"
#include "../other/diag.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace combat {

static float dist2(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

Pick select_target(const Candidate* cs, int n, ImVec2 cross, float fovPx,
                   int mode, int bone, bool useVisible) {
    Pick best;
    if (!cs || n <= 0 || !(fovPx > 0.f)) return best;
    if (mode < 0 || mode > 2) mode = 0;
    const bool chest = (bone == 1);
    for (int i = 0; i < n; ++i) {
        const Candidate& c = cs[i];
        if (!c.object) continue;
        if (useVisible && !c.visible) continue;
        const ImVec2 p = chest ? c.chest : c.head;
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        const float cd = dist2(p, cross);
        if (cd > fovPx) continue; // outside detect/fire zone
        bool take = false;
        if (!best.ok) take = true;
        else if (mode == 0) take = (cd < best.crossDist);
        else if (mode == 1) take = (c.dist < best.dist);
        else take = (c.hp < best.hp);
        if (take) {
            best.ok = true;
            best.object = c.object;
            best.point = p;
            best.crossDist = cd;
            best.hp = c.hp;
            best.dist = c.dist;
        }
    }
    return best;
}

ImVec2 humanize_step(HumanState& st, ImVec2 raw, int smooth, int humanAmt,
                     float dt, float tNow) {
    if (!st.init) { st.x = 0.f; st.y = 0.f; st.init = true; }
    if (!(dt > 0.f)) return ImVec2(st.x, st.y);
    if (!std::isfinite(raw.x) || !std::isfinite(raw.y)) return ImVec2(st.x, st.y);
    smooth = std::clamp(smooth, 1, 20);
    humanAmt = std::clamp(humanAmt, 0, 100);

    // Higher Smooth setting = slower, heavier motion.
    const float rate = 30.f - (float(smooth - 1) / 19.f) * 26.f; // 30 .. 4 1/s
    const float k = 1.f - expf(-rate * dt);

    // Deterministic micro-wobble (two detuned sines), scaled by Humanize %.
    const float amp = (humanAmt / 100.f) * 5.f; // px
    constexpr float PI2 = 6.28318530718f;
    const float wx = (sinf(tNow * PI2 * 7.f) * 0.6f + sinf(tNow * PI2 * 13.f + 1.7f) * 0.4f) * amp;
    const float wy = (sinf(tNow * PI2 * 9.f + 0.6f) * 0.6f + sinf(tNow * PI2 * 11.f + 2.9f) * 0.4f) * amp;

    float nx = st.x + ((raw.x + wx) - st.x) * k;
    float ny = st.y + ((raw.y + wy) - st.y) * k;

    // Hard speed cap so a teleporting target can't sling the output.
    const float maxD = 3000.f * dt;
    float dx = nx - st.x, dy = ny - st.y;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len > maxD && len > 0.f) {
        const float s = maxD / len;
        dx *= s; dy *= s;
        nx = st.x + dx; ny = st.y + dy;
    }

    st.x = nx; st.y = ny;
    return ImVec2(nx, ny);
}

namespace binds {

static const char* kBindFile = "/data/local/tmp/aether_binds.cfg";

const char* action_name(int a) {
    switch (a) {
        case AIMBOT: return "Aimbot";
        case AUTOFIRE: return "Auto Fire";
        case PULSE: return "Ground Pulse";
        case ESPLINE: return "ESP Line";
        case SNAP: return "Snaplines";
        case HITMARK: return "Hit Marker";
        default: return "None";
    }
}

static void flip(int a) {
    switch (a) {
        case AIMBOT: cfg::combat::aim_enable = !cfg::combat::aim_enable; break;
        case AUTOFIRE: cfg::combat::fire_enable = !cfg::combat::fire_enable; break;
        case PULSE: cfg::pulse::enable = !cfg::pulse::enable; break;
        case ESPLINE: cfg::fx::tracers = !cfg::fx::tracers; break;
        case SNAP: cfg::esp::snaplines = !cfg::esp::snaplines; break;
        case HITMARK: cfg::fx::hitmarker = !cfg::fx::hitmarker; break;
        default: break;
    }
}

static bool held_action(int a) {
    if (a <= NONE || a >= COUNT) return false;
    if (cfg::binds::volup == a && inject::key_down(0)) return true;
    if (cfg::binds::voldown == a && inject::key_down(1)) return true;
    return false;
}

bool aim_active() {
    if (!cfg::binds::enable || cfg::binds::mode != 1) return cfg::combat::aim_enable;
    return cfg::combat::aim_enable || held_action(AIMBOT);
}

bool fire_active() {
    if (!cfg::binds::enable || cfg::binds::mode != 1) return cfg::combat::fire_enable;
    return cfg::combat::fire_enable || held_action(AUTOFIRE);
}

void load() {
    FILE* f = std::fopen(kBindFile, "r");
    if (!f) return;
    int en = 0, up = 0, dn = 0, md = 0;
    if (std::fscanf(f, "%d %d %d %d", &en, &up, &dn, &md) == 4) {
        cfg::binds::enable = (en != 0);
        if (up >= NONE && up < COUNT) cfg::binds::volup = up;
        if (dn >= NONE && dn < COUNT) cfg::binds::voldown = dn;
        if (md == 0 || md == 1) cfg::binds::mode = md;
    }
    std::fclose(f);
}

static void save() {
    FILE* f = std::fopen(kBindFile, "w");
    if (!f) return;
    std::fprintf(f, "%d %d %d %d\n", cfg::binds::enable ? 1 : 0,
                 cfg::binds::volup, cfg::binds::voldown, cfg::binds::mode);
    std::fclose(f);
}

void apply() {
    if (!cfg::binds::enable) return;
    inject::keywatch_ensure();
    // One flip per action per frame even if both slots bind the same action.
    bool trig[COUNT] = {};
    static unsigned long long seqUp = 0, seqDn = 0;
    unsigned long long s = inject::key_seq(0);
    if (s != seqUp) {
        seqUp = s;
        const int a = cfg::binds::volup;
        if (a > NONE && a < COUNT) trig[a] = true;
    }
    s = inject::key_seq(1);
    if (s != seqDn) {
        seqDn = s;
        const int a = cfg::binds::voldown;
        if (a > NONE && a < COUNT) trig[a] = true;
    }
    if (cfg::binds::mode == 0) {
        for (int a = 1; a < COUNT; ++a)
            if (trig[a]) flip(a);
    }
    // Persist on change only.
    static int savedFp = -1;
    const int fp = (cfg::binds::enable ? 1000 : 0) +
                   cfg::binds::volup * 100 + cfg::binds::voldown * 10 + cfg::binds::mode;
    if (fp != savedFp) { savedFp = fp; save(); }
}

} // namespace binds

} // namespace combat
