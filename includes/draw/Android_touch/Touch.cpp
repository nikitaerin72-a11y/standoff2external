#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <cmath>
#include <unistd.h>
#include <vector>
#include "imgui.h"

#define TOUCH_MODE 2

namespace touch {
    struct input {
        int fd;
        bool canBeUsed{false};
        input_absinfo absX{}, absY{};
        float absXMultiplier{0}, absYMultiplier{0};
#if TOUCH_MODE == 2
        bool trackingIDPresent{false};
#endif

        input(const char *_path, int32_t _screen_w, int32_t _screen_h) {
            fd = open(_path, O_RDWR);
            if (fd == -1)
                return;
            canBeUsed = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX) != -1 && ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY) != -1 && absX.maximum > 0 && absY.maximum > 0;
            if (!canBeUsed) {
                close();
                return;
            }

#if TOUCH_MODE == 2
            input_absinfo absTrackingID{};
            trackingIDPresent = ioctl(fd, EVIOCGABS(ABS_MT_TRACKING_ID), &absTrackingID) != -1 && absTrackingID.minimum <= 0 && absTrackingID.maximum > 0;
#endif

            calculateMultipliers(_screen_w, _screen_h);
        }

        void close() {
            if (fd != -1) {
                ::close(fd);
                fd = -1;
            }
        }

        void calculateMultipliers(int32_t _screen_w, int32_t _screen_h) {
            auto max = fmax((float) absX.maximum, (float) absY.maximum);
            auto min = fmin((float) absX.maximum, (float) absY.maximum);
            absXMultiplier = (float) _screen_w / (_screen_w > _screen_h ? max : min);
            absYMultiplier = (float) _screen_h / (_screen_w > _screen_h ? min : max);
        }
    };

    uint8_t orientation;
    std::vector<input> inputs;

    // Worker threads only take snapshots here; the main thread applies the
    // latest one to ImGuiIO in poll(). Direct io writes from several device
    // threads used to race each other and the render thread (torn cursor,
    // stuck clicks), and shared function-statics mixed coordinates of
    // different devices together.
    struct TouchSample {
        float x{0}, y{0};
        bool down{false};
        float maxX{1}, maxY{1}, mulX{1}, mulY{1};
        unsigned long long seq{0};
    };
    static TouchSample g_sample;
    static std::mutex g_sampleMutex;

    void input_thread(const input &_input) {
        float x = 0.0f, y = 0.0f; // per-thread locals: devices no longer share state
        bool isDown = false;
        input_event events[64]{0};
        while (_input.fd > 0) {
            auto event_readed_count = read(_input.fd, events, sizeof(events));
            if (event_readed_count == -1) { usleep(2000); continue; } // dead fd: don't hot-spin
            event_readed_count /= (ssize_t) sizeof(input_event);

            for (long j = 0; j < event_readed_count; j++) {
                auto &event = events[j];
                if (event.type == EV_ABS) {
                    if (_input.trackingIDPresent && event.code == ABS_MT_TRACKING_ID) {
                        isDown = event.value != -1;
                        continue;
                    }
                    if (event.code == ABS_MT_POSITION_X) {
                        x = (float) event.value;
                        continue;
                    }
                    if (event.code == ABS_MT_POSITION_Y) {
                        y = (float) event.value;
                        continue;
                    }
                }
                if (event.code == SYN_REPORT) {
                    std::lock_guard<std::mutex> lk(g_sampleMutex);
                    g_sample.x = x; g_sample.y = y; g_sample.down = isDown;
                    g_sample.maxX = (float) _input.absX.maximum;
                    g_sample.maxY = (float) _input.absY.maximum;
                    g_sample.mulX = _input.absXMultiplier;
                    g_sample.mulY = _input.absYMultiplier;
                    g_sample.seq++;
                }
            }
        }
    }

    // Called once per frame from the main thread, BEFORE drawBegin/NewFrame,
    // so ImGui sees each sample exactly once with no data race.
    void poll() {
        TouchSample s;
        {
            std::lock_guard<std::mutex> lk(g_sampleMutex);
            s = g_sample;
        }
        ImGuiIO &io = ImGui::GetIO();
        if (!s.down) { io.MouseDown[0] = false; return; }
        io.MouseDown[0] = true;
        float sx, sy;
        switch (orientation) {
            case 1:
                sx = s.y;
                sy = s.maxX - s.x;
                break;
            case 2:
                sx = s.maxX - s.x;
                sy = s.maxY - s.y;
                break;
            case 3:
                sx = s.maxY - s.y;
                sy = s.x;
                break;
            default:
                sx = s.x;
                sy = s.y;
                break;
        }
        io.MousePos.x = sx * s.mulX;
        io.MousePos.y = sy * s.mulY;
    }

    bool init(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation) {
        orientation = _orientation;

        bool result = false;

        const char *path = "/dev/input/";
        auto dir = opendir(path);
        dirent *ptr;
        while ((ptr = readdir(dir)))
            if (strstr(ptr->d_name, "event")) {
                char event_path[128];
                sprintf(event_path, "%s%s", path, ptr->d_name);
                input _input(event_path, _screen_w, _screen_h);
                if (_input.canBeUsed) {
                    result = true;
                    inputs.push_back(_input);
                    std::thread(input_thread, _input).detach();
                }
            }
        closedir(dir);

        return result;
    }

    void update(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation) {
        orientation = _orientation;

        for (auto &_input: inputs) {
            _input.calculateMultipliers(_screen_w, _screen_h);
        }
    }

    void updateOrientation(uint8_t _orientation) {
        orientation = _orientation;
    }

    void shutdown() {
        for (auto &_input: inputs)
            _input.close();
        inputs.clear();
    }
}
