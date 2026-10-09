#include "inject.hpp"
#include "../other/diag.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <pthread.h>
#include <poll.h>
#include <linux/input.h>
#include <sys/ioctl.h>

namespace inject {

namespace {

int g_fd = -1;
int g_minX = 0, g_maxX = 0, g_minY = 0, g_maxY = 0;
long long g_failLog = 0;
float g_sw = 1920.f, g_sh = 1080.f; // overlay size, set per frame via set_screen

#define NBITS(x) ((((x) - 1) / (8 * sizeof(unsigned long))) + 1)
inline bool bit(const unsigned long* a, int b) {
    return (a[(b) / (8 * sizeof(unsigned long))] >> ((b) % (8 * sizeof(unsigned long)))) & 1UL;
}

bool device_is_touchscreen(int fd) {
    unsigned long ev[NBITS(EV_MAX)]{};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0) return false;
    if (!bit(ev, EV_ABS)) return false;
    unsigned long ab[NBITS(ABS_MAX)]{};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ab)), ab) < 0) return false;
    return bit(ab, ABS_MT_POSITION_X) && bit(ab, ABS_MT_POSITION_Y);
}

bool query_ranges(int fd) {
    struct input_absinfo info{};
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &info) < 0) return false;
    g_minX = info.minimum; g_maxX = info.maximum;
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &info) < 0) return false;
    g_minY = info.minimum; g_maxY = info.maximum;
    return g_maxX > g_minX && g_maxY > g_minY;
}

bool open_touchscreen() {
    if (g_fd >= 0) return true;
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        if (diag::every(g_failLog, 10000))
            diag::log("WARN", "inject: cannot open /dev/input (need root)");
        return false;
    }
    int found = -1;
    while (dirent* e = readdir(dir)) {
        if (std::strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        const int fd = ::open(path, O_RDWR);
        if (fd < 0) continue;
        if (device_is_touchscreen(fd) && query_ranges(fd)) {
            found = fd;
            diag::log("INFO", "inject: touchscreen %s range x[%d,%d] y[%d,%d]",
                      path, g_minX, g_maxX, g_minY, g_maxY);
            break;
        }
        ::close(fd);
    }
    closedir(dir);
    if (found < 0) {
        if (diag::every(g_failLog, 10000))
            diag::log("WARN", "inject: no MT touchscreen in /dev/input");
        return false;
    }
    g_fd = found;
    return true;
}

inline void put(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event ev{};
    ev.type = type; ev.code = code; ev.value = value;
    (void)::write(fd, &ev, sizeof(ev));
}

} // namespace

static bool tap_sync(float x_px, float y_px);

bool ready() { return g_fd >= 0; }

void shutdown() {
    if (g_fd >= 0) { ::close(g_fd); g_fd = -1; }
}

bool tap(float x_px, float y_px) {
    if (!open_touchscreen()) return false;
    return tap_sync(x_px, y_px);
}

