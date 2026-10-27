// The VR menu's natives: the summon gesture, the two pointers, and the input the menu holds back.
//
// The menu itself is the game's own UI on a plane in the world (vrport\ui\vr_menu.ent, built and run by
// mods/cet/CyberpunkVRPort_Menu). What script cannot see is the hands: their tracking history for the
// gesture, and where the rendered index fingertip and the hand's pointing ray are this frame. These
// hand that over, and the Lua decides what a touch or a click means.
//
// THE GESTURE is Sword Art Online's swipe, made deliberate: the right hand, GRIP HELD, swept straight
// down (an open hand dropping after an action is not a swipe). Read in TRACKING space, from the same published hand frame the melee reads, so a turn
// of the body or the camera never makes a swipe. Fired once; the Lua checks whether the moment allows
// a menu (no weapon in hand, no scene) before it opens one.
//
// THE POINTERS are measured on the hand the player SEES: the solved rig's snapshot (g_VRFKSnap*, filled
// while diag capture is on -- the hand collision runs it, and the menu turns it on while open), put in
// the world by the same model-to-world pair VRPalmWorldPos uses. The fingertip is the end of
// RightHandIndex3 / LeftHandIndex3; the ray starts at the index knuckle and runs along the HAND bone,
// which a trigger press does not bend -- so clicking does not move the ray off what it points at.

#include "Natives/NativeFunctions.hpp"
#include "Natives/VrMenu.hpp"
#include "Anim/VrikHook.hpp"
#include "Anim/VrikState.hpp"
#include "Overlay/VrOverlay.hpp"
#include "Overlay/UiRec.hpp"
#include "Runtimes/OpenXRManager.hpp"

#include <RED4ext/Scripting/Utils.hpp>

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cvr::vrmenu {
namespace {
std::atomic<float> g_blink{0.0f};
std::atomic<ULONGLONG> g_blinkUpdated{0};
}
float BlinkClosure() {
    if (GetTickCount64() - g_blinkUpdated.load(std::memory_order_acquire) > 2000) return 0.0f;
    return g_blink.load(std::memory_order_relaxed);
}
namespace {
std::atomic<bool> g_capture{false};
}
bool CapturesButtons() { return g_capture.load(std::memory_order_relaxed); }
}

namespace {
// Bone indices of the player rig (the snapshot is indexed like it; see the hand collision's capsules.lua).
constexpr int kHand[2] = {23, 24};      // LeftHand, RightHand
constexpr int kIndex1[2] = {46, 51};    // LeftHandIndex1, RightHandIndex1
constexpr int kIndex3[2] = {65, 69};    // LeftHandIndex3, RightHandIndex3
// From the bone to the end of the finger, along the bone's X: the last capsule's length and radius.
constexpr float kTip = 0.024f;

struct Sample {
    double t;
    float p[3];
    bool held;      // the grip squeezed
};
std::deque<Sample> g_history;
uint64_t g_lastSequence = 0;
double g_lastFire = -10.0;

// The swipe: at least this far down, inside this window, at least this fast on average, and mostly
// vertical. Measured against a deliberate flick (~0.3 m in ~0.25 s) and against a hand dropped to the
// side after an action (slower, and it starts low).
constexpr float kDrop = 0.20f;
constexpr double kWindow = 0.40;
constexpr float kSpeed = 0.75f;
constexpr float kSlant = 0.65f;
constexpr double kCooldown = 0.9;

bool ModelToWorldFrame(float* m2w) {
    if (!g_VRPalmModelValid || !g_VRViewWorldValid) return false;
    const float camRot[4] = {g_VRCamModelRot[0], g_VRCamModelRot[1], g_VRCamModelRot[2], g_VRCamModelRot[3]};
    const float viewRot[4] = {g_VRViewWorldRot[0], g_VRViewWorldRot[1], g_VRViewWorldRot[2], g_VRViewWorldRot[3]};
    float camInv[4];
    VRIK_QuatConj(camRot, camInv);
    VRIK_QuatMul(viewRot, camInv, m2w);
    VRIK_QuatNorm(m2w);
    return true;
}

void ModelToWorld(const float* m2w, const float* model, float* world) {
    const float rel[3] = {model[0] - g_VRCamModelPos[0], model[1] - g_VRCamModelPos[1], model[2] - g_VRCamModelPos[2]};
    float w[3];
    VRIK_QuatRotateVec(m2w, rel, w);
    for (int k = 0; k < 3; ++k) world[k] = g_VRViewWorldPos[k] + w[k];
}

// A bone of the snapshot in the world: position, and one of its axes (bone-local `axis`).
bool BoneWorld(const float* m2w, int bone, const float* offset, const float* axis, float* pos, float* dir) {
    if (bone < 0 || bone >= g_VRFKSnapCount) return false;
    const float p[3] = {g_VRFKSnapPos[bone][0], g_VRFKSnapPos[bone][1], g_VRFKSnapPos[bone][2]};
    const float q[4] = {g_VRFKSnapRot[bone][0], g_VRFKSnapRot[bone][1], g_VRFKSnapRot[bone][2], g_VRFKSnapRot[bone][3]};
    float model[3] = {p[0], p[1], p[2]};
    if (offset) {
        float o[3];
        VRIK_QuatRotateVec(q, offset, o);
        for (int k = 0; k < 3; ++k) model[k] += o[k];
    }
    ModelToWorld(m2w, model, pos);
    if (axis) {
        float a[3];
        VRIK_QuatRotateVec(q, axis, a);
        VRIK_QuatRotateVec(m2w, a, dir);
    }
    return true;
}

float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
} // namespace

