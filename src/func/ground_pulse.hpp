#pragma once

#include "imgui.h"
#include "../other/vector3.h"
#include <cstdint>

struct matrix;

namespace pulse {

// Ground Pulse ESP (Expanding Ring ESP).
// feet: enemy feet world position (from player::position(), offsets v100::*
//   resolved via dump.cs in /sdcard/bogdlc).
// key: stable enemy id (managed object address from NetObject registry).
// distance: meters to local player (for LOD / culling).
// view/proj: selected gameplay camera matrices.
// globalAlpha: overlay fade (0..1).
// Rings spawn only while the enemy moves (cfg::pulse::only_moving);
// standing still lets alive rings fade out and spawns nothing.
void draw_pulse(uint64_t key, const Vector3& feet, float distance,
                const matrix& view, const matrix& proj, float globalAlpha);

// Drop states of enemies not seen for a while. Call once per frame.
void sweep(long long now_ms);

// Drop all ring states (e.g. when feature toggled off).
void clear();

// Current ring color as ImVec4 (for menu preview / debugging).
ImVec4 pulse_color();

} // namespace pulse
