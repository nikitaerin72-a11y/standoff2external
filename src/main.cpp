#include "Android_draw/draw.h"
#include "ui/theme/theme.hpp"
#include "ui/menu.hpp"
#include "ui/bar.hpp"
#include "other/memory.hpp"
#include "game/game.hpp"
#include "func/visuals.hpp"
#include "func/tracer.hpp"
#include "combat/combat.hpp"
#include "protect/oxorany.hpp"
#include <cstdio>
#include <thread>
#include <chrono>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

static void print_status(const char* status) {
    diag::log("STATUS", "%s", status);
}

// Функция для запуска Standoff
static void launch_standoff() {
    int rc = system(oxorany("monkey -p com.axlebolt.standoff2 -c android.intent.category.LAUNCHER 1"));
    diag::log(rc == 0 ? "INFO" : "WARN", "game launch command exit=%d package=com.axlebolt.standoff2", rc);
}

int main() {
    // Root cheat: game memory (/proc/pid/mem) and /dev/input taps both need uid 0.
    // Fail loud here instead of showing an empty overlay later.
    if (::getuid() != 0) {
        diag::log("ERROR", "root required: uid=%d — run with su (mem reader + tap-fire need root)", (int)::getuid());
        return 3;
    }
    const int stdout_flags = ::fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (stdout_flags >= 0) ::fcntl(STDOUT_FILENO, F_SETFL, stdout_flags | O_NONBLOCK);
    const int instance_lock = ::open("/data/local/tmp/bycmd-1.0.0.lock", O_CREAT | O_RDWR, 0600);
    if (instance_lock < 0 || flock(instance_lock, LOCK_EX | LOCK_NB) != 0) {
        diag::log("ERROR", "another bycmd instance already owns the overlay; refusing duplicate menu");
        if (instance_lock >= 0) ::close(instance_lock);
        return 2;
    }
    diag::log("INFO", "bycmd Standoff2 1.0.0 external ESP starting");
    screen_config();

    int max_size = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    int min_size = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);

    g_sw = static_cast<float>(max_size);
    g_sh = static_cast<float>(min_size);

    native_window_screen_x = max_size;
    native_window_screen_y = min_size; // overlay matches the real display rect

    diag::log("INFO", "display width=%d height=%d orientation=%d overlay=%dx%d",
              displayInfo.width, displayInfo.height, displayInfo.orientation,
              native_window_screen_x, native_window_screen_y);
    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true)) {
        diag::log("ERROR", "initGUI_draw failed");
        return -1;
    }
    diag::log("INFO", "GUI initialized");

    touch::init(max_size, min_size, (uint8_t)displayInfo.orientation);

    // Автоматический запуск Standoff
    print_status(oxorany("Game detect ✅"));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    print_status(oxorany("start cheat...."));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    launch_standoff();
    
    game::init();
    combat::binds::load();

    static float alpha = 0.f;
    static bool prev = false;
    static bool game_started = false;

    while (!ui::menu::consume_exit()) {
        const auto frame_start = std::chrono::steady_clock::now();
        touch::poll();
        drawBegin();

        bool run = game::valid();

#if defined(__x86_64__)
        bool is_landscape = (displayInfo.orientation == 0 || displayInfo.orientation == 2);
#else
        bool is_landscape = (displayInfo.orientation == 1 || displayInfo.orientation == 3);
#endif

        if (run && !prev) {
            if (!game_started) {
                print_status(oxorany("Game detect ✅"));
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                print_status(oxorany("start cheat...."));
                game_started = true;
            } else {
                print_status(oxorany("Game detect ✅"));
            }
            prev = true;
        } else if (!run && prev) {
            print_status(oxorany("game closed"));
            prev = false;
            game_started = false;
        }

        if (is_landscape) {
            ImGuiIO& io = ImGui::GetIO();
            float dt = io.DeltaTime;
            if (dt <= 0.f || dt > 0.1f) dt = 0.016f;

            float target = run ? 1.f : 0.f;
            float spd = run ? 4.f : 6.f;

            if (alpha < target) {
                alpha += dt * spd;
                if (alpha > target) alpha = target;
            } else if (alpha > target) {
                alpha -= dt * spd;
                if (alpha < target) alpha = target;
            }

            ui::bar::set_game_alpha(alpha);

            if (alpha > 0.001f) {
                ui::menu::render();
            }
            combat::binds::apply(); // physical keys -> cfg, every frame
            snow::draw();           // foreground decor, input-safe

            // check_lib runs unconditionally: its pm==0 recovery branch was
            // dead code while hidden behind a lib!=0 guard.
            game::check_lib(proc::lib);
            if (run && proc::lib != 0) {
                visuals::draw();
            }
        }

        drawEnd();
        // 60 Hz frame pacing: the old 1.5/4ms sleeps spun the loop at 200+fps,
        // burning CPU/battery and starving the game. ESP stays smooth at 60.
        const auto frame_end = std::chrono::steady_clock::now();
        const long elapsed_us =
            std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
        const long budget_us = 16666;
        if (elapsed_us < budget_us) usleep((useconds_t)(budget_us - elapsed_us));
    }

    shutdown();
    return 0;
}
