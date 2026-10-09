// ============================================================================
//  t.me/aether_co2 — LITVIN TAP MENU port for aether
// ============================================================================
#define IMGUI_DEFINE_MATH_OPERATORS
#include "menu.hpp"
#include "bar.hpp"
#include "cfg.hpp"
#include "theme/theme.hpp"
#include "Android_draw/draw.h"
#include "imgui.h"

#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <cctype>

namespace ui::menu {

// ---------------------------------------------------------------- palette
// t.me/aether_co2
inline ImU32 Hex(unsigned h, int a = 255) { return IM_COL32((h >> 16) & 255, (h >> 8) & 255, h & 255, a); }

static const ImU32 C_BG      = Hex(0x1a1b20);
static const ImU32 C_SIDE    = Hex(0x16171b);
static const ImU32 C_BTN     = Hex(0x202127);
static const ImU32 C_BTN_H   = Hex(0x26272d);
static const ImU32 C_TAB     = Hex(0x1d1e23);
static const ImU32 C_PANEL   = Hex(0x1e1f24);
static const ImU32 C_TRACK   = Hex(0x2a2b31);
static const ImU32 C_TRACK_H = Hex(0x33343b);
static const ImU32 C_TXT     = Hex(0x8e9097);
static const ImU32 C_TXT_H   = Hex(0xb5b7bd);
static const ImU32 C_DIM     = Hex(0x5e6067);
static const ImU32 C_LINE    = Hex(0x24252b);
static const ImU32 C_BORDER  = Hex(0x2a2b31);
static const ImU32 C_VAL     = Hex(0xd6d7db);
static const ImU32 C_NAV     = Hex(0xa3a5ac);
static const ImU32 C_NAV_H   = Hex(0xd0d2d7);
static const ImU32 C_INPUT   = Hex(0xdcdde1);
static const ImU32 C_WHITE   = IM_COL32_WHITE;

static const float PI = 3.14159265f;

static float menu_scale = 1.5f;

static ImFont* fMed  = nullptr;
static ImFont* fSemi = nullptr;
static ImFont* fBold = nullptr;

// ---------------------------------------------------------------- helpers
// t.me/aether_co2
inline float  S(float v)            { return v * menu_scale; }
inline ImVec2 S2(float x, float y)  { return ImVec2(x * menu_scale, y * menu_scale); }
inline ImU32  U(const ImVec4& v)    { return ImGui::ColorConvertFloat4ToU32(v); }
inline void   PF(ImFont* f)         { ImGui::PushFont(f ? f : ImGui::GetFont()); }

static ImU32 Mix(ImU32 a, ImU32 b, float t) {
    ImVec4 A = ImGui::ColorConvertU32ToFloat4(a), B = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(A.x + (B.x - A.x) * t, A.y + (B.y - A.y) * t,
                                                 A.z + (B.z - A.z) * t, A.w + (B.w - A.w) * t));
}
static ImU32 WithAlpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c); v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

static std::unordered_map<ImGuiID, float> g_anim;
static float Anim(ImGuiID id, float target, float speed = 14.f) {
    auto it = g_anim.find(id);
    if (it == g_anim.end()) { g_anim[id] = target; return target; }
    float& v = it->second;
    v += (target - v) * (1.f - expf(-speed * ImGui::GetIO().DeltaTime));
    if (fabsf(target - v) < 0.002f) v = target;
    return v;
}

