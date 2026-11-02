// WalkDiag -- the [walk] line (include/Hooks/WalkDiag.hpp).
#include "Hooks/WalkDiag.hpp"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

extern void Log(const char* fmt, ...);
extern "C" int CyberpunkVR_XrRateLog;   // Runtimes/OpenXRFrameLoop.cpp: the frame-path reports

namespace cvr::walkdiag {
namespace {

double NowSeconds() {
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart) / double(frequency.QuadPart);
}

// Mean, spread and range of one quantity over the report's window.
struct Stat {
    double sum = 0, sum2 = 0, low = 1e30, high = -1e30;
    int n = 0;
    void Add(double v) { sum += v; sum2 += v * v; low = std::min(low, v); high = std::max(high, v); ++n; }
    double Mean() const { return n ? sum / n : 0.0; }
    double Sd() const { return n > 1 ? std::sqrt(std::max(0.0, sum2 / n - Mean() * Mean())) : 0.0; }
    double Cv() const { return Mean() > 1e-6 ? 100.0 * Sd() / Mean() : 0.0; }
};

// Steps of a moving point: its speed from one sample to the next (horizontal, metres a second) and the time between.
struct Track {
    double lastTime = 0;
    float last[3]{};
    bool have = false;
    double lastSpeed = -1;
    Stat speed, interval, change;   // change: |speed - the previous step's speed|
    // Returns the step's speed, or -1 when there is none to measure (first sample, a pause, a teleport).
    double Step(float x, float y, float z, double now) {
        double v = -1;
        if (have) {
            const double dt = now - lastTime;
            const double dx = double(x) - last[0], dy = double(y) - last[1];
            if (dt > 0.0005 && dt < 0.1) {
                v = std::sqrt(dx * dx + dy * dy) / dt;
                if (v > 20.0) v = -1;   // a teleport, not a step
            }
            if (v >= 0) {
                interval.Add(dt * 1000.0);
                if (lastSpeed >= 0) change.Add(std::fabs(v - lastSpeed));
                lastSpeed = v;
            } else {
                lastSpeed = -1;
            }
        }
        last[0] = x; last[1] = y; last[2] = z;
        lastTime = now;
        have = true;
        return v;
    }
};

std::mutex g_mutex;
Track g_entity, g_camera;
Stat g_stickX, g_stickY;
std::atomic<float> g_lastStickX{0}, g_lastStickY{0};
double g_windowStart = 0;
bool g_moving = false;   // the player entity moved at its last tick

void Clear() {
    g_entity.speed = g_entity.interval = g_entity.change = Stat{};
    g_camera.speed = g_camera.interval = g_camera.change = Stat{};
    g_stickX = g_stickY = Stat{};
}

}  // namespace

void NoteStick(float x, float y) {
    if (!CyberpunkVR_XrRateLog) return;
    g_lastStickX.store(x, std::memory_order_relaxed);
    g_lastStickY.store(y, std::memory_order_relaxed);
}

void NoteEntity(float x, float y, float z) {
    if (!CyberpunkVR_XrRateLog) return;
    std::lock_guard lock(g_mutex);
    const double now = NowSeconds();
    const double v = g_entity.Step(x, y, z, now);
    // A walk is what is measured: a step at least a slow walk's (0.3 m/s); standing still the samples are dropped.
    g_moving = v >= 0.3;
    if (g_moving) {
        g_entity.speed.Add(v);
        g_stickX.Add(g_lastStickX.load(std::memory_order_relaxed));
        g_stickY.Add(g_lastStickY.load(std::memory_order_relaxed));
    }
    if (!g_windowStart) g_windowStart = now;
    if (now - g_windowStart < 1.0) return;
    g_windowStart = now;
    if (g_entity.speed.n >= 10) {
        const Stat& es = g_entity.speed;
        const Stat& cs = g_camera.speed;
        Log("[walk] entity: %d ticks %.2f ms apart (sd %.2f) | speed %.3f m/s, sd %.3f (%.1f%%), %.3f..%.3f, tick to tick "
            "%.3f mean %.3f max | stick x %.3f (sd %.3f) y %.3f (sd %.3f, %.3f..%.3f) | camera: %d frames %.2f ms apart (sd "
            "%.2f), speed %.3f, sd %.3f (%.1f%%), %.3f..%.3f, frame to frame %.3f mean %.3f max\n",
            es.n, g_entity.interval.Mean(), g_entity.interval.Sd(), es.Mean(), es.Sd(), es.Cv(), es.low, es.high,
            g_entity.change.Mean(), g_entity.change.n ? g_entity.change.high : 0.0, g_stickX.Mean(), g_stickX.Sd(),
            g_stickY.Mean(), g_stickY.Sd(), g_stickY.n ? g_stickY.low : 0.0, g_stickY.n ? g_stickY.high : 0.0, cs.n,
            g_camera.interval.Mean(), g_camera.interval.Sd(), cs.Mean(), cs.Sd(), cs.Cv(), cs.n ? cs.low : 0.0,
            cs.n ? cs.high : 0.0, g_camera.change.Mean(), g_camera.change.n ? g_camera.change.high : 0.0);
    }
    Clear();
}

void NoteCamera(float x, float y, float z) {
    if (!CyberpunkVR_XrRateLog) return;
    std::lock_guard lock(g_mutex);
    // One sample per position: a locate entered again with the same camera is the same frame.
    if (g_camera.have && x == g_camera.last[0] && y == g_camera.last[1] && z == g_camera.last[2]) return;
    const double v = g_camera.Step(x, y, z, NowSeconds());
    if (v >= 0 && g_moving) g_camera.speed.Add(v);
}

}  // namespace cvr::walkdiag
