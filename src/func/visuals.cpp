#include "visuals.hpp"
#include "ground_pulse.hpp"
#include "tracer.hpp"
#include "../combat/combat.hpp"
#include "../input/inject.hpp"
#include "../game/game.hpp"
#include "../game/math.hpp"
#include "../game/player.hpp"
#include "../ui/theme/theme.hpp"
#include "../ui/cfg.hpp"
#include "../protect/oxorany.hpp"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

extern ImFont* espFont;

static void esp_wait(const char* stage) {
    static long long last = 0;
    if (diag::every(last, 5000)) diag::log("WAIT", "ESP blocked at: %s", stage);
}

void visuals::draw() {
    bool any = cfg::esp::box || cfg::esp::name || cfg::esp::health || cfg::esp::distance ||
               cfg::esp::snaplines || cfg::pulse::enable ||
               cfg::combat::aim_enable || cfg::combat::fire_enable ||
               cfg::combat::aim_draw_fov || cfg::combat::fire_draw_zone ||
               cfg::fx::tracers || cfg::fx::hitmarker || cfg::fx::trail;
    if (!any) { pulse::clear(); esp_wait("all ESP options disabled"); return; }

    std::vector<uint64_t> Players = players();
    if (Players.empty()) { esp_wait("player discovery / registry scan"); return; }
    struct name_cache_entry { uint32_t id{}; std::string name; long long refreshed{}; };
    static std::unordered_map<uint64_t, name_cache_entry> name_cache;
    static size_t name_cursor = 0;
    // Drop names of objects that left the match (address reuse would show a
    // stranger's nick on a new player otherwise).
    if (name_cache.size() > 24) {
        for (auto it = name_cache.begin(); it != name_cache.end();) {
            if (std::find(Players.begin(), Players.end(), it->first) == Players.end())
                it = name_cache.erase(it);
            else ++it;
        }
    }
    // Per-object combat state (HP baseline for hit markers, visibility stamps,
    // acquire timers). Same prune policy as names.
    static std::unordered_map<uint64_t, int> hpPrev;
    static std::unordered_map<uint64_t, long long> lastVis;
    static std::unordered_map<uint64_t, double> acquireT;
    // Hit attribution state (previous frame's engagement): a marker fires
    // only for OUR target shortly after OUR fire — never for random damage.
    static double g_lastFireT = -1e9;
    static uint64_t g_lastFireObj = 0, g_lastAimObj = 0;
    static bool g_aimWasReady = false;
    if (hpPrev.size() > 32) {
        auto prune = [&](auto& m) {
            for (auto it = m.begin(); it != m.end();) {
                if (std::find(Players.begin(), Players.end(), it->first) == Players.end())
                    it = m.erase(it);
                else ++it;
            }
        };
        prune(hpPrev); prune(lastVis); prune(acquireT);
    }
    trail::prune(Players);
    // Resolve at most one name per frame. Name discovery touches several
    // component objects, so batching all ten players caused visible UI stalls.
    {
        const uint64_t object = Players[name_cursor++ % Players.size()];
        const uint32_t id = rpm<uint32_t>(object + v100::net_object_id);
        name_cache_entry& entry = name_cache[object];
        const long long now = diag::now_ms();
        if (entry.id != id || now - entry.refreshed > 5000) {
            entry.id = id;
            entry.name = player_name(object);
            entry.refreshed = now;
        }
    }
    static long long object_log = 0;
    if (diag::every(object_log, 10000)) {
        for (size_t i = 0; i < Players.size(); ++i) {
            const uint64_t object = Players[i];
            const Vector3 position = player::position(object);
            std::string name_text = name_cache[object].name;
            if (name_text.empty()) name_text = "<name unavailable>";
            diag::log("PLAYER", "[%zu] object=0x%llx id=%u name=%s local=%d team=%d hp=%d pos=(%.3f %.3f %.3f)",
                      i, (unsigned long long)object, rpm<uint32_t>(object + v100::net_object_id),
                      name_text.c_str(), player::local(object) ? 1 : 0, player::team(object),
                      player::health(object), position.x, position.y, position.z);
        }
    }
    uint64_t LocalPlayer = 0;
    for (uint64_t p : Players) if (player::local(p)) { LocalPlayer = p; break; }
    if (!LocalPlayer) { esp_wait("local player flag"); return; }
    Vector3 LocalPosition = player::position(LocalPlayer);
    int LocalTeam = player::team(LocalPlayer);
    if (LocalTeam != 1 && LocalTeam != 2) { esp_wait("local team validation"); return; }
    if (!std::isfinite(LocalPosition.x) || !std::isfinite(LocalPosition.y) || !std::isfinite(LocalPosition.z) ||
        (LocalPosition.x == 0.f && LocalPosition.y == 0.f && LocalPosition.z == 0.f)) {
        esp_wait("local transform hierarchy"); return;
    }
    int enemies = 0, alive = 0, positioned = 0, projected = 0;
    struct enemy_frame {
        uint64_t object{};
        Vector3 position{};
        int health{};
        float distance{};
    };
    std::vector<enemy_frame> frame_enemies;
    const double nowT0 = ImGui::GetTime();
    for (uint64_t Player : Players) {
        if (!Player || Player == LocalPlayer) continue;
        int PlayerTeam = player::team(Player);
        if (PlayerTeam == LocalTeam || (PlayerTeam != 1 && PlayerTeam != 2)) continue;
        ++enemies;

        Vector3 PlayerPosition = player::position(Player);
        if (PlayerPosition.x == 0.f && PlayerPosition.y == 0.f && PlayerPosition.z == 0.f) continue;
        ++positioned;

        // Tri-state HP: an unreadable value is UNKNOWN, not death.
        // Only a confirmed 0 evicts; failures skip the frame with caches intact.
        bool hpOk = false;
        const int Health = player::health_ex(Player, hpOk);
        if (!hpOk) continue;
        // Hit-marker feed: any observed HP drop on a recently-visible enemy.
        // First sighting only sets the baseline (no false trigger).
        {
            auto it = hpPrev.find(Player);
            if (it == hpPrev.end()) {
                hpPrev[Player] = Health;
            } else if (Health < it->second) {
                const int drop = it->second - Health;
                const long long nowMs = diag::now_ms();
                const auto lv = lastVis.find(Player);
                const long long lastSeen = (lv == lastVis.end()) ? 0 : lv->second;
                // Attribution: only OUR damage counts — the damaged object must
                // be our recent fire/aim target. Teammate/world damage is mute.
                const bool inWin = (nowMs - lastSeen) < 500;
                const bool ownFire = (nowT0 - g_lastFireT) < 0.7 && Player == g_lastFireObj;
                const bool ownAim = g_aimWasReady && Player == g_lastAimObj;
                if (cfg::fx::hitmarker && inWin && (ownFire || ownAim))
                    hitmark::notify(drop);
                it->second = Health;
            } else if (Health != it->second) {
                it->second = Health;
            }
        }
        // Instant eviction on death: without it the render cache could keep
        // resurrecting this object for ~0.5s after the registry drops it,
        // freezing a box at the corpse. Re-added automatically on respawn.
        if (Health <= 0) {
            drop_player_cache(Player); name_cache.erase(Player);
            hpPrev.erase(Player); lastVis.erase(Player); acquireT.erase(Player);
            continue;
        }
        ++alive;

        float Distance = calculate_distance(PlayerPosition, LocalPosition);
        if (Distance > 500.f) continue;

        frame_enemies.push_back({Player, PlayerPosition, Health, Distance});
    }

    // Do not trust the first camera-shaped native object. Unity keeps scene,
    // UI, preview and stale Camera instances alive together. Score every
    // validated candidate against the live player coordinates and select the
    // one whose view/projection pair actually places bodies on this display.
    //
    // Scope/ADS note: aiming swaps the gameplay camera to a zoomed one (its
    // projection FOV narrows). The generated fallback candidate must reuse
    // the FOV observed on the live native camera — a hardcoded 60° projection
    // is what used to shift every box/bar/label while scoped.
    struct cam_score { int score = 0; int front = 0; int onscreen = 0; float dist = 1000000.f; };
    auto score_camera = [&](const camera_candidate& Camera, cam_score& out) {
        out = cam_score{};
        const float* view = reinterpret_cast<const float*>(&Camera.view);
        const Vector3 camera_position(
            -(view[0] * view[12] + view[1] * view[13] + view[2] * view[14]),
            -(view[4] * view[12] + view[5] * view[13] + view[6] * view[14]),
            -(view[8] * view[12] + view[9] * view[13] + view[10] * view[14]));
        const float camera_distance = calculate_distance(camera_position, LocalPosition);
        int score = 0, front = 0, onscreen = 0;
        if (camera_distance < 2.f) score += 100;
        else if (camera_distance < 10.f) score += 80;
        else if (camera_distance < 30.f) score += 30;
        const float* projection = reinterpret_cast<const float*>(&Camera.projection);
        if (std::fabs(projection[11]) > .5f && std::fabs(projection[15]) < .1f) score += 30;
        for (const auto& Enemy : frame_enemies) {
            Vector3 Head(Enemy.position.x, Enemy.position.y + 1.67f, Enemy.position.z);
            ImVec2 screen_head, screen_foot;
            if (!world_to_screen(Head, Camera.view, Camera.projection, screen_head) ||
                !world_to_screen(Enemy.position, Camera.view, Camera.projection, screen_foot)) continue;
            if (!std::isfinite(screen_head.x) || !std::isfinite(screen_head.y) ||
                !std::isfinite(screen_foot.x) || !std::isfinite(screen_foot.y)) continue;
            ++front;
            ++score;
            const bool head_inside = screen_head.x >= -g_sw * .25f && screen_head.x <= g_sw * 1.25f &&
                                     screen_head.y >= -g_sh * .25f && screen_head.y <= g_sh * 1.25f;
            const bool foot_inside = screen_foot.x >= -g_sw * .25f && screen_foot.x <= g_sw * 1.25f &&
                                     screen_foot.y >= -g_sh * .25f && screen_foot.y <= g_sh * 1.25f;
            const float body_height = std::fabs(screen_foot.y - screen_head.y);
            if (head_inside && foot_inside) { ++onscreen; score += 6; }
            if (body_height >= 2.f && body_height <= g_sh * 1.5f) score += 3;
        }
        out.score = score; out.front = front; out.onscreen = onscreen; out.dist = camera_distance;
    };

    std::vector<camera_candidate> Cameras = camera_candidates(LocalPlayer);

    // Pass 1 — observe the live vertical FOV on native cameras (scope detect).
    // Unity perspective: proj[5] = 1/tan(fov/2), so fov = 2*atan(1/proj[5]).
    // Scores are kept and reused in the final pass: each candidate is
    // projected exactly once per frame (optimization).
    static float lastGoodFov = 60.f;
    static float prevScopeFov = 60.f;
    std::vector<cam_score> nativeScores;
    bool fovJump = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        nativeScores.assign(Cameras.size(), cam_score{});
        int bestNative = -1;
        const matrix* bestProj = nullptr;
        for (size_t ni = 0; ni < Cameras.size(); ++ni) {
            score_camera(Cameras[ni], nativeScores[ni]);
            if (nativeScores[ni].score > bestNative) {
                bestNative = nativeScores[ni].score;
                bestProj = &Cameras[ni].projection;
            }
        }
        if (bestProj) {
            const float p55 = reinterpret_cast<const float*>(bestProj)[5];
            if (std::isfinite(p55) && p55 > 0.05f) {
                const float fov = 2.f * atanf(1.f / p55) * 57.29578f;
                if (fov > 5.f && fov < 150.f) lastGoodFov = fov;
            }
        }
        fovJump = std::fabs(lastGoodFov - prevScopeFov) > 8.f; // scope in/out
        if (fovJump && attempt == 0) {
            // A new scope camera object may exist RIGHT NOW; the 750ms
            // discovery throttle would hide it and pin ESP to stale glass.
            g_camera_discover_ms = 0;
            Cameras = camera_candidates(LocalPlayer);
            continue;
        }
        break;
    }
    static long long scope_log = 0;
    if (fovJump && diag::every(scope_log, 1000))
        diag::log("CAMERA", "FOV jump %.1f -> %.1f (scope change), forcing reselect",
                  (double)prevScopeFov, (double)lastGoodFov);
    prevScopeFov = lastGoodFov;

    // Hip-fire baseline for zoom-compensated zones: a fixed pixel radius
    // covers a much smaller world angle when scoped, so the zone (and target
    // selection inside it) is scaled to keep a constant world angle.
    static float hipFov = 60.f;
    if (lastGoodFov > 45.f) hipFov = lastGoodFov;
    const float zoomScale = std::clamp(
        tanf(hipFov * 0.00872665f) / tanf(lastGoodFov * 0.00872665f), 0.5f, 8.f);

    // In 1.0.0 the gameplay Camera is no longer reachable through the
    // Transform/GameObject component list. The local ios controller still
    // owns the live camera transforms, so build a view matrix from their
    // hierarchy instead of falling back to a multi-gigabyte heap scan.
    // The generated projection reuses the observed FOV so ESP stays correct
    // while scoped; FOV 60 here used to be the scope-shift bug.
    std::vector<camera_candidate> TransformCameras =
        player::camera_fallback_candidates(LocalPlayer, lastGoodFov);
    const size_t nNative = Cameras.size();
    Cameras.insert(Cameras.end(), TransformCameras.begin(), TransformCameras.end());
    if (Cameras.empty()) { esp_wait("native / transform camera resolution"); return; }
    // Score ONLY the generated newcomers here; native scores come from pass 1.
    std::vector<cam_score> fbScores;
    fbScores.reserve(TransformCameras.size());
    for (size_t i = nNative; i < Cameras.size(); ++i) {
        cam_score s; score_camera(Cameras[i], s);
        fbScores.push_back(s);
    }

    matrix ViewMatrix{}, ProjectionMatrix{};
    uint64_t selected_camera = 0;
    int selected_score = -1, selected_front = 0, selected_onscreen = 0;
    float selected_camera_distance = 1000000.f;
    bool selected_is_fallback = true;
    static long long candidate_log = 0;
    const bool log_candidates = diag::every(candidate_log, 5000);
    for (size_t ci = 0; ci < Cameras.size(); ++ci) {
        const camera_candidate& Camera = Cameras[ci];
        const cam_score& cs = (ci < nNative) ? nativeScores[ci] : fbScores[ci - nNative];
        const int score = cs.score;
        // Tie policy: a native camera always beats a generated one on equal
        // score (the generated projection is only an approximation), and the
        // previous winner is kept on equal scores to avoid flicker — except
        // on a scope FOV jump, where the old winner is by definition wrong.
        const bool tiePreferNative = (score == selected_score && selected_camera != 0 &&
                                      !Camera.generated && selected_is_fallback);
        const bool tieSticky = (score == selected_score && !fovJump &&
                                Camera.address == g_camera &&
                                Camera.generated == selected_is_fallback);
        if (score > selected_score || tiePreferNative || tieSticky) {
            selected_score = score;
            selected_front = cs.front;
            selected_onscreen = cs.onscreen;
            selected_camera_distance = cs.dist;
            selected_camera = Camera.address;
            selected_is_fallback = Camera.generated;
            ViewMatrix = Camera.view;
            ProjectionMatrix = Camera.projection;
        }
        if (log_candidates)
            diag::log("CAMERA", "candidate=0x%llx %s score=%d front=%d onscreen=%d localDistance=%.2f proj=(%.3f %.3f)",
                      (unsigned long long)Camera.address, Camera.generated ? "gen" : "nat",
                      score, cs.front, cs.onscreen, cs.dist,
                      Camera.projection.m11, Camera.projection.m22);
    }
    if (!selected_camera || (selected_camera_distance >= 30.f && selected_onscreen == 0)) {
        g_camera = 0;
        esp_wait("camera candidates do not project players");
        return;
    }
    if (g_camera != selected_camera) {
        diag::log("CAMERA", "selected=0x%llx score=%d front=%d onscreen=%d localDistance=%.2f candidates=%zu",
                  (unsigned long long)selected_camera, selected_score, selected_front,
                  selected_onscreen, selected_camera_distance, Cameras.size());
    }
    g_camera = selected_camera;

    std::vector<combat::Candidate> cands;
    cands.reserve(frame_enemies.size());
    for (const auto& Enemy : frame_enemies) {
        const uint64_t Player = Enemy.object;
        const Vector3 PlayerPosition = Enemy.position;
        const int Health = Enemy.health;
        const float Distance = Enemy.distance;

        Vector3 HeadPosition(PlayerPosition.x, PlayerPosition.y + 1.67f, PlayerPosition.z);

        ImVec2 ScreenHead, ScreenFoot;
        bool HeadVisible = world_to_screen(HeadPosition, ViewMatrix, ProjectionMatrix, ScreenHead);
        bool FootVisible = world_to_screen(PlayerPosition, ViewMatrix, ProjectionMatrix, ScreenFoot);

        // Ground Pulse ESP: expanding rings under feet, follows enemy via
        // NetObject position (offsets v100::*, dump.cs /sdcard/bogdlc).
        // Runs on the already-resolved frame enemies + selected camera:
        // no extra memory reads here, only projection math.
        if (cfg::pulse::enable && FootVisible) {
            pulse::draw_pulse(Player, PlayerPosition, Distance, ViewMatrix, ProjectionMatrix, 1.f);
        }

        if (!HeadVisible || !FootVisible) continue;
        ++projected;
        // Strict render bounds: scoring keeps wide margins to FIND the right
        // camera, but painting far-offscreen boxes is waste + edge garbage.
        if (ScreenHead.x < -g_sw * 0.05f || ScreenHead.x > g_sw * 1.05f ||
            ScreenHead.y < -g_sh * 0.05f || ScreenHead.y > g_sh * 1.05f ||
            ScreenFoot.x < -g_sw * 0.05f || ScreenFoot.x > g_sw * 1.05f ||
            ScreenFoot.y < -g_sh * 0.05f || ScreenFoot.y > g_sh * 1.05f) continue;
        lastVis[Player] = diag::now_ms();

        // Combat feed: projected enemies become target candidates.
        {
            const Vector3 chestW(PlayerPosition.x, PlayerPosition.y + 1.15f, PlayerPosition.z);
            ImVec2 chestS;
            const bool chestOk = world_to_screen(chestW, ViewMatrix, ProjectionMatrix, chestS);
            combat::Candidate cd;
            cd.object = Player;
            cd.head = ScreenHead;
            cd.chest = chestOk ? chestS : ScreenFoot;
            cd.hp = Health;
            cd.dist = Distance;
            cd.visible = true; // inside frustum with margin (see world_to_screen bounds)
            cands.push_back(cd);
            if (cfg::fx::trail) trail::push(Player, ScreenFoot);
        }

        float x1 = roundf(ScreenHead.x);
        float y1 = roundf(fminf(ScreenHead.y, ScreenFoot.y));
        float x2 = roundf(ScreenFoot.x);
        float y2 = roundf(fmaxf(ScreenHead.y, ScreenFoot.y));

        float bh = fabsf(y2 - y1);
        float bw = roundf(bh * 0.25f);
        float cx = roundf((x1 + x2) * 0.5f);

        ImVec2 BoxMin(cx - bw, y1);
        ImVec2 BoxMax(cx + bw, y2);

        float inv_dist = 1.f / (Distance + 0.1f);
        float font_sz = std::clamp(400.f * inv_dist, 10.f, 18.f);

        if (cfg::esp::box) {
            dbox(BoxMin, BoxMax, 1.f);
        }

        if (cfg::esp::snaplines) {
            dsnapline(ScreenFoot, 1.f);
        }

        if (cfg::esp::health) {
            dhp(Health, BoxMin, BoxMax, bh, 1.f);
        }

        if (cfg::esp::name) {
            const auto found = name_cache.find(Player);
            const char* label = found != name_cache.end() && !found->second.name.empty()
                ? found->second.name.c_str() : "Enemy";
            dnick(label, BoxMax, cx, font_sz, 1.f);
        }

        if (cfg::esp::distance) {
            ddist(Distance, BoxMax.x, BoxMin.y, font_sz, 1.f);
        }
    }

    // ---- combat: zones, target selection, tracers ----
    // Selection + timing + visuals are real and run on live ESP data.
    // Actuation (moving the crosshair / tapping fire) needs an input path
    // this external overlay does not have, so aimbot/autofire stop at the
    // "firing solution" stage: chosen target, smoothed point, fire timing.
    {
        const ImVec2 cross(g_sw * 0.5f, g_sh * 0.5f);
        const double nowT = ImGui::GetTime();
        static double lastNowT = 0.0;
        float hdt = (lastNowT > 0.0) ? (float)(nowT - lastNowT) : 0.016f;
        lastNowT = nowT;
        if (!(hdt > 0.f) || hdt > 0.1f) hdt = 0.016f;

        if (cfg::combat::aim_draw_fov)
            tracer::draw_zone(cross, (float)cfg::combat::aim_fov * zoomScale, IM_COL32(255, 255, 255, 160));
        if (cfg::combat::fire_draw_zone)
            tracer::draw_zone(cross, (float)cfg::combat::fire_fov * zoomScale, IM_COL32(255, 80, 80, 160));

        const bool effAim = combat::binds::aim_active();
        const bool effFire = combat::binds::fire_active();
        const float aimZonePx = (float)cfg::combat::aim_fov * zoomScale;
        const float fireZonePx = (float)cfg::combat::fire_fov * zoomScale;
        // ESP Line / beam origin = your gun on screen (calibrated in menu).
        const ImVec2 muzzle(std::clamp(cfg::fx::muzzle_x / 100.f, 0.f, 1.f) * g_sw,
                            std::clamp(cfg::fx::muzzle_y / 100.f, 0.f, 1.f) * g_sh);

        combat::Pick aimPick, firePick;
        if (effAim && !cands.empty())
            aimPick = combat::select_target(cands.data(), (int)cands.size(), cross,
                aimZonePx, cfg::combat::aim_target,
                cfg::combat::aim_bone, cfg::combat::visible_check);
        if (effFire && !cands.empty())
            firePick = combat::select_target(cands.data(), (int)cands.size(), cross,
                fireZonePx, 0, 1, cfg::combat::visible_check);

        // Acquire timing: target must sit in its zone for delay ms.
        static uint64_t aimObj = 0, fireObj = 0;
        static combat::HumanState humanSt;
        static double aimSince = 0.0, fireSince = 0.0;
        if (!aimPick.ok || aimPick.object != aimObj) {
            aimObj = aimPick.ok ? aimPick.object : 0;
            combat::HumanState::reset(humanSt);
        }
        if (!firePick.ok || firePick.object != fireObj)
            fireObj = firePick.ok ? firePick.object : 0;
        // Restart the acquire clock for anything we are NOT holding now:
        // otherwise a lost-then-reacquired target inherits an expired delay
        // and fires/aims instantly.
        for (auto it = acquireT.begin(); it != acquireT.end();) {
            if (it->first != aimObj && it->first != fireObj) it = acquireT.erase(it);
            else ++it;
        }
        if (aimPick.ok) {
            auto it = acquireT.find(aimPick.object);
            if (it == acquireT.end()) { acquireT[aimPick.object] = nowT; aimSince = nowT; }
            else aimSince = it->second;
        }
        if (firePick.ok) {
            auto it = acquireT.find(firePick.object);
            if (it == acquireT.end()) { acquireT[firePick.object] = nowT; fireSince = nowT; }
            else fireSince = it->second;
        }
        const bool aimReady = aimPick.ok &&
            (nowT - aimSince) * 1000.0 >= cfg::combat::aim_delay;
        const bool firing = firePick.ok &&
            (nowT - fireSince) * 1000.0 >= cfg::combat::fire_delay;

        // Beam lifetime starts at the actual fire moment (rising edge), not at
        // target acquisition: with delay > beam life the old code drew a
        // long-dead effect (i.e. nothing) exactly when firing began.
        static double fireBeamT = -1e9;
        static uint64_t fireBeamObj = 0;
        static bool wasFiring = false;
        if (firing && firePick.ok && (!wasFiring || firePick.object != fireBeamObj)) {
            fireBeamT = nowT;
            fireBeamObj = firePick.object;
        }
        wasFiring = firing && firePick.ok;

        // Aim diamond on the (humanized, if enabled) aim point.
        if (aimPick.ok && aimReady) {
            ImVec2 pt = aimPick.point;
            if (cfg::combat::humanizer) {
                const ImVec2 raw(aimPick.point.x - cross.x, aimPick.point.y - cross.y);
                const ImVec2 sm = combat::humanize_step(humanSt, raw,
                    cfg::combat::aim_smooth, cfg::combat::human_amt, hdt, (float)nowT);
                pt = ImVec2(cross.x + sm.x, cross.y + sm.y);
            }
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const ImU32 dc = IM_COL32(255, 255, 255, 220);
            const float r = 7.f;
            dl->AddLine(ImVec2(pt.x, pt.y - r), ImVec2(pt.x + r, pt.y), dc, 1.5f);
            dl->AddLine(ImVec2(pt.x + r, pt.y), ImVec2(pt.x, pt.y + r), dc, 1.5f);
            dl->AddLine(ImVec2(pt.x, pt.y + r), ImVec2(pt.x - r, pt.y), dc, 1.5f);
            dl->AddLine(ImVec2(pt.x - r, pt.y), ImVec2(pt.x, pt.y - r), dc, 1.5f);
        }

        // ESP Line: muzzle (your gun) -> every visible enemy chest.
        // The engaged fire target gets the full beam effect instead.
        if (cfg::fx::tracers && !cands.empty()) {
            int tidx = cfg::fx::tracer_col;
            if (tidx < 0 || tidx > 5) tidx = 0;
            const ImVec4 tc = visuals::esp_preset_color(tidx);
            ImDrawList* edl = ImGui::GetBackgroundDrawList();
            for (const auto& cd : cands) {
                if (firing && firePick.ok && cd.object == firePick.object) {
                    tracer::draw_beam(muzzle, firePick.point, fireBeamT, nowT);
                } else {
                    edl->AddLine(muzzle, cd.chest,
                        IM_COL32((int)(tc.x * 255), (int)(tc.y * 255), (int)(tc.z * 255), 115), 1.5f);
                }
            }
        }

        // Hit attribution feed for the next frames (see HP poll above).
        g_lastAimObj = aimPick.ok ? aimPick.object : 0;
        g_aimWasReady = aimReady;
        if (firing && firePick.ok) {
            g_lastFireT = nowT;
            g_lastFireObj = firePick.object;
        }

        // Physical fire tap (root, /dev/input) off the render thread.
        // Throttled to 5 taps/sec max; coordinates from Fire X/Y % in menu.
        inject::set_screen(g_sw, g_sh);
        if (firing && firePick.ok && cfg::combat::tap_fire) {
            static double lastTapT = -1e9;
            static long long tapWarn = 0;
            if (nowT - lastTapT >= 0.20) {
                lastTapT = nowT;
                float fx = std::clamp(cfg::combat::fire_x_pct / 100.f, 0.f, 1.f) * g_sw;
                float fy = std::clamp(cfg::combat::fire_y_pct / 100.f, 0.f, 1.f) * g_sh;
                if (!inject::request_tap(fx, fy) && diag::every(tapWarn, 5000))
                    diag::log("WARN", "tap-fire: input thread unavailable");
            }
        }

        // Anti-recoil: while autofire engages, drag down in the look area to
        // counter muzzle climb. Experimental: rate/direction depend on your
        // sensitivity + invert settings — tune Power/Look pos on device.
        // True zero-recoil would need live weapon-state offsets (not in the
        // static dump); this is pull-down compensation, not memory editing.
        if (firing && cfg::combat::antirecoil && cfg::combat::recoil_power > 0) {
            static double lastSwipeT = -1e9;
            if (nowT - lastSwipeT >= 0.12) {
                lastSwipeT = nowT;
                const float lx = std::clamp(cfg::combat::look_x_pct / 100.f, 0.f, 1.f) * g_sw;
                const float ly = std::clamp(cfg::combat::look_y_pct / 100.f, 0.f, 1.f) * g_sh;
                const float p = (float)cfg::combat::recoil_power;
                inject::request_swipe(lx, ly - p * 0.5f, lx, ly + p * 0.5f);
            }
        }
    }
    trail::draw();
    hitmark::draw();

    static long long frame_log = 0;
    pulse::sweep(diag::now_ms());
    if (diag::every(frame_log, 5000)) {
        diag::log(projected ? "INFO" : "WARN",
                  "ESP frame players=%zu local=0x%llx team=%d pos=(%.2f %.2f %.2f) enemies=%d positioned=%d alive=%d projected=%d camera=0x%llx",
                  Players.size(), (unsigned long long)LocalPlayer, LocalTeam,
                  LocalPosition.x, LocalPosition.y, LocalPosition.z, enemies, positioned, alive,
                  projected, (unsigned long long)g_camera);
    }
}