static bool Match(const char* s, const char* f) {
    while (*f == ' ') ++f;
    if (!*f) return true;
    for (; *s; ++s) {
        const char* a = s; const char* b = f;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

// ---------------------------------------------------------------- icons
// t.me/aether_co2
enum Ico { I_USERS, I_TARGET, I_GRENADE, I_GRID, I_GEAR, I_FOLDER, I_CLOSE, I_SEARCH, I_LEFT, I_RIGHT };

static void DrawIcon(ImDrawList* d, int ico, ImVec2 c, float size, ImU32 col, float th) {
    const float k = size / 24.f;
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 12.f) * k, c.y + (y - 12.f) * k); };
    switch (ico) {
    case I_USERS:
        d->AddCircle(P(9, 7), 4 * k, col, 24, th);
        d->PathLineTo(P(2, 21)); d->PathLineTo(P(2, 19));
        d->PathArcTo(P(6, 19), 4 * k, PI, PI * 1.5f, 10);
        d->PathArcTo(P(12, 19), 4 * k, PI * 1.5f, PI * 2.f, 10);
        d->PathLineTo(P(16, 21));
        d->PathStroke(col, 0, th);
        d->PathLineTo(P(22, 21)); d->PathLineTo(P(22, 19));
        d->PathArcTo(P(18, 19), 4 * k, 0.f, -1.25f, 8);
        d->PathStroke(col, 0, th);
        d->PathArcTo(P(15, 7), 4 * k, -1.32f, 1.32f, 12);
        d->PathStroke(col, 0, th);
        break;
    case I_TARGET:
        d->AddCircle(P(12, 12), 7.5f * k, col, 32, th);
        d->AddCircle(P(12, 12), 3.f * k, col, 20, th);
        d->AddLine(P(12, 1.5f), P(12, 4.5f), col, th);
        d->AddLine(P(12, 19.5f), P(12, 22.5f), col, th);
        d->AddLine(P(1.5f, 12), P(4.5f, 12), col, th);
        d->AddLine(P(19.5f, 12), P(22.5f, 12), col, th);
        break;
    case I_GRENADE:
        d->AddCircle(P(13.5f, 15), 6.5f * k, col, 28, th);
        d->PathLineTo(P(10.5f, 9.2f)); d->PathLineTo(P(10.5f, 6));
        d->PathLineTo(P(16.5f, 6));    d->PathLineTo(P(16.5f, 9.2f));
        d->PathStroke(col, 0, th);
        d->AddLine(P(5, 2.5f), P(5, 7.5f), col, th);
        d->AddLine(P(2.5f, 5), P(7.5f, 5), col, th);
        break;
    case I_GRID:
        for (int i = 0; i < 4; i++) {
            float x = (i % 2) ? 13.5f : 3.f, y = (i / 2) ? 13.5f : 3.f;
            d->AddRect(P(x, y), P(x + 7.5f, y + 7.5f), col, 2.2f * k, 0, th);
        }
        break;
    case I_GEAR: {
        const int teeth = 8;
        for (int i = 0; i < teeth; i++) {
            float a = i * 2.f * PI / teeth;
            const float q[4][2] = { {7.6f, a - 0.30f}, {10.f, a - 0.17f}, {10.f, a + 0.17f}, {7.6f, a + 0.30f} };
            for (auto& p : q) d->PathLineTo(ImVec2(c.x + cosf(p[1]) * p[0] * k, c.y + sinf(p[1]) * p[0] * k));
        }
        d->PathStroke(col, ImDrawFlags_Closed, th);
        d->AddCircle(c, 3.f * k, col, 20, th);
        break;
    }
    case I_FOLDER: {
        const ImVec2 f[] = { P(2.5f, 18.5f), P(2.5f, 5), P(4, 3.5f), P(8.3f, 3.5f), P(10.6f, 6.3f),
                             P(20, 6.3f), P(21.5f, 7.8f), P(21.5f, 18.5f), P(20, 20), P(4, 20) };
        for (auto& p : f) d->PathLineTo(p);
        d->PathStroke(col, ImDrawFlags_Closed, th);
        d->AddLine(P(9, 13.2f), P(15, 13.2f), col, th);
        break;
    }
    case I_CLOSE:
        d->AddLine(P(6, 6), P(18, 18), col, th);
        d->AddLine(P(18, 6), P(6, 18), col, th);
        break;
    case I_SEARCH:
        d->AddCircle(P(11, 11), 7.f * k, col, 28, th);
        d->AddLine(P(16.5f, 16.5f), P(21, 21), col, th);
        break;
    case I_LEFT:
        d->PathLineTo(P(14.5f, 5)); d->PathLineTo(P(8, 12)); d->PathLineTo(P(14.5f, 19));
        d->PathStroke(col, 0, th);
        break;
    case I_RIGHT:
        d->PathLineTo(P(9.5f, 5)); d->PathLineTo(P(16, 12)); d->PathLineTo(P(9.5f, 19));
        d->PathStroke(col, 0, th);
        break;
    }
}

static void DrawLogo(ImDrawList* d, ImVec2 c, float size) {
    const float k = size / 30.f;
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 15.f) * k, c.y + (y - 15.f) * k); };
    ImVec2 in[4] = { P(18.4f, 11.6f), P(18.4f, 18.4f), P(11.6f, 18.4f), P(11.6f, 11.6f) };
    d->AddQuadFilled(in[0], in[1], in[2], in[3], C_WHITE);
    d->AddTriangleFilled(in[3], P(15, 2),  in[0], C_WHITE);
    d->AddTriangleFilled(in[0], P(28, 15), in[1], C_WHITE);
    d->AddTriangleFilled(in[1], P(15, 28), in[2], C_WHITE);
    d->AddTriangleFilled(in[2], P(2, 15),  in[3], C_WHITE);
}

// ---------------------------------------------------------------- data
// t.me/aether_co2
enum RowType { R_TOGGLE, R_SELECT, R_SLIDER, R_BUTTON, R_INFO };
struct Row {
    const char* label; RowType type; bool* b; int* v;
    const char* const* opts; int count; int mn; int mx;
    void (*fn)() = nullptr;      // R_BUTTON action
    const char* info = nullptr;  // R_INFO value text
};
static Row T  (const char* l, bool* b)                             { return Row{ l, R_TOGGLE, b, nullptr, nullptr, 0, 0, 0 }; }
static Row Sel(const char* l, int* v, const char* const* o, int n) { return Row{ l, R_SELECT, nullptr, v, o, n, 0, 0 }; }
static Row Sl (const char* l, int* v, int mn, int mx)              { return Row{ l, R_SLIDER, nullptr, v, nullptr, 0, mn, mx }; }
static Row Btn(const char* l, void (*fn)()) {
    Row r{ l, R_BUTTON, nullptr, nullptr, nullptr, 0, 0, 0 };
    r.fn = fn;
    return r;
}
static Row Inf(const char* l, const char* v) {
    Row r{ l, R_INFO, nullptr, nullptr, nullptr, 0, 0, 0 };
    r.info = v;
    return r;
}