bool cvr::vrmenu::FingerWorld(int side, float* world) {
    VRControllerState input{};
    if (side < 0 || side > 1 || !OpenXRManager::Get().GetControllerState(&input) ||
        !(side ? input.rightHandValid : input.leftHandValid)) return false;
    float m2w[4];
    const float end[3] = {side ? -kTip : kTip, 0, 0};
    return ModelToWorldFrame(m2w) && BoneWorld(m2w, kIndex3[side], end, nullptr, world, nullptr);
}

// VRMenuGesture() -> array<Vector4>: empty, or once per swipe
//   [0] (1, sequence, 0, 0)
//   [1] where the swipe started, relative to the head in its heading frame: (right, up, forward, 1) metres
//   [2] where it ended, the same way
// or, once per L3 + R3 hold (Overlay/VrOverlay.cpp), the single item (2, 0, 0, 0): open or close the menu
// in front of the head.
void VRMenuGesture(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, RED4ext::DynArray<RED4ext::Vector4>* out, int64_t) {
    stack->code++;
    if (!out) return;
    out->Clear();
    if (cvr::vrui::ConsumeMenuSummon()) {
        out->PushBack({2.0f, 0.0f, 0.0f, 0.0f});
        return;
    }
    OpenXRHeadPose head{}, hands[2]{};
    uint64_t sequence{}, stamp{};
    VRControllerState input{};
    auto& xr = OpenXRManager::Get();
    if (!xr.GetGestureHandFrame(&head, hands, &sequence, &stamp, false) || !hands[1].valid ||
        !xr.GetControllerState(&input) || !input.rightHandValid) {
        g_history.clear();
        return;
    }
    if (sequence == g_lastSequence) return;
    g_lastSequence = sequence;
    const float headQ[4] = {head.oriX, head.oriY, head.oriZ, head.oriW};
    const float local[3] = {hands[1].posX, hands[1].posY, hands[1].posZ};
    float p[3];
    VRIK_QuatRotateVec(headQ, local, p);
    const double t = double(stamp) * 1e-6;
    g_history.push_back({t, {p[0] + head.posX, p[1] + head.posY, p[2] + head.posZ},
                         input.rightGrip > 0.70f});
    while (!g_history.empty() && t - g_history.front().t > 0.6) g_history.pop_front();
    if (t - g_lastFire < kCooldown || g_history.size() < 3 || !g_history.back().held) return;

    // The earliest gripped sample of the window from which the hand fell far and straight enough.
    const Sample& end = g_history.back();
    const Sample* start = nullptr;
    for (auto it = g_history.rbegin(); it != g_history.rend(); ++it) {
        if (end.t - it->t > kWindow || !it->held) break;
        const float drop = it->p[1] - end.p[1];
        const float dx = it->p[0] - end.p[0], dz = it->p[2] - end.p[2];
        if (drop >= kDrop && std::sqrt(dx * dx + dz * dz) <= kSlant * drop && drop / float(end.t - it->t) >= kSpeed)
            start = &*it;
    }
    if (!start) return;
    g_lastFire = t;

    // The head's heading frame in tracking space (OpenXR: Y up, -Z ahead).
    const float ahead[3] = {0.0f, 0.0f, -1.0f};
    float f[3];
    VRIK_QuatRotateVec(headQ, ahead, f);
    f[1] = 0.0f;
    const float n = std::sqrt(f[0] * f[0] + f[2] * f[2]);
    if (n < 1e-4f) return;
    f[0] /= n;
    f[2] /= n;
    const float r[3] = {-f[2], 0.0f, f[0]};
    auto relative = [&](const float* q) {
        const float d[3] = {q[0] - head.posX, q[1] - head.posY, q[2] - head.posZ};
        return RED4ext::Vector4{Dot(d, r), d[1], Dot(d, f), 1.0f};
    };
    out->PushBack({1.0f, float(sequence & 0xffffffu), 0.0f, 0.0f});
    out->PushBack(relative(start->p));
    out->PushBack(relative(end.p));
    g_history.clear();
}