void visuals::dbox(const ImVec2& t, const ImVec2& b, float a) {
    if (a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    float x1 = t.x, y1 = t.y, x2 = b.x, y2 = b.y;
    float r = cfg::esp::box_rounding;

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::box_col.x * 255),
        static_cast<int>(cfg::esp::box_col.y * 255),
        static_cast<int>(cfg::esp::box_col.z * 255),
        static_cast<int>(cfg::esp::box_col.w * 255 * a)
    );

    if (cfg::esp::box_type == 0) {
        dl->AddRect(ImVec2(x1 + 2.f, y1 + 2.f), ImVec2(x2 + 2.f, y2 + 2.f), IM_COL32(0, 0, 0, (int)(100 * a)), r, 0, 2.f);
        dl->AddRect(ImVec2(x1 - 1.f, y1 - 1.f), ImVec2(x2 + 1.f, y2 + 1.f), IM_COL32(0, 0, 0, (int)(180 * a)), r, 0, 1.f);
        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), col, r, 0, 1.f);
    } else {
        float w = x2 - x1, h = y2 - y1;
        float sz = std::min(w, h) * 0.25f;
        float cr = std::min(r, sz * 0.5f);

        ImU32 s1 = IM_COL32(0, 0, 0, (int)(100 * a));
        ImU32 s2 = IM_COL32(0, 0, 0, (int)(180 * a));

        auto corner = [&](float cx, float cy, float dx, float dy) {
            if (cr > 0.5f) {
                float arc_cx = cx + dx * cr;
                float arc_cy = cy + dy * cr;

                float angle_start, angle_end;
                if (dx > 0 && dy > 0) { angle_start = 3.14159265f; angle_end = 4.71238898f; }
                else if (dx < 0 && dy > 0) { angle_start = 4.71238898f; angle_end = 6.28318530f; }
                else if (dx > 0 && dy < 0) { angle_start = 1.57079632f; angle_end = 3.14159265f; }
                else { angle_start = 0.f; angle_end = 1.57079632f; }

                dl->PathArcTo(ImVec2(arc_cx + 2.f * (dx > 0 ? 1 : -1), arc_cy + 2.f * (dy > 0 ? 1 : -1)), cr, angle_start, angle_end, 8);
                dl->PathStroke(s1, 0, 2.f);
                dl->AddLine(ImVec2(cx + dx * cr, cy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(cx + dx * sz, cy + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);
                dl->AddLine(ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + dy * cr), ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + dy * sz), s1, 2.f);

                dl->PathArcTo(ImVec2(arc_cx, arc_cy), cr, angle_start, angle_end, 8);
                dl->PathStroke(s2, 0, 1.f);
                dl->AddLine(ImVec2(cx + dx * cr, cy), ImVec2(cx + dx * sz, cy), s2, 1.f);
                dl->AddLine(ImVec2(cx, cy + dy * cr), ImVec2(cx, cy + dy * sz), s2, 1.f);

                dl->PathArcTo(ImVec2(arc_cx, arc_cy), cr, angle_start, angle_end, 8);
                dl->PathStroke(col, 0, 1.f);
                dl->AddLine(ImVec2(cx + dx * cr, cy), ImVec2(cx + dx * sz, cy), col, 1.f);
                dl->AddLine(ImVec2(cx, cy + dy * cr), ImVec2(cx, cy + dy * sz), col, 1.f);
            } else {
                dl->AddLine(ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(cx + dx * sz + 2.f * (dx > 0 ? 1 : -1), cy + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);
                dl->AddLine(ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + dy * sz + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);

                dl->AddLine(ImVec2(cx, cy), ImVec2(cx + dx * sz, cy), s2, 1.f);
                dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + dy * sz), s2, 1.f);

                dl->AddLine(ImVec2(cx, cy), ImVec2(cx + dx * sz, cy), col, 1.f);
                dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + dy * sz), col, 1.f);
            }
        };

        corner(x1, y1, 1, 1);
        corner(x2, y1, -1, 1);
        corner(x1, y2, 1, -1);
        corner(x2, y2, -1, -1);
    }
}