static bool g_exitReq = false;
static void do_exit() { g_exitReq = true; } // main loop breaks -> shutdown() kills the overlay
bool consume_exit() { const bool r = g_exitReq; g_exitReq = false; return r; }
struct Tab     { const char* name; std::vector<Row> rows; };
struct Section { int icon; std::vector<Tab> tabs; };

static const char* kBoxStyle[]   = { "Full", "Corner" };
static const char* kPulseColors[] = { "Cyan", "Green", "Red", "Purple", "Yellow", "White" };
static const char* kCombatTarget[] = { "Crosshair", "Closest", "Weakest" };
static const char* kAimBone[] = { "Head", "Chest" };
static const char* kBindActions[] = { "None", "Aimbot", "Auto Fire", "Ground Pulse", "ESP Line", "Snaplines", "Hit Marker" };
static const char* kBindModes[] = { "Toggle", "Hold" };
static const char* kTrailModes[] = { "Rainbow", "Single" };
#define OPT(a) a, IM_ARRAYSIZE(a)

static std::vector<Section>& Sections() {
    // Only wired features. Everything here reads live cfg every frame.
    static std::vector<Section> s = {
        { I_USERS, {
            { "Visuals", { T("Box ESP", &cfg::esp::box), T("Name", &cfg::esp::name), T("Health Bar", &cfg::esp::health),
                           T("Distance", &cfg::esp::distance),
                           Sel("Box Style", &cfg::esp::box_type, OPT(kBoxStyle)),
                           Sl("Box Rounding", &cfg::esp::box_rounding, 0, 10) } },
            { "Pulse",   { T("Ground Pulse", &cfg::pulse::enable),
                           T("Only When Moving", &cfg::pulse::only_moving),
                           Sl("Ring Count", &cfg::pulse::ring_count, 1, 5),
                           Sl("Spawn Interval (ms)", &cfg::pulse::spawn_ms, 100, 1500),
                           Sl("Expansion (cm/s)", &cfg::pulse::expansion, 50, 500),
                           Sl("Min Radius (cm)", &cfg::pulse::min_r, 20, 200),
                           Sl("Max Radius (cm)", &cfg::pulse::max_r, 100, 600),
                           Sel("Color", &cfg::pulse::color_idx, OPT(kPulseColors)),
                           Sl("Alpha (%)", &cfg::pulse::alpha, 5, 100),
                           Sl("Thickness x10", &cfg::pulse::thickness, 10, 50),
                           Sl("Anim Speed (%)", &cfg::pulse::anim_speed, 25, 300) } },
            { "Other",   { T("Snaplines", &cfg::esp::snaplines),
                           Sel("Snaplines Color", &cfg::esp::snaplines_col, OPT(kPulseColors)),
                           T("ESP Line", &cfg::fx::tracers),
                           Sel("ESP Line Color", &cfg::fx::tracer_col, OPT(kPulseColors)),
                           Sl("ESP Line Life (ms)", &cfg::fx::tracer_life, 100, 800),
                           Sl("Muzzle X %", &cfg::fx::muzzle_x, 0, 100),
                           Sl("Muzzle Y %", &cfg::fx::muzzle_y, 0, 100),
                           T("Hit Marker", &cfg::fx::hitmarker),
                           T("Snow", &cfg::fx::snow),
                           Sl("Snow Count", &cfg::fx::snow_count, 20, 150),
                           T("Watermark", &cfg::litvin::watermark) } },
            { "Trail",   { T("Rainbow Trail", &cfg::fx::trail),
                           Sel("Trail Color", &cfg::fx::trail_mode, OPT(kTrailModes)),
                           Sel("Single Color", &cfg::fx::trail_col, OPT(kPulseColors)),
                           Sl("Trail Width x10", &cfg::fx::trail_width, 10, 50),
                           Sl("Trail Length", &cfg::fx::trail_len, 4, 20),
                           Sl("Rainbow Speed (%)", &cfg::fx::trail_speed, 10, 200),
                           Sl("Opacity (%)", &cfg::fx::trail_opacity, 10, 100),
                           T("Glow", &cfg::fx::trail_glow) } } } },
        { I_TARGET, {
            { "Aim",      { T("Aimbot", &cfg::combat::aim_enable),
                           T("Visible Check", &cfg::combat::visible_check),
                           T("Draw FOV", &cfg::combat::aim_draw_fov),
                           Sl("FOV (px)", &cfg::combat::aim_fov, 40, 400),
                           Sel("Target", &cfg::combat::aim_target, OPT(kCombatTarget)),
                           Sel("Bone", &cfg::combat::aim_bone, OPT(kAimBone)),
                           Sl("Smooth", &cfg::combat::aim_smooth, 1, 20),
                           Sl("Delay (ms)", &cfg::combat::aim_delay, 0, 500),
                           T("Anti-Recoil", &cfg::combat::antirecoil),
                           Sl("Recoil Power", &cfg::combat::recoil_power, 0, 60),
                           Sl("Look X %", &cfg::combat::look_x_pct, 0, 100),
                           Sl("Look Y %", &cfg::combat::look_y_pct, 0, 100),
                           T("Humanizer", &cfg::combat::humanizer),
                           Sl("Humanize (%)", &cfg::combat::human_amt, 0, 100) } },
            { "Auto Fire", { T("Auto Fire", &cfg::combat::fire_enable),
                           T("Tap Fire", &cfg::combat::tap_fire),
                           T("Draw Zone", &cfg::combat::fire_draw_zone),
                           Sl("Fire FOV (px)", &cfg::combat::fire_fov, 40, 400),
                           Sl("Delay (ms)", &cfg::combat::fire_delay, 0, 500),
                           Sl("Smooth", &cfg::combat::fire_smooth, 1, 20),
                           Sl("Fire X %", &cfg::combat::fire_x_pct, 0, 100),
                           Sl("Fire Y %", &cfg::combat::fire_y_pct, 0, 100) } } } },
        { I_GEAR, {
            { "Binds",    { T("Keybinds", &cfg::binds::enable),
                           Sel("Vol Up", &cfg::binds::volup, OPT(kBindActions)),
                           Sel("Vol Down", &cfg::binds::voldown, OPT(kBindActions)),
                           Sel("Mode", &cfg::binds::mode, OPT(kBindModes)) } },
            { "Settings", { Btn("Exit Overlay", do_exit),
                           Inf("Version", "bycmd-1.0.0"),
                           Inf("Game", "Standoff2 1.0.0"),
                           Inf("Mode", "Root / External") } } } },
    };
    return s;
}

