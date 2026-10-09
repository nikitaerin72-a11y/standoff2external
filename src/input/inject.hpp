#pragma once

namespace inject {

// Root-only touchscreen tap injector (/dev/input, multitouch protocol B).
// The overlay draws ABOVE the game but drawing is not input: this is the
// separate path that lets Auto Fire physically tap the fire button.
// Requirements (checked at runtime, fail-safe):
//   - root (reading/writing /dev/input needs it, same as the mem reader),
//   - a touchscreen device exposing ABS_MT_POSITION_X,
//   - calibrated fire-button coordinates (Combat -> Auto Fire -> Fire X/Y %).
// No device here could provide those at build time, so everything is
// resolved lazily on first tap and every failure returns false.
bool tap(float x_px, float y_px); // single down/up contact (~10ms), repeat-safe
bool request_tap(float x_px, float y_px); // async version: never blocks the caller
// Short look-drag for recoil compensation (finger moves down on screen).
// Runs on the same worker thread, multitouch slot 1 so it coexists with taps.
bool request_swipe(float x0_px, float y0_px, float x1_px, float y1_px);
void set_screen(float w, float h); // overlay size for px -> sensor mapping
bool ready();                     // a usable touchscreen was opened before
void shutdown();

// Physical keys for binds (volume up/down), watched on a background thread.
void keywatch_ensure();              // start once, no-op afterwards
bool key_down(int which);            // 0 = VolUp, 1 = VolDown; current held state
unsigned long long key_seq(int which); // press counter (edges for Toggle mode)

} // namespace inject