// VRMenuPointer(center, right, up, size) -> array<Vector4>, the menu plane given in the world (unit
// right and up axes, size.X x size.Y metres; its front faces right x up), for each hand (0 left, 1 right):
//   [4h+0] fingertip: (u, v, distance in front of the plane, valid)   u, v in 0..1 across / down the plane
//   [4h+1] ray:       (u, v, length to the plane, hits)               hits = 1 only when it meets the front
//   [4h+2] ray origin in the world (x, y, z, trigger 0..1)
//   [4h+3] ray hit point in the world (x, y, z, grip 0..1)
void VRMenuPointer(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, RED4ext::DynArray<RED4ext::Vector4>* out, int64_t) {
    RED4ext::Vector4 center{}, right{}, up{}, size{};
    RED4ext::GetParameter(stack, &center);
    RED4ext::GetParameter(stack, &right);
    RED4ext::GetParameter(stack, &up);
    RED4ext::GetParameter(stack, &size);
    stack->code++;
    if (!out) return;
    out->Clear();
    const float c[3] = {center.X, center.Y, center.Z}, rx[3] = {right.X, right.Y, right.Z}, uy[3] = {up.X, up.Y, up.Z};
    const float nz[3] = {rx[1] * uy[2] - rx[2] * uy[1], rx[2] * uy[0] - rx[0] * uy[2], rx[0] * uy[1] - rx[1] * uy[0]};
    const float w = size.X > 0.01f ? size.X : 1.0f, h = size.Y > 0.01f ? size.Y : 1.0f;
    VRControllerState input{};
    const bool haveInput = OpenXRManager::Get().GetControllerState(&input);
    float m2w[4];
    const bool frame = ModelToWorldFrame(m2w) && g_VRFKSnapCount > kIndex3[1];
    for (int side = 0; side < 2; ++side) {
        RED4ext::Vector4 tip{-1, -1, 0, 0}, ray{-1, -1, 0, 0}, origin{0, 0, 0, 0}, hit{0, 0, 0, 0};
        const float trigger = haveInput ? (side ? input.rightTrigger : input.leftTrigger) : 0.0f;
        const float grip = haveInput ? (side ? input.rightGrip : input.leftGrip) : 0.0f;
        const float along[3] = {side ? -1.0f : 1.0f, 0.0f, 0.0f};
        const float end[3] = {along[0] * kTip, 0.0f, 0.0f};
        float p[3], d[3];
        if (frame && BoneWorld(m2w, kIndex3[side], end, nullptr, p, nullptr)) {
            const float l[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
            tip = {Dot(l, rx) / w + 0.5f, 0.5f - Dot(l, uy) / h, Dot(l, nz), 1.0f};
        }
        float o[3];
        if (frame && BoneWorld(m2w, kIndex1[side], nullptr, nullptr, o, nullptr) &&
            BoneWorld(m2w, kHand[side], nullptr, along, p, d)) {
            origin = {o[0], o[1], o[2], trigger};
            const float l[3] = {o[0] - c[0], o[1] - c[1], o[2] - c[2]};
            const float front = Dot(l, nz), into = Dot(d, nz);
            if (front > 0.0f && into < -1e-3f) {
                const float t = -front / into;
                const float q[3] = {o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t};
                const float lq[3] = {q[0] - c[0], q[1] - c[1], q[2] - c[2]};
                ray = {Dot(lq, rx) / w + 0.5f, 0.5f - Dot(lq, uy) / h, t, t < 6.0f ? 1.0f : 0.0f};
                hit = {q[0], q[1], q[2], grip};
            } else {
                hit = {o[0] + d[0] * 2.0f, o[1] + d[1] * 2.0f, o[2] + d[2] * 2.0f, grip};
            }
        }
        out->PushBack(tip);
        out->PushBack(ray);
        out->PushBack(origin);
        out->PushBack(hit);
    }
}

// VRMenuOpenSettings(): the port's own settings panel, as F10 opens it.
void VRMenuOpenSettings(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, bool* out, int64_t) {
    stack->code++;
    const bool open = !cvr::vrui::Visible();
    if (open) cvr::vrui::Toggle();
    if (out) *out = open;
}

// VRMenuCapture(open): while the menu is open the triggers, grips and face buttons stop reaching the game.
void VRMenuCapture(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, bool* out, int64_t) {
    bool open = false;
    RED4ext::GetParameter(stack, &open);
    stack->code++;
    cvr::vrmenu::g_capture.store(open, std::memory_order_relaxed);
    if (out) *out = open;
}

void VRMenuBlink(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, bool* out, int64_t) {
    float closure = 0.0f;
    RED4ext::GetParameter(stack, &closure);
    stack->code++;
    cvr::vrmenu::g_blink.store(std::isfinite(closure) ? std::clamp(closure, 0.0f, 1.0f) : 0.0f,
                              std::memory_order_relaxed);
    cvr::vrmenu::g_blinkUpdated.store(GetTickCount64(), std::memory_order_release);
    if (out) *out = true;
}

// VRSettingsPage(page) -> String: the port's settings page as the VR menu draws it (Overlay/UiRec.hpp),
// the latest one recorded -- "" until the first -- and a request for a fresh one, recorded on the next
// present. 0 GENERAL, 1 FRAMEGEN, 2 HUD, 3 CONTROLS, 4 STEREO, 5 AVATAR, 6 OVERLAY.
void VRSettingsPage(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, RED4ext::CString* out, int64_t) {
    int32_t page = 0;
    RED4ext::GetParameter(stack, &page);
    stack->code++;
    cvr::uirec::Request(page);
    if (out) *out = RED4ext::CString(cvr::uirec::Latest(page).c_str());
}

// VRSettingsEdit(page, id, value): what the VR menu did to the item `id` of that page -- a checkbox's
// 0 / 1, a slider's value, a combo's or tab's index, any value for a button. Applied through the panel
// code on the next present, persisted the way the panel persists.
void VRSettingsEdit(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, bool* out, int64_t) {
    int32_t page = 0;
    RED4ext::CString id;
    float value = 0.0f;
    RED4ext::GetParameter(stack, &page);
    RED4ext::GetParameter(stack, &id);
    RED4ext::GetParameter(stack, &value);
    stack->code++;
    cvr::uirec::QueueEdit(page, id.c_str(), value);
    if (out) *out = true;
}

// THE MENU'S OWN SOUNDS. The game's UI events are Wwise random containers -- one event, a different
// variant each play -- so the variant the player picked can only be had as its own file: the mod ships
// it as a WAV and this plays it on the system's default output with XAudio2, at the volume the Lua
// works out from the game's master and effects sliders. No voice callbacks (nothing of this DLL runs on
// XAudio2's thread, so unloading the plugin at exit cannot leave a call into it); a finished voice is
// destroyed on the next play.
namespace {
struct Wav {
    WAVEFORMATEX format{};
    std::vector<uint8_t> data;
};

std::mutex g_soundMutex;
IXAudio2* g_audio = nullptr;
IXAudio2MasteringVoice* g_master = nullptr;
bool g_audioFailed = false;
std::unordered_map<std::wstring, std::shared_ptr<Wav>> g_wavs;
std::vector<IXAudio2SourceVoice*> g_voices;

// RIFF / WAVE, PCM only: the 'fmt ' chunk and the 'data' chunk, whatever else sits between them.
std::shared_ptr<Wav> LoadWav(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return nullptr;
    auto wav = std::make_shared<Wav>();
    bool haveFormat = false;
    for (size_t at = 12; at + 8 <= b.size();) {
        uint32_t size = 0;
        std::memcpy(&size, b.data() + at + 4, 4);
        const uint8_t* body = b.data() + at + 8;
        if (at + 8 + size > b.size()) break;
        if (std::memcmp(b.data() + at, "fmt ", 4) == 0 && size >= 16) {
            std::memcpy(&wav->format, body, 16);
            haveFormat = wav->format.wFormatTag == WAVE_FORMAT_PCM;
        } else if (std::memcmp(b.data() + at, "data", 4) == 0) {
            wav->data.assign(body, body + size);
        }
        at += 8 + size + (size & 1);
    }
    if (!haveFormat || wav->data.empty()) return nullptr;
    wav->format.cbSize = 0;
    return wav;
}

// A relative path is under the game's CET mods folder (bin\x64\plugins\cyber_engine_tweaks\mods).
std::filesystem::path SoundPath(const char* file) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, file, -1, nullptr, 0), L'\0');
    if (!w.empty()) MultiByteToWideChar(CP_UTF8, 0, file, -1, w.data(), static_cast<int>(w.size()));
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    std::filesystem::path p(w);
    if (p.is_absolute()) return p;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / L"plugins" / L"cyber_engine_tweaks" / L"mods" / p;
}