static int  g_cur = 0;
static int  g_tab[5] = {};
static char g_query[64] = "";
static bool g_gearWasOpen = false;

// ---------------------------------------------------------------- widgets
// t.me/aether_co2
static bool IconButton(const char* id, ImVec2 pos, ImVec2 size, int ico, ImU32 idle, float th, bool forceOn = false) {
    ImGui::SetCursorScreenPos(pos);
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    float h = Anim(ImGui::GetID(id), (hov || forceOn) ? 1.f : 0.f);
    ImDrawList* d = ImGui::GetWindowDrawList();
    float sc = act ? 0.94f : 1.f;
    ImVec2 c = pos + size * 0.5f, half = size * 0.5f * sc;
    d->AddRectFilled(c - half, c + half, Mix(C_BTN, C_BTN_H, h), S(9) * sc);
    DrawIcon(d, ico, c, S(19) * sc, Mix(idle, forceOn ? C_NAV_H : C_NAV, h), th);
    return pressed;
}

static void Toggle(ImVec2 pos, bool* v) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 sz = S2(44, 23);
    ImGui::SetCursorScreenPos(pos);
    if (ImGui::InvisibleButton("##tg", sz)) *v = !*v;
    float t = Anim(ImGui::GetID("##tg"), *v ? 1.f : 0.f, 12.f);
    d->AddRectFilled(pos, pos + sz, Mix(C_TRACK, U(cfg::litvin::col_toggle), t), sz.y * 0.5f);
    float r = S(8.5f);
    d->AddCircleFilled(ImVec2(pos.x + S(3) + r + t * S(21), pos.y + sz.y * 0.5f), r, C_WHITE, 24);
}

static bool ArrowButton(const char* id, ImVec2 pos, bool right) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 sz = S2(23, 23);
    ImGui::SetCursorScreenPos(pos);
    bool pressed = ImGui::InvisibleButton(id, sz);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    float h = Anim(ImGui::GetID(id), hov ? 1.f : 0.f);
    float sc = act ? 0.9f : 1.f;
    ImVec2 c = pos + sz * 0.5f, half = sz * 0.5f * sc;
    d->AddRectFilled(c - half, c + half, Mix(C_TRACK, C_TRACK_H, h), S(7) * sc);
    DrawIcon(d, right ? I_RIGHT : I_LEFT, c, S(12) * sc, Mix(C_NAV, C_WHITE, h), S(1.8f));
    return pressed;
}

static void Selector(float right, float cy, int* idx, const char* const* opts, int n) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    float top = cy - S(11.5f);
    if (ArrowButton("##l", ImVec2(right - S(122), top), false)) *idx = (*idx - 1 + n) % n;
    if (ArrowButton("##r", ImVec2(right - S(23),  top), true))  *idx = (*idx + 1) % n;
    if (*idx < 0) *idx = 0;
    if (*idx >= n) *idx = n - 1;
    const char* txt = opts[*idx];
    ImVec2 ts = ImGui::CalcTextSize(txt);
    float cx = right - S(23) - S(6) - S(32);
    d->AddText(ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), C_VAL, txt);
}