void visuals::dhp(int hp, const ImVec2& t, const ImVec2& b, float box_h, float a) {
    if (a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    hp = std::clamp(hp, 0, 100);
    float pct = hp / 100.f;

    float bx = roundf(t.x - 6.f);
    float bw = 3.f;
    float bh = roundf(b.y - t.y);
    float fh = bh * pct;

    float top_y = t.y;
    float bot_y = b.y;
    float fill_top = roundf(bot_y - fh);

    dl->AddRectFilled(ImVec2(bx - 2.f, top_y - 2.f), ImVec2(bx + bw + 2.f, bot_y + 2.f), IM_COL32(0, 0, 0, (int)(120 * a)), 1.f);
    dl->AddRectFilled(ImVec2(bx - 1.f, top_y - 1.f), ImVec2(bx + bw + 1.f, bot_y + 1.f), IM_COL32(0, 0, 0, (int)(200 * a)), 0.f);
    dl->AddRect(ImVec2(bx - 1.f, top_y - 1.f), ImVec2(bx + bw + 1.f, bot_y + 1.f), IM_COL32(30, 30, 30, (int)(255 * a)), 0, 0, 1.f);

    ImU32 col_top = IM_COL32(
        static_cast<int>(cfg::esp::health_col.x * 255),
        static_cast<int>(cfg::esp::health_col.y * 255),
        static_cast<int>(cfg::esp::health_col.z * 255),
        static_cast<int>(cfg::esp::health_col.w * 255 * a)
    );
    ImU32 col_bot = IM_COL32(
        static_cast<int>(cfg::esp::health_col.x * 180),
        static_cast<int>(cfg::esp::health_col.y * 180),
        static_cast<int>(cfg::esp::health_col.z * 180),
        static_cast<int>(cfg::esp::health_col.w * 255 * a)
    );

    if (fh > 1.f) {
        dl->AddRectFilledMultiColor(ImVec2(bx, fill_top), ImVec2(bx + bw, bot_y), col_top, col_top, col_bot, col_bot);
    }

    if (hp < 100 && espFont && bh > 20.f) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d", hp);
        float fs = 10.f;
        ImVec2 ts = espFont->CalcTextSizeA(fs, FLT_MAX, 0.f, txt);
        float tx = roundf(bx + (bw - ts.x) * 0.5f);
        float ty = roundf(fill_top - ts.y * 0.5f);
        if (ty < top_y) ty = top_y;
        if (ty + ts.y > bot_y) ty = bot_y - ts.y;
        draw_text_outlined(dl, espFont, fs, ImVec2(tx, ty), IM_COL32(255, 255, 255, (int)(255 * a)), txt);
    }
}