// ---- async input queue: the render thread never blocks on input ----
// First-tap device discovery (opendir + dozens of opens/ioctls) used to
// stall the ESP frame and looked like "ESP freezes when firing".
// Taps use MT slot 0, compensation swipes use slot 1 (true multitouch).
static pthread_t g_tapThr;
static bool g_tapStarted = false;
static pthread_mutex_t g_tapM = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_tapC = PTHREAD_COND_INITIALIZER;
struct Req { bool swipe = false; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
static bool g_pending = false;
static Req g_req;

static void swipe_sync(float x0, float y0, float x1, float y1) {
    auto mapAxis = [](float px, float screen, int mn, int mx) {
        float t = screen > 0.f ? px / screen : 0.5f;
        if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
        return mn + (int)(t * (float)(mx - mn));
    };
    const int ax = mapAxis(x0, g_sw, g_minX, g_maxX);
    const int ay = mapAxis(y0, g_sh, g_minY, g_maxY);
    const int bx = mapAxis(x1, g_sw, g_minX, g_maxX);
    const int by = mapAxis(y1, g_sh, g_minY, g_maxY);
    put(g_fd, EV_ABS, ABS_MT_SLOT, 1);
    put(g_fd, EV_ABS, ABS_MT_TRACKING_ID, 48);
    put(g_fd, EV_ABS, ABS_MT_POSITION_X, ax);
    put(g_fd, EV_ABS, ABS_MT_POSITION_Y, ay);
    put(g_fd, EV_ABS, ABS_MT_PRESSURE, 40);
    put(g_fd, EV_KEY, BTN_TOUCH, 1);
    put(g_fd, EV_SYN, SYN_REPORT, 0);
    for (int i = 1; i <= 3; ++i) { // ~30ms drag in 3 steps
        usleep(10000);
        put(g_fd, EV_ABS, ABS_MT_POSITION_X, ax + (bx - ax) * i / 3);
        put(g_fd, EV_ABS, ABS_MT_POSITION_Y, ay + (by - ay) * i / 3);
        put(g_fd, EV_SYN, SYN_REPORT, 0);
    }
    put(g_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    put(g_fd, EV_KEY, BTN_TOUCH, 0);
    put(g_fd, EV_SYN, SYN_REPORT, 0);
}

static void* tap_worker(void*) {
    for (;;) {
        pthread_mutex_lock(&g_tapM);
        while (!g_pending) pthread_cond_wait(&g_tapC, &g_tapM);
        const Req rq = g_req;
        g_pending = false;
        pthread_mutex_unlock(&g_tapM);
        if (g_fd < 0 && !open_touchscreen()) {
            if (diag::every(g_failLog, 10000))
                diag::log("WARN", "inject: no touchscreen (need root)");
            continue;
        }
        if (rq.swipe) swipe_sync(rq.x0, rq.y0, rq.x1, rq.y1);
        else if (!tap_sync(rq.x0, rq.y0) && diag::every(g_failLog, 10000))
            diag::log("WARN", "inject: tap failed");
    }
    return nullptr;
}

static bool worker_ensure() {
    if (g_tapStarted) return true;
    g_tapStarted = true;
    if (pthread_create(&g_tapThr, nullptr, tap_worker, nullptr) != 0) {
        g_tapStarted = false;
        return false;
    }
    pthread_detach(g_tapThr);
    return true;
}

bool request_tap(float x_px, float y_px) {
    if (!worker_ensure()) return tap(x_px, y_px); // no threads: sync fallback
    pthread_mutex_lock(&g_tapM);
    g_req = Req{false, x_px, y_px, 0, 0}; // coalesce: newest request wins
    g_pending = true;
    pthread_cond_signal(&g_tapC);
    pthread_mutex_unlock(&g_tapM);
    return true;
}

bool request_swipe(float x0_px, float y0_px, float x1_px, float y1_px) {
    if (!worker_ensure()) return false;
    pthread_mutex_lock(&g_tapM);
    if (g_pending) { pthread_mutex_unlock(&g_tapM); return true; } // busy: drop, worker is mid-gesture
    g_req = Req{true, x0_px, y0_px, x1_px, y1_px};
    g_pending = true;
    pthread_cond_signal(&g_tapC);
    pthread_mutex_unlock(&g_tapM);
    return true;
}

void set_screen(float w, float h) {
    if (w > 0.f && h > 0.f) { g_sw = w; g_sh = h; }
}

// ---- physical key monitor for binds (volume up/down) ----
static pthread_mutex_t g_keyM = PTHREAD_MUTEX_INITIALIZER;
static bool g_keyDown[2] = {};
static unsigned long long g_keySeq[2] = {};
static bool g_keyStarted = false;

static int key_code(int which) { return which == 0 ? 115 : 114; } // VOLUP, VOLDOWN
static int key_which(unsigned short code) {
    if (code == 115) return 0;
    if (code == 114) return 1;
    return -1;
}

static void* key_worker(void*) {
    int fds[32]; int nf = 0;
    DIR* dir = opendir("/dev/input");
    if (dir) {
        while (dirent* e = readdir(dir)) {
            if (nf >= 32) break;
            if (std::strncmp(e->d_name, "event", 5) != 0) continue;
            bool digits = true;
            for (const char* p = e->d_name + 5; *p; ++p)
                if (*p < '0' || *p > '9') { digits = false; break; }
            if (!digits || !e->d_name[5]) continue;
            char path[64];
            std::snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
            const int fd = ::open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            unsigned long ev[NBITS(EV_MAX)]{};
            unsigned long ky[NBITS(KEY_MAX)]{};
            const bool ok = ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) >= 0 && bit(ev, EV_KEY) &&
                            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(ky)), ky) >= 0 &&
                            (bit(ky, (unsigned)key_code(0)) || bit(ky, (unsigned)key_code(1)));
            if (ok) fds[nf++] = fd;
            else ::close(fd);
        }
        closedir(dir);
    }
    if (!nf) {
        if (diag::every(g_failLog, 10000)) diag::log("WARN", "inject: no key device for binds");
        return nullptr;
    }
    struct pollfd pf[32];
    for (int i = 0; i < nf; ++i) { pf[i].fd = fds[i]; pf[i].events = POLLIN; pf[i].revents = 0; }
    for (;;) {
        if (poll(pf, (nfds_t)nf, 120) <= 0) continue;
        for (int i = 0; i < nf; ++i) {
            if (!(pf[i].revents & POLLIN)) continue;
            struct input_event ev{};
            while (::read(fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type != EV_KEY) continue;
                const int which = key_which(ev.code);
                if (which < 0) continue;
                pthread_mutex_lock(&g_keyM);
                if (ev.value == 1) {
                    if (!g_keyDown[which]) { g_keyDown[which] = true; g_keySeq[which]++; }
                } else if (ev.value == 0) {
                    g_keyDown[which] = false;
                } else if (ev.value == 2) {
                    g_keyDown[which] = true; // autorepeat: still held, no new edge
                }
                pthread_mutex_unlock(&g_keyM);
            }
        }
    }
    return nullptr;
}