static void Slider(float right, float cy, int* v, int mn, int mx) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float w = S(170), h = S(5);
    const float x0 = right - S(210);
    ImGui::SetCursorScreenPos(ImVec2(x0 - S(7), cy - S(9)));
    ImGui::InvisibleButton("##sl", ImVec2(w + S(14), S(18)));
    bool act = ImGui::IsItemActive();
    if (act) {
        float p = (ImGui::GetIO().MousePos.x - x0) / w;
        p = p < 0.f ? 0.f : (p > 1.f ? 1.f : p);
        *v = mn + (int)lroundf(p * (float)(mx - mn));
    }
    float p  = (float)(*v - mn) / (float)(mx - mn);
    float pa = Anim(ImGui::GetID("##slA"), p, 24.f);
    ImVec2 a(x0, cy - h * 0.5f);
    d->AddRectFilled(a, a + ImVec2(w, h), C_TRACK, h * 0.5f);
    if (w * pa > 1.f) d->AddRectFilled(a, ImVec2(x0 + w * pa, a.y + h), U(cfg::litvin::col_slider), h * 0.5f);
    d->AddCircleFilled(ImVec2(x0 + w * pa, cy), S(6.5f) * (act ? 1.15f : 1.f), C_WHITE, 20);

    char buf[16]; snprintf(buf, sizeof buf, "%d", *v);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    d->AddText(ImVec2(right - ts.x, cy - ts.y * 0.5f), C_VAL, buf);
}

static void DrawRows(std::vector<Row>& rows) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float rowH = S(44);
    std::vector<Row*> vis;
    for (auto& r : rows) if (Match(r.label, g_query)) vis.push_back(&r);

    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;

    if (vis.empty()) {
        d->AddRectFilled(p, p + ImVec2(w, rowH), C_PANEL, S(9));
        char msg[96]; snprintf(msg, sizeof msg, "No settings match \"%s\"", g_query);
        d->AddText(ImVec2(p.x + S(15), p.y + (rowH - ImGui::GetFontSize()) * 0.5f), C_DIM, msg);
        ImGui::Dummy(ImVec2(w, rowH));
        return;
    }

    d->AddRectFilled(p, p + ImVec2(w, rowH * vis.size()), C_PANEL, S(9));
    for (size_t i = 0; i < vis.size(); i++) {
        Row& r = *vis[i];
        ImVec2 rp(p.x, p.y + rowH * i);
        float cy = rp.y + rowH * 0.5f;
        float right = rp.x + w - S(17);

        ImGui::PushID((int)i);
        if (r.type == R_TOGGLE || r.type == R_SELECT || r.type == R_SLIDER) {
            float ctrlW = r.type == R_TOGGLE ? S(44) : (r.type == R_SELECT ? S(122) : S(210));
            d->PushClipRect(rp, ImVec2(right - ctrlW - S(12), rp.y + rowH), true);
            d->AddText(ImVec2(rp.x + S(15), cy - ImGui::GetFontSize() * 0.5f), C_TXT, r.label);
            d->PopClipRect();
        }

        switch (r.type) {
        case R_TOGGLE: Toggle(ImVec2(right - S(44), cy - S(11.5f)), r.b); break;
        case R_SELECT: Selector(right, cy, r.v, r.opts, r.count);         break;
        case R_SLIDER: Slider(right, cy, r.v, r.mn, r.mx);                break;
        case R_BUTTON: {
            const ImVec2 bp(rp.x + S(8), rp.y + S(6));
            const ImVec2 bs(w - S(16), rowH - S(12));
            ImGui::SetCursorScreenPos(bp);
            const bool pressed = ImGui::InvisibleButton("##btn", bs);
            const bool hov = ImGui::IsItemHovered();
            const float h = Anim(ImGui::GetID("##btnh"), hov ? 1.f : 0.f);
            d->AddRectFilled(bp, bp + bs, Mix(C_BTN, C_BTN_H, h), S(9));
            d->AddRect(bp, bp + bs, hov ? U(cfg::litvin::col_menu) : C_BORDER, S(9));
            PF(fBold);
            const ImVec2 ts = ImGui::CalcTextSize(r.label);
            d->AddText(bp + ImVec2((bs.x - ts.x) * 0.5f, (bs.y - ts.y) * 0.5f),
                       hov ? C_NAV_H : C_TXT_H, r.label);
            ImGui::PopFont();
            if (pressed && r.fn) r.fn();
            break;
        }
        case R_INFO: {
            d->AddText(ImVec2(rp.x + S(15), cy - ImGui::GetFontSize() * 0.5f), C_DIM, r.label);
            if (r.info) {
                const ImVec2 ts = ImGui::CalcTextSize(r.info);
                d->AddText(ImVec2(right - ts.x, cy - ts.y * 0.5f), C_VAL, r.info);
            }
            break;
        }
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, rowH * vis.size()));
}

static void ColorRow(const char* label, ImVec4* c) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = S(32);
    d->AddText(ImVec2(p.x, p.y + (h - ImGui::GetFontSize()) * 0.5f), C_TXT, label);

    ImGui::PushID(label);
    ImVec2 sw(p.x + w - S(30), p.y + (h - S(18)) * 0.5f);
    ImGui::SetCursorScreenPos(sw);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6));
    if (ImGui::ColorButton("##sw", *c, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, S2(30, 18)))
        ImGui::OpenPopup("##picker");
    ImGui::PopStyleVar();
    d->AddRect(sw, sw + S2(30, 18), Hex(0x34353c), S(6));

    if (ImGui::BeginPopup("##picker")) {
        ImGui::SetNextItemWidth(S(180));
        ImGui::ColorPicker3("##pk", (float*)c,
            ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_PickerHueBar);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
}

