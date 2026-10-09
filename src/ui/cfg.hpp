#pragma once
#include "imgui.h"

namespace cfg {

namespace esp {
    inline bool box = true;
    inline bool name = false;
    inline bool health = true;
    inline bool distance = false;
    inline int box_type = 0;
    inline int box_rounding = 0; // 0..10 px
    inline ImVec4 box_col = ImVec4(162/255.f, 144/255.f, 225/255.f, 1.f);
    inline ImVec4 name_col = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 health_col = ImVec4(162/255.f, 144/255.f, 225/255.f, 1.f);
    inline ImVec4 distance_col = ImVec4(162/255.f, 144/255.f, 225/255.f, 1.f);
    inline bool snaplines = false;
    inline int snaplines_col = 0; // preset idx: Cyan/Green/Red/Purple/Yellow/White
}

// t.me/aether_co2 — menu theme + watermark only. Everything below is wired.
namespace litvin {
    inline bool watermark = true;

    inline ImVec4 col_menu = ImVec4(0.878f, 0.149f, 0.169f, 1.f);
    inline ImVec4 col_tabText = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 col_toggle = ImVec4(0.878f, 0.149f, 0.169f, 1.f);
    inline ImVec4 col_slider = ImVec4(0.878f, 0.149f, 0.169f, 1.f);
}

// Combat: target selection runs on live ESP projections (memory pipeline,
// no screen capture needed). Fire taps go through /dev/input (root).
namespace combat {
    inline bool aim_enable = false;
    inline bool visible_check = true; // screen-visibility gate (no wall raycast externally)
    inline bool aim_draw_fov = false;
    inline int aim_fov = 120;     // px radius of detect zone, 40..400
    inline int aim_target = 0;    // 0 crosshair-nearest, 1 closest, 2 weakest
    inline int aim_bone = 0;      // 0 head, 1 chest
    inline int aim_smooth = 6;    // 1..20
    inline int aim_delay = 80;    // ms before engage, 0..500
    inline bool humanizer = false;
    inline int human_amt = 50;    // %, 0..100

    inline bool fire_enable = false;
    inline bool fire_draw_zone = false;
    inline int fire_fov = 90;     // px radius of fire zone, 40..400
    inline int fire_delay = 120;  // ms target must sit in zone, 0..500
    inline int fire_smooth = 6;   // 1..20
    inline bool tap_fire = false; // physical tap via /dev/input (root, needs Fire X/Y)
    inline int fire_x_pct = 85;   // fire button X, % of screen width, 0..100
    inline int fire_y_pct = 60;   // fire button Y, % of screen height, 0..100
    inline bool antirecoil = false; // pull-down compensation swipe while firing
    inline int recoil_power = 12; // px per 120ms swipe, 0..60
    inline int look_x_pct = 70;   // look-area X for the swipe, % width
    inline int look_y_pct = 50;   // look-area Y for the swipe, % height
}

// Extra visual effects (tracers / ESP lines + hit marker + snow + trails).
namespace fx {
    inline bool tracers = false;  // ESP Line master
    inline int tracer_col = 0;    // preset idx
    inline int tracer_life = 350; // ms, 100..800
    inline int muzzle_x = 68;     // line origin X (your gun), % width, 0..100
    inline int muzzle_y = 75;     // line origin Y (your gun), % height, 0..100
    inline bool hitmarker = false;
    inline bool snow = false;
    inline int snow_count = 60;   // flakes, 20..150
    inline bool trail = false;    // rainbow trail behind moving enemies
    inline int trail_mode = 0;    // 0 rainbow, 1 single color
    inline int trail_col = 0;     // preset idx (single mode)
    inline int trail_width = 25;  // x10 px, 10..50
    inline int trail_len = 10;    // points, 4..20 (also quality control)
    inline int trail_speed = 100; // hue anim %, 10..200
    inline int trail_opacity = 80;// %, 10..100
    inline bool trail_glow = true;
}

// Keybinds (physical keys via /dev/input: volume up/down).
// Slot-based: VolUp + VolDown, Toggle or Hold. Persisted to a file.
namespace binds {
    inline bool enable = false;
    inline int volup = 0;   // action id, 0 = none
    inline int voldown = 0; // action id, 0 = none
    inline int mode = 0;    // 0 toggle, 1 hold
}

// Ground Pulse ESP (Expanding Ring ESP) settings.
// int-based so they map 1:1 to LITVIN menu Sl/Sel rows.
namespace pulse {
    inline bool enable = false;   // Enable/Disable
    inline int ring_count = 3;    // 1..5
    inline int spawn_ms = 450;    // 100..1500 ms
    inline int expansion = 180;   // cm/s, 50..500
    inline int min_r = 40;        // cm, 20..200
    inline int max_r = 220;       // cm, 100..600
    inline int color_idx = 0;     // preset: Cyan/Green/Red/Purple/Yellow/White
    inline int alpha = 85;        // %, 5..100
    inline int thickness = 20;    // x10 px, 10..50 -> 1.0..5.0px
    inline int anim_speed = 100;  // %, 25..300
    inline bool only_moving = true; // rings spawn only while the enemy walks
}

}