void keywatch_ensure() {
    if (g_keyStarted) return;
    g_keyStarted = true;
    pthread_t th;
    if (pthread_create(&th, nullptr, key_worker, nullptr) != 0) {
        g_keyStarted = false;
        return;
    }
    pthread_detach(th);
}

bool key_down(int which) {
    if (which < 0 || which > 1) return false;
    pthread_mutex_lock(&g_keyM);
    const bool r = g_keyDown[which];
    pthread_mutex_unlock(&g_keyM);
    return r;
}

unsigned long long key_seq(int which) {
    if (which < 0 || which > 1) return 0;
    pthread_mutex_lock(&g_keyM);
    const unsigned long long r = g_keySeq[which];
    pthread_mutex_unlock(&g_keyM);
    return r;
}

static bool tap_sync(float x_px, float y_px) {
    if (!std::isfinite(x_px) || !std::isfinite(y_px)) return false;
    // Display px -> sensor range. Raw px are NOT sensor units on most panels
    // (different extents/orientation), so scale proportionally instead of
    // clamping: without this taps land in the wrong place.
    auto mapAxis = [](float px, float screen, int mn, int mx) {
        float t = screen > 0.f ? px / screen : 0.5f;
        if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
        return mn + (int)(t * (float)(mx - mn));
    };
    const int ix = mapAxis(x_px, g_sw, g_minX, g_maxX);
    const int iy = mapAxis(y_px, g_sh, g_minY, g_maxY);

    // Multitouch protocol B: slot 0 down/up, one SYN frame each side.
    put(g_fd, EV_ABS, ABS_MT_SLOT, 0);
    put(g_fd, EV_ABS, ABS_MT_TRACKING_ID, 45);
    put(g_fd, EV_ABS, ABS_MT_POSITION_X, ix);
    put(g_fd, EV_ABS, ABS_MT_POSITION_Y, iy);
    put(g_fd, EV_ABS, ABS_MT_PRESSURE, 50);
    put(g_fd, EV_KEY, BTN_TOUCH, 1);
    put(g_fd, EV_SYN, SYN_REPORT, 0);
    usleep(10000); // ~10ms contact so the game registers a tap
    put(g_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    put(g_fd, EV_KEY, BTN_TOUCH, 0);
    put(g_fd, EV_SYN, SYN_REPORT, 0);
    return true;
}

} // namespace inject