static void ColorsPopup(ImVec2 origin) {
    ImGui::SetNextWindowPos(origin + S2(96, 50));
    ImGui::SetNextWindowSize(ImVec2(S(240), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, S2(12, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(11));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, C_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, C_BORDER);

    if (ImGui::BeginPopup("##colors")) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        PF(fBold);
        ImGui::PushStyleColor(ImGuiCol_Text, C_WHITE);
        ImGui::TextUnformatted("Colors");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(S2(0, 4));

        PF(fMed);
        ColorRow("Menu",            &cfg::litvin::col_menu);
        ColorRow("Active tab text", &cfg::litvin::col_tabText);
        ColorRow("Toggles",         &cfg::litvin::col_toggle);
        ColorRow("Sliders",         &cfg::litvin::col_slider);
        ImGui::PopFont();

        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        d->AddLine(ImVec2(p.x, p.y + S(6)), ImVec2(p.x + w, p.y + S(6)), C_BORDER, 1.f);
        float y = p.y + S(16);
        static const unsigned presets[8] = { 0xe0262b, 0xff7a1a, 0xf2c230, 0x3ccf6e, 0x25c4d9, 0x3b82f6, 0x8b5cf6, 0xec4899 };
        for (int i = 0; i < 8; i++) {
            ImVec2 pos(p.x + S(23) * i, y);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(pos);
            if (ImGui::InvisibleButton("##pre", S2(17, 17))) {
                ImVec4 c = ImGui::ColorConvertU32ToFloat4(Hex(presets[i]));
                cfg::litvin::col_menu = cfg::litvin::col_toggle = cfg::litvin::col_slider = c;
            }
            float k = ImGui::IsItemActive() ? 0.9f : (ImGui::IsItemHovered() ? 1.15f : 1.f);
            ImVec2 c = pos + S2(8.5f, 8.5f);
            d->AddCircleFilled(c, S(8.5f) * k, Hex(presets[i]), 24);
            d->AddCircle(c, S(8.5f) * k - 1.f, IM_COL32(0, 0, 0, 64), 24, 2.f);
            ImGui::PopID();
        }
        PF(fSemi);
        const char* rs = "Reset";
        ImVec2 ts = ImGui::CalcTextSize(rs);
        ImVec2 rp(p.x + w - ts.x, y + (S(17) - ts.y) * 0.5f);
        ImGui::SetCursorScreenPos(rp);
        if (ImGui::InvisibleButton("##reset", ts)) {
            cfg::litvin::col_menu = ImVec4(0.878f, 0.149f, 0.169f, 1.f);
            cfg::litvin::col_tabText = ImVec4(1.f, 1.f, 1.f, 1.f);
            cfg::litvin::col_toggle = ImVec4(0.878f, 0.149f, 0.169f, 1.f);
            cfg::litvin::col_slider = ImVec4(0.878f, 0.149f, 0.169f, 1.f);
        }
        d->AddText(rp, ImGui::IsItemHovered() ? C_NAV_H : C_DIM, rs);
        ImGui::PopFont();

        // t.me/aether_co2
        PF(fSemi);
        const char* brand = "t.me/aether_co2";
        ImVec2 bs = ImGui::CalcTextSize(brand);
        ImVec2 bp(p.x, y + S(22));
        d->AddText(bp, C_DIM, brand);
        (void)bs;
        ImGui::PopFont();

        ImGui::SetCursorScreenPos(ImVec2(p.x, y));
        ImGui::Dummy(ImVec2(w, S(40)));
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

// ---------------------------------------------------------------- watermark
// t.me/aether_co2 — top-left, aether / powered by t.me/aether_co2
static ImVec2 wm_pos = ImVec2(15, 15);
static bool wm_dragging = false;
static ImVec2 wm_drag_delta = ImVec2(0, 0);
static ImVec2 wm_last_pos = ImVec2(15, 15);
static float wm_speed = 1.0f;

void watermark() {
    if (!cfg::litvin::watermark) return;
    if (bar::game_alpha() < 0.01f) return;

    const ImVec2 text_offset = ImVec2(13, 17);

    const char* hack_name = "aether";
    const char* hack_sub  = "powered by t.me/aether_co2";

    const ImVec2 username_size = ImGui::CalcTextSize(hack_sub);
    const ImVec2 hack_name_size = ImGui::CalcTextSize(hack_name);

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    if (screen_size.x <= 0) screen_size = ImVec2(g_sw, g_sh);

    ImVec2 watermark_size = ImVec2(
        username_size.x + hack_name_size.x + text_offset.x * 4,
        (username_size.y + text_offset.y) * 1.5f) + text_offset;

    wm_pos.x = ImClamp(wm_pos.x, 0.0f, ImMax(0.0f, screen_size.x - watermark_size.x));
    wm_pos.y = ImClamp(wm_pos.y, 0.0f, ImMax(0.0f, screen_size.y - watermark_size.y));

    const ImRect full_wm_bb(wm_pos, wm_pos + watermark_size);

    float split_x = full_wm_bb.Min.x + text_offset.x + hack_name_size.x + text_offset.x * 2;

    float rounding = ImGui::GetStyle().WindowRounding / 4.0f;
    if (rounding <= 0) rounding = 6.0f;

    // One solid dark color, never transparent (alpha forced to 255).
    draw->AddRectFilled(full_wm_bb.Min, full_wm_bb.Max, IM_COL32(0x16, 0x17, 0x1B, 255), rounding);
    draw->AddRect(full_wm_bb.Min, full_wm_bb.Max, IM_COL32(0x2A, 0x2B, 0x31, 255), rounding, 0, 1.f);

    float text_y_center = full_wm_bb.Min.y + (full_wm_bb.GetHeight() - username_size.y) / 2;
    draw->AddText(ImVec2(full_wm_bb.Min.x + text_offset.x + 5, text_y_center),
        IM_COL32(255, 255, 255, 255), hack_name);
    draw->AddText(ImVec2(split_x + text_offset.x, text_y_center),
        IM_COL32(255, 255, 255, 255), hack_sub);

    ImGui::SetNextWindowPos(full_wm_bb.Min);
    ImGui::SetNextWindowSize(full_wm_bb.GetSize());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##aether_watermark", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImVec2 drag_area_size = ImVec2(split_x - full_wm_bb.Min.x, full_wm_bb.GetHeight());
    ImGui::InvisibleButton("##aether_wm_drag", drag_area_size);

    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!wm_dragging) {
                wm_dragging = true;
                wm_drag_delta = ImGui::GetIO().MousePos - wm_pos;
                wm_last_pos = wm_pos;
            }
            ImVec2 target_pos = ImGui::GetIO().MousePos - wm_drag_delta;
            ImVec2 delta = target_pos - wm_last_pos;
            wm_pos = wm_last_pos + delta * wm_speed;
            wm_last_pos = wm_pos;
        } else {
            wm_dragging = false;
        }
    } else {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) wm_dragging = false;
    }

    ImGui::SameLine();
    ImVec2 click_area_size = ImVec2(full_wm_bb.Max.x - split_x, full_wm_bb.GetHeight());
    ImGui::SetCursorPos(ImVec2(drag_area_size.x, 0));
    if (ImGui::InvisibleButton("##aether_wm_btn", click_area_size)) {
        bar::toggle();
    }

    ImGui::End();
}

