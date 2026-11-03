// WalkDiag -- the [walk] line (include/Hooks/WalkDiag.hpp).
#include "Hooks/WalkDiag.hpp"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
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
    // The window's steps themselves (horizontal, metres), for the [walkstep] line: what moves between two samples
    // with no clock in it, so a sample taken late does not read as a speed change.
    static constexpr int kSteps = 160;
    float stepX[kSteps]{}, stepY[kSteps]{};
    int steps = 0;
    double lastDx = 0, lastDy = 0;
    void KeepStep() {
        if (steps < kSteps) { stepX[steps] = float(lastDx); stepY[steps] = float(lastDy); ++steps; }
    }
    // Returns the step's speed, or -1 when there is none to measure (first sample, a pause, a teleport).
    double Step(float x, float y, float z, double now) {
        double v = -1;
        if (have) {
            const double dt = now - lastTime;
            const double dx = double(x) - last[0], dy = double(y) - last[1];
            lastDx = dx; lastDy = dy;
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
Track g_entity, g_camera, g_rendered;
Stat g_stickX, g_stickY;
std::atomic<float> g_lastStickX{0}, g_lastStickY{0};
double g_windowStart = 0;
bool g_moving = false;   // the player entity moved at its last tick

// The stick is pushed: the player means to walk. The [walkstep] steps are kept on this rather than on the entity
// moving, so a sample where nothing moved during a walk is counted, not dropped.
bool StickPushed() {
    const float x = g_lastStickX.load(std::memory_order_relaxed), y = g_lastStickY.load(std::memory_order_relaxed);
    return x * x + y * y >= 0.04f;
}

void Clear() {
    for (Track* t : {&g_entity, &g_camera, &g_rendered}) {
        t->speed = t->interval = t->change = Stat{};
        t->steps = 0;
    }
    g_stickX = g_stickY = Stat{};
}

// One track's steps for the [walkstep] line, in millimetres: each step split into its part along the window's
// direction of travel (the sum of its steps) and the part across it. Appends the spread of both, the steps that went
// backwards, and the first steps along the walk one by one.
void AppendSteps(char* out, size_t size, const char* name, const Track& t) {
    size_t used = strlen(out);
    auto append = [&](const char* fmt, auto... args) {
        if (used >= size) return;
        const int n = snprintf(out + used, size - used, fmt, args...);
        if (n > 0) used = std::min(size, used + size_t(n));
    };
    double sx = 0, sy = 0;
    for (int i = 0; i < t.steps; ++i) { sx += t.stepX[i]; sy += t.stepY[i]; }
    const double length = std::sqrt(sx * sx + sy * sy);
    if (t.steps < 2 || length < 1e-6) { append(" | %s: none", name); return; }
    const double ux = sx / length, uy = sy / length;
    Stat along, across;
    int backward = 0;
    for (int i = 0; i < t.steps; ++i) {
        const double a = (t.stepX[i] * ux + t.stepY[i] * uy) * 1000.0;
        along.Add(a);
        across.Add((t.stepY[i] * ux - t.stepX[i] * uy) * 1000.0);
        if (a < 0) ++backward;
    }
    append(" | %s: %d steps, along mean %.1f sd %.1f (%.1f..%.1f), backward %d, across sd %.1f:", name, along.n,
           along.Mean(), along.Sd(), along.low, along.high, backward, across.Sd());
    for (int i = 0; i < std::min(t.steps, 48); ++i) append(" %.1f", (t.stepX[i] * ux + t.stepY[i] * uy) * 1000.0);
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
    if (v >= 0 && StickPushed()) g_entity.KeepStep();
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
    if (g_entity.steps >= 10) {
        char line[2048];
        snprintf(line, sizeof line, "[walkstep] mm per sample, while the stick is pushed");
        AppendSteps(line, sizeof line, "drawn from (main view as prepared)", g_rendered);
        AppendSteps(line, sizeof line, "engine camera (locate, before the port)", g_camera);
        AppendSteps(line, sizeof line, "entity (per tick)", g_entity);
        Log("%s\n", line);
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
    if (v >= 0 && StickPushed()) g_camera.KeepStep();
}

void NoteRendered(float x, float y, float z) {
    if (!CyberpunkVR_XrRateLog) return;
    std::lock_guard lock(g_mutex);
    // Every sample is kept, a repeat included: two prepares of one frame show here as a 0 step, two cameras as
    // alternating steps -- either is worth seeing.
    const double v = g_rendered.Step(x, y, z, NowSeconds());
    if (v >= 0 && StickPushed()) g_rendered.KeepStep();
}

}  // namespace cvr::walkdiag