bool StartAudio() {
    if (g_audio != nullptr) return true;
    if (g_audioFailed) return false;
    // The calling thread (the game's script thread) has COM already; this only adds a reference, or
    // answers that the thread is in another apartment, which XAudio2 does not mind.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(XAudio2Create(&g_audio, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
        FAILED(g_audio->CreateMasteringVoice(&g_master))) {
        if (g_audio != nullptr) g_audio->Release();
        g_audio = nullptr;
        g_audioFailed = true;
        return false;
    }
    return true;
}
}

// VRMenuPlayWav(file, volume) -> Bool: plays a PCM WAV once (relative to the CET mods folder), at
// `volume` (0..1). False when the file or the audio device is not there -- the Lua then falls back to
// the game's event.
void VRMenuPlayWav(RED4ext::IScriptable*, RED4ext::CStackFrame* stack, bool* out, int64_t) {
    RED4ext::CString file;
    float volume = 1.0f;
    RED4ext::GetParameter(stack, &file);
    RED4ext::GetParameter(stack, &volume);
    stack->code++;
    bool played = false;
    {
        std::lock_guard lock(g_soundMutex);
        if (StartAudio()) {
            for (size_t i = 0; i < g_voices.size();) {
                XAUDIO2_VOICE_STATE st{};
                g_voices[i]->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                if (st.BuffersQueued == 0) {
                    g_voices[i]->DestroyVoice();
                    g_voices[i] = g_voices.back();
                    g_voices.pop_back();
                } else {
                    ++i;
                }
            }
            const std::filesystem::path path = SoundPath(file.c_str());
            auto& wav = g_wavs[path.wstring()];
            if (wav == nullptr) wav = LoadWav(path);
            IXAudio2SourceVoice* voice = nullptr;
            if (wav != nullptr && g_voices.size() < 16 && SUCCEEDED(g_audio->CreateSourceVoice(&voice, &wav->format))) {
                XAUDIO2_BUFFER buf{};
                buf.AudioBytes = static_cast<UINT32>(wav->data.size());
                buf.pAudioData = wav->data.data();
                buf.Flags = XAUDIO2_END_OF_STREAM;
                voice->SetVolume(std::clamp(volume, 0.0f, 1.0f));
                if (SUCCEEDED(voice->SubmitSourceBuffer(&buf)) && SUCCEEDED(voice->Start(0))) {
                    g_voices.push_back(voice);
                    played = true;
                } else {
                    voice->DestroyVoice();
                }
            }
        }
    }
    if (out) *out = played;
}