// ---------------------------------------------------------------- render
// t.me/aether_co2
void render() {
    bar::render();
    watermark();

    if (!fMed) {
        fMed = fontMedium ? fontMedium : ImGui::GetFont();
        fSemi = fontMedium ? fontMedium : ImGui::GetFont();
        fBold = fontBold ? fontBold : ImGui::GetFont();
    }

    if (g_sw > 0) {
        menu_scale = ImClamp(g_sw / 1350.f, 1.0f, 2.0f);
    } else {
        menu_scale = 1.5f;
    }

    float mdt = ImGui::GetIO().DeltaTime;
    if (!(mdt > 0.f) || mdt > 0.1f) mdt = 0.016f;

    // Open animation: window slides up on show (explicit colors everywhere,
    // so motion instead of alpha fade).
    static float openA = 0.f;
    if (!bar::g_open) { openA = 0.f; return; }
    if (bar::game_alpha() < 0.01f) { openA = 0.f; return; }
    openA += (1.f - openA) * ImMin(1.f, mdt * 9.f);
    if (openA > 0.999f) openA = 1.f;
    const float openE = 1.f - (1.f - openA) * (1.f - openA) * (1.f - openA);

    const ImVec2 size = S2(696, 448);
    ImVec2 center = ImVec2((g_sw - size.x) * 0.5f, (g_sh - size.y) * 0.5f);
    if (center.x < 0) center.x = 0;
    if (center.y < 0) center.y = 0;
    center.y += (1.f - openE) * S(26);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##aether_menu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);

    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    for (int i = 1; i <= 6; i++) {
        float e = S(3.f * (float)i);
        bg->AddRectFilled(o - ImVec2(e, e) + ImVec2(0, S(10)), o + size + ImVec2(e, e) + ImVec2(0, S(10)),
                          IM_COL32(0, 0, 0, 12), S(18) + e);
    }
    d->AddRectFilled(o, o + size, C_BG, S(18));
    d->AddRect(o, o + size, C_LINE, S(18), 0, 1.f);

    // ---------------- sidebar
    const ImVec2 sp = o + S2(9, 9);
    d->AddRectFilled(sp, sp + S2(77, 430), C_SIDE, S(12));
    DrawLogo(d, sp + S2(38.5f, 31), S(30));

    // t.me/aether_co2 brand under logo
    {
        const char* brand_top = "aether";
        ImVec2 ts = ImGui::CalcTextSize(brand_top);
        float bx = sp.x + (S(77) - ts.x) * 0.5f;
        d->AddText(ImVec2(bx, sp.y + S(50)), C_WHITE, brand_top);
        const char* brand_sub = "t.me/aether_co2";
        ImVec2 ts2 = ImGui::CalcTextSize(brand_sub);
        float scale_fix = menu_scale > 1.4f ? 1.0f : 0.85f;
        (void)scale_fix;
        float bx2 = sp.x + (S(77) - ts2.x) * 0.5f;
        // clip to sidebar
        d->PushClipRect(sp, sp + S2(77, 80), true);
        d->AddText(ImVec2(bx2, sp.y + S(64)), C_DIM, brand_sub);
        d->PopClipRect();
    }

    auto& secs = Sections();
    static const float navY[5] = { 79, 151, 222, 294, 364 };
    for (int i = 0; i < (int)secs.size(); i++) {
        ImVec2 bp = sp + S2(0, navY[i]);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(bp);
        if (ImGui::InvisibleButton("##nav", S2(77, 44)) && g_cur != i) { g_cur = i; g_query[0] = 0; }
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        float h = Anim(ImGui::GetID("##navh"), hov ? 1.f : 0.f);
        float a = Anim(ImGui::GetID("##nava"), g_cur == i ? 1.f : 0.f);
        ImU32 col = Mix(Mix(C_NAV, C_NAV_H, h), U(cfg::litvin::col_menu), a);
        DrawIcon(d, secs[i].icon, bp + S2(38.5f, 22), S(24) * (act ? 0.9f : 1.f), col, S(1.7f));
        if (a > 0.01f)
            d->AddRectFilled(bp + S2(3, 12), bp + S2(5, 32), WithAlpha(U(cfg::litvin::col_menu), a), S(1));
        ImGui::PopID();
    }

    // ---------------- top bar
    bool popOpen = ImGui::IsPopupOpen("##colors");
    bool gearPressed = IconButton("##gear", o + S2(96, 9), S2(35, 35), I_GEAR, C_DIM, S(1.8f), popOpen);
    if (ImGui::IsItemActivated()) g_gearWasOpen = popOpen;
    if (gearPressed && !g_gearWasOpen) ImGui::OpenPopup("##colors");

    {   // search
        ImVec2 sb = o + S2(139, 9), ss = S2(461, 35);
        d->AddRectFilled(sb, sb + ss, C_BTN, S(9));
        DrawIcon(d, I_SEARCH, sb + ImVec2(S(18.5f), ss.y * 0.5f), S(19), C_TXT, S(2));
        PF(fMed);
        float fh = ImGui::GetFontSize();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (ss.y - fh) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,           C_INPUT);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled,   C_TXT);
        ImGui::SetCursorScreenPos(sb + ImVec2(S(36), 0));
        ImGui::SetNextItemWidth(ss.x - S(45));
        ImGui::InputTextWithHint("##search", "Search type...", g_query, sizeof g_query);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
    }

    if (IconButton("##folder", o + S2(609, 9), S2(35, 35), I_FOLDER, C_TXT, S(1.8f))) {
        // t.me/aether_co2 — folder reserved
    }
    if (IconButton("##close",  o + S2(653, 9), S2(35, 35), I_CLOSE, Hex(0xa8aab0), S(2))) {
        bar::set_open(false);
    }

    // ---------------- tabs
    Section& sec = secs[g_cur];
    for (int i = 0; i < (int)sec.tabs.size(); i++) {
        ImVec2 tp = o + S2(96 + 139.f * (float)i, 52), ts = S2(130, 35);
        ImGui::PushID(100 + i);
        ImGui::SetCursorScreenPos(tp);
        if (ImGui::InvisibleButton("##tab", ts)) g_tab[g_cur] = i;
        bool on = g_tab[g_cur] == i;
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        float a = Anim(ImGui::GetID("##ta"), on ? 1.f : 0.f);
        float h = Anim(ImGui::GetID("##th"), (hov && !on) ? 1.f : 0.f);
        float sc = act ? 0.97f : 1.f;
        ImVec2 c = tp + ts * 0.5f, half = ts * 0.5f * sc;
        d->AddRectFilled(c - half, c + half, Mix(Mix(C_TAB, C_BTN, h), C_BTN, a), S(9));
        PF(on ? fBold : fSemi);
        const char* name = sec.tabs[i].name;
        ImVec2 tsz = ImGui::CalcTextSize(name);
        d->AddText(c - tsz * 0.5f, Mix(Mix(C_TXT, C_TXT_H, h), U(cfg::litvin::col_tabText), a), name);
        ImGui::PopFont();
        ImGui::PopID();
    }

    // ---------------- content (slides in on tab switch)
    static int lastSec = -1, lastTab = -1;
    static float contA = 1.f;
    if (g_cur != lastSec || g_tab[g_cur] != lastTab) {
        contA = 0.f; lastSec = g_cur; lastTab = g_tab[g_cur];
    }
    contA += (1.f - contA) * ImMin(1.f, mdt * 12.f);
    if (contA > 0.999f) contA = 1.f;
    const float contE = 1.f - (1.f - contA) * (1.f - contA);
    ImGui::SetCursorScreenPos(o + S2(96, 99) + ImVec2((1.f - contE) * S(14), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##panel", S2(591, 340), false, ImGuiWindowFlags_NoBackground);
    PF(fMed);
    DrawRows(sec.tabs[g_tab[g_cur]].rows);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // footer brand t.me/aether_co2
    {
        const char* fb = "t.me/aether_co2";
        ImVec2 fbs = ImGui::CalcTextSize(fb);
        ImVec2 fp(o.x + size.x - fbs.x - S(12), o.y + size.y - fbs.y - S(6));
        d->AddText(fp, C_DIM, fb);
    }

    ColorsPopup(o);
    ImGui::End();
}

} // namespace ui::menu