void visuals::dnick(const char* name, const ImVec2& box_max, float cx, float sz, float a) {
    if (!espFont || a < 0.01f || !name) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    ImVec2 ts = espFont->CalcTextSizeA(sz, FLT_MAX, 0.f, name);
    ImVec2 tp(roundf(cx - ts.x * 0.5f), roundf(box_max.y + 4.f));

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::name_col.x * 255),
        static_cast<int>(cfg::esp::name_col.y * 255),
        static_cast<int>(cfg::esp::name_col.z * 255),
        static_cast<int>(cfg::esp::name_col.w * 255 * a)
    );
    draw_text_outlined(dl, espFont, sz, tp, col, name);
}

void visuals::ddist(float dist, float x, float y, float sz, float a) {
    if (!espFont || a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    char txt[16];
    snprintf(txt, sizeof(txt), "%dm", static_cast<int>(dist));

    ImVec2 tp(roundf(x + 5.f), roundf(y));

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::distance_col.x * 255),
        static_cast<int>(cfg::esp::distance_col.y * 255),
        static_cast<int>(cfg::esp::distance_col.z * 255),
        static_cast<int>(cfg::esp::distance_col.w * 255 * a)
    );
    draw_text_outlined(dl, espFont, sz, tp, col, txt);
}

void visuals::dsnapline(const ImVec2& foot, float a) {
    if (a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    int idx = cfg::esp::snaplines_col;
    if (idx < 0 || idx > 5) idx = 0;
    const ImVec4 c = esp_preset_color(idx);
    const ImU32 col = IM_COL32(
        static_cast<int>(c.x * 255.f),
        static_cast<int>(c.y * 255.f),
        static_cast<int>(c.z * 255.f),
        static_cast<int>(c.w * 255.f * a)
    );
    dl->AddLine(ImVec2(g_sw * 0.5f, g_sh), foot, col, 1.5f);
}

void visuals::draw_text_outlined(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text) {
    if (!font || !dl) return;

    int a = (color >> IM_COL32_A_SHIFT) & 0xFF;
    int s1 = static_cast<int>(a * 0.4f);
    int s2 = static_cast<int>(a * 0.7f);

    dl->AddText(font, size, ImVec2(pos.x + 2.f, pos.y + 2.f), IM_COL32(0, 0, 0, s1), text);
    dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, s2), text);
    dl->AddText(font, size, pos, color, text);
}
