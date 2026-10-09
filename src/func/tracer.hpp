#pragma once

#include "imgui.h"
#include <cstdint>
#include <vector>

namespace tracer {

// Engagement beam: crosshair -> aim point. Stateless; phase derives from
// (nowT - startT) / life. Draws the full line immediately with fading alpha
// plus a bright head pulse travelling along it, then disappears.
void draw_beam(ImVec2 from, ImVec2 to, double startT, double nowT);

// FOV / fire zone circle around the crosshair.
void draw_zone(ImVec2 center, float radiusPx, ImU32 col);

} // namespace tracer

namespace snow {
void draw(); // decorative snowfall, foreground, input-safe
} // namespace snow

namespace trail {
void push(uint64_t key, ImVec2 pt); // record foot screen pos (call when projected)
void draw();                        // rainbow/single ribbon + glow + dots
void prune(const std::vector<uint64_t>& alive);
} // namespace trail

namespace hitmark {

// Feeding: visuals::draw() polls enemy HP every frame; any observed drop on
// a recently-visible enemy fires notify(dmg). Short cross animation at the
// screen center: 4 expanding lines + fade (+ damage number).
void notify(int dmg);
void draw();

} // namespace hitmark
