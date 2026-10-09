#pragma once

#include "imgui.h"
#include <cstdint>

namespace combat {

// One projected enemy, filled from the live ESP frame in visuals::draw().
// visible = projected on screen and in front of the camera (screen-space
// gate; wall/occlusion raycast is not available to an external overlay).
struct Candidate {
    uint64_t object = 0;
    ImVec2 head{};
    ImVec2 chest{};
    int hp = 0;
    float dist = 0.f;
    bool visible = false;
};

struct Pick {
    bool ok = false;
    uint64_t object = 0;
    ImVec2 point{};
    float crossDist = 1e9f;
    int hp = 0;
    float dist = 0.f;
};

// mode: 0 = nearest to crosshair, 1 = closest (world distance),
//        2 = weakest (lowest hp).
// bone: 0 = head, 1 = chest (aim point used for zone test + beam).
// useVisible: drop candidates failing the screen-visibility gate.
Pick select_target(const Candidate* cs, int n, ImVec2 cross, float fovPx,
                   int mode, int bone, bool useVisible);

// Humanizer state (per engagement; reset when the target changes).
struct HumanState {
    float x = 0.f, y = 0.f;
    bool init = false;
    static void reset(HumanState& st) { st.x = st.y = 0.f; st.init = false; }
};

// Smooths a desired 2D offset (e.g. crosshair -> target delta):
// exponential approach with rate from smooth (1..20, higher = slower),
// deterministic micro-wobble scaled by humanAmt (0..100), hard speed cap.
// Pure function, frame-rate independent via dt.
ImVec2 humanize_step(HumanState& st, ImVec2 raw, int smooth, int humanAmt,
                     float dt, float tNow);

// Keybinds: VolUp/VolDown slots, Toggle or Hold, persisted to a file.
// Toggle flips the bound cfg bool on each press edge; Hold ORs the feature
// while the key is physically down (menu toggle then shows the base state).
// Volume keys never reach text fields, so typing is never disturbed.
namespace binds {
enum Action { NONE = 0, AIMBOT, AUTOFIRE, PULSE, ESPLINE, SNAP, HITMARK, COUNT };
void load();        // once at startup
void apply();       // every frame: edges, save-on-change, keywatch start
bool aim_active();  // cfg.aim_enable (+held key in Hold mode)
bool fire_active(); // cfg.fire_enable (+held key in Hold mode)
const char* action_name(int a);
} // namespace binds

} // namespace combat
