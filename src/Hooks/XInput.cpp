// XInput -- one hook, one file. It registers itself at the bottom; Hooks/Hook.hpp says why the
// stage and order live here rather than in a boot function.
//
// Merges the VR controller into gamepad 0 by patching the game's XInput imports. Opt-out
// via xr_xinput_install, because it is the one hook that changes how ordinary input
// reaches the game.

#include "Anim/WheelGrab.hpp"
#include "Anim/VrikState.hpp"   // g_VRLocomotionState, for the sprint crouch gate
#include "Core/VrCoreShared.hpp"
#include "Camera/CameraState.hpp"   // DeviceCamActive -- the stick belongs to the camera, not the player
#include "Core/LiveControls.hpp"
#include "Core/Telemetry.hpp"
#include "Hooks/Hook.hpp"
#include "Hooks/LookAtRayFromHand.hpp"   // VRPointingHandIsRight: one answer, two readers
#include "Hooks/Trampoline.hpp"
#include "Runtimes/OpenXRManager.hpp"
#include "Utils/AobScanner.hpp"
#include "Utils/MemorySafe.hpp"
#include "Utils/SharedSlots.hpp"

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <Xinput.h>

// DASH on the right stick pushed fully UP. 1 = on. A knob rather than a hardcoded gesture like
// sprint and crouch, because this one claims half of an axis that a user who turns "Disable Mouse Y"
// off is using for camera pitch -- and they should be able to have it back.
// DISCRETE MOVEMENT SPEED. 1 = a push past the deadzone moves at ONE speed whatever the deflection,
// which is what the keyboard does (LeftY_Axis binds IK_W at val="1.0"); 0 = the pad's analogue
// magnitude, where the speed rides the thumb.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_MoveTiers = 1;
// HOW LONG THE STICK MUST STAY AT THE STOP BEFORE IT SPRINTS, milliseconds. It used to be instant, and
// with discrete speed that is wrong by construction: any push already runs at full speed, so the stick
// naturally sits AT the stop and an instant detent sprints on every step. 200 ms separates walking
// forward from leaning on it.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_SprintHoldMs = 200;
// 0 (default, MEASURED) = a crouch blocks the detent. PSMLocomotionStates has a CrouchSprint state, so
// this was worth asking for -- and the answer, in game, was that the player is STOOD UP and sprints.
// That is the opposite of what a crouching player wants, so the block is the honest default. 1 asks
// anyway, for a build or a cyberware where the game answers differently.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_SprintFromCrouch = 0;
// THE RE-ENTRY BLIP. Sprint is a HOLD -- measured: ten press/release pulses never entered the sprint
// state once, while a held button always has. But a game that has LEFT the sprint (a dash, a jump, ADS)
// will not re-enter on a button that is already down, so the hold is briefly RELEASED to make the next
// hold an edge. Held for SprintPulseMs at a minimum between blips, released for SprintGapMs.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_SprintPulseMs = 240;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_SprintGapMs   = 60;
// After this long without reaching the sprint, stop blipping and just HOLD. Holding is the behaviour
// that shipped and worked, so it is the right thing to fall back to when the game is refusing us
// (no stamina, a state that forbids it) -- a blip every 300 ms forever would only flicker it.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_SprintGiveUpMs = 1500;
// The WALK band, opt-in: stick magnitudes up to this walk instead of run. 0 = no band, every push runs.
// It is off by default on purpose -- a band is a slice of travel that has to be HELD, which is the thing
// discrete speed exists to remove -- but the game gives a pad no walk button at all (ToggleWalk_Button
// is IK_G, a key an XInput hook cannot press), so a band is the only way to reach a walk.
extern "C" __declspec(dllexport) float CyberpunkVR_MoveWalkMax = 0.0f;
// What the walk band outputs, and how much wider the band gets once you are in it (so a thumb resting on
// the boundary does not flip between walk and run).
extern "C" __declspec(dllexport) float CyberpunkVR_MoveWalkMag    = 0.35f;
extern "C" __declspec(dllexport) float CyberpunkVR_MoveWalkMargin = 0.06f;

// SCANNER GESTURE -- left hand at the left ear + right stick click held = the scanner is held.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerGesture = 1;
// The ear, in HMD-LOCAL XR axes (x right, y up, z BACKWARD -- the published hand position is in the
// headset's own frame, so this rides the head and works whichever way the player faces). Slightly left,
// a little below eye level and a little behind the eyes: where a hand goes when it cups an ear.
extern "C" __declspec(dllexport) float CyberpunkVR_ScannerEarX = -0.10f;
extern "C" __declspec(dllexport) float CyberpunkVR_ScannerEarY = -0.03f;
extern "C" __declspec(dllexport) float CyberpunkVR_ScannerEarZ =  0.06f;
// How near the ear point counts, and how much wider it has to get before it stops counting. The margin
// is what keeps a hand sitting on the boundary from chattering.
extern "C" __declspec(dllexport) float CyberpunkVR_ScannerEarRadius = 0.18f;
extern "C" __declspec(dllexport) float CyberpunkVR_ScannerEarMargin = 0.04f;
// The two debounce windows, milliseconds. Asymmetric on purpose: arming is a decision, dropping is a
// dropout, so a bad tick has to be swallowed rather than believed.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerEnterMs = 120;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerExitMs  = 250;
// TOGGLE OR HOLD. The game's own binding is Vision_Hold_Button, i.e. the scanner stays open only
// while LB is asserted -- so a held gesture was the shape that matched it, and holding a grip at the
// ear for as long as the scanner is wanted is what that costs. With this on the gesture only FLIPS a
// latch and the port keeps LB asserted itself: squeeze at the ear to open, squeeze at the ear again
// to close, hand free in between. 0 restores the original hold.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerToggle = 1;
// THE HACK LIST ON THE LEFT STICK, up and down, at the threshold every other gesture in this file
// fires at. It REPLACES the face buttons for paging -- X and Y sat under the thumb of the hand that is
// already squeezing a grip at the ear, which is the reach the one-handed scanner exists to avoid, so X
// is now only the apply and Y does nothing. 0.90 to fire and 0.50 to re-arm are the snap turn's numbers
// for its reason: a resting thumb, or a wrist drifting while walking, must not page a list.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerStickNav      = 1;
extern "C" __declspec(dllexport) float   CyberpunkVR_ScannerStickNavFire  = 0.90f;
extern "C" __declspec(dllexport) float   CyberpunkVR_ScannerStickNavRearm = 0.50f;

// SCANNER, WORKED WITH ONE HAND. What each of these has to become is not a preference: the game
// binds the hack list to the D-PAD, applying to X, the scanner's tab to RB and the tag to the right
// stick click. The block that emits them, down in the merge, names the file every one of those was
// read out of. Each is separately switchable because each takes a button away from gameplay.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerFaceNav     = 1;   // left Y up, left X down / apply
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerTriggerTag  = 1;   // right trigger tags, and does not fire
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerStickTab    = 1;   // right stick click changes the tab
// THE SCANNER'S ZOOM: hold the LEFT trigger and the right stick zooms, in the game's own steps.
// PHYSICAL CROUCH -- the depth lives in LiveControls (xr_physical_crouch_depth, the Controls
// tab slider), not in an exported global, because it is a comfort setting a player tunes on
// their own body and it has to survive a restart.
//
// The height it compares against is shared[90], the NECK PIVOT rather than the optical centre,
// and that choice is the whole difference between working and not: the eyes swing forward and
// DOWN on their own arc when the head tilts, so looking at your feet reads as ~15 cm of squat
// on shared[89] and as nothing here. CharacterRig.cpp uses the same slot for the avatar's
// squat, for the same reason -- measured there as the #1 cause of a false squat.
//
// Standing up releases at this fraction of the depth. Two thresholds and not one because a
// single line chatters: a head resting exactly on it flips the crouch with breathing.
static constexpr float kPhysCrouchReleaseFrac = 0.60f;
// What the gate actually saw, for the readout under the Controls-tab slider. A crouch that
// does not fire has four possible reasons -- the switch, a context that forbids it, a height
// that never arrives, a height that never gets deep enough -- and they are indistinguishable
// from inside the headset without showing the number.
extern "C" __declspec(dllexport) float CyberpunkVR_DebugPhysCrouchDrop = 0.0f;
extern "C" __declspec(dllexport) float CyberpunkVR_DebugPhysCrouchRaw = 0.0f;
// bit0 = the switch is on, bit1 = the context allows it, bit2 = a height slot is arriving,
// bit3 = currently crouched.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_DebugPhysCrouchGate = 0;
// The other half of the answer: whether the R3 actually went out this poll, and what the game
// says its locomotion state is (PSMLocomotionStates: 1 = Crouch, 12 = CrouchSprint,
// 13 = dodge-from-crouch). A gate that fires while the state never becomes a crouch means the
// button is being emitted and something downstream is eating it -- a different failure from
// the gate never firing at all, and the two are indistinguishable without both numbers.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_DebugPhysCrouchSent = 0;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_DebugPhysCrouchLoco = 0;
// How many toggles the body has asked for. One per edge: down, then up.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_DebugPhysCrouchToggles = 0;

// The right stick is busy -- crouch, dash, pitch, snap turn -- so the trigger is the modifier that
// borrows it, and while it is held the axis is consumed before any of those four sees it.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerZoom        = 1;
// How far the stick must go for a step. Lower than the 0.90 the crouch and dash gestures use on
// purpose: those are deliberate shoves to the stop, this is a readout being nudged.
extern "C" __declspec(dllexport) float   CyberpunkVR_ScannerZoomStick   = 0.50f;
// How often a held stick repeats the step. ZoomIn_Button is a button, so holding it does not keep
// zooming -- the press has to be made again, and this is the rate.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerZoomRepeatMs = 200;
// How long X must be HELD to apply the selected hack instead of paging down one. The same split the
// vehicle exit uses and for the same reason: one button with two meanings, the cheap one on the tap.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerApplyHoldMs = 350;
// How long each synthesised press stays down. The game's listeners act on BUTTON_PRESSED, so this
// only has to outlast one XInput poll -- it is not a repeat rate.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_ScannerPulseMs     = 90;

// HOW LONG THE SYNTHETIC AUTODRIVE PRESS LASTS, milliseconds.
//
// Vehicle_Autodrive is IK_Pad_LeftThumb in the game's mappings -- the LEFT stick click, which in this
// port is also the D-pad modifier and, released bare, the vanilla click. Autodrive on that button is
// one slip away at any moment, and being handed to the autopilot mid-chase is not a small mistake. It
// moves to the RIGHT stick click, which this port has nothing else on while seated.
//
// The right click carries VehicleInverseCameraToggle_Button in flat, and that binding is given up
// without a fight: it swings the camera round to look behind the car, which is what a head is for in
// VR. The button is worth more as the autopilot than as a camera nobody in a headset will press.
//
// A PULSE, not a level press: Vehicle_Autodrive is a toggle the game acts on per press, so a held
// button either does nothing after the first frame or flips back and forth depending on how it
// debounces. 100 ms is long enough for any poll rate to catch the edge.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_VehicleClickPulseMs = 100;

// HOW LONG B MUST BE HELD TO LEAVE A CAR, milliseconds. Not zero, and that is the whole point:
// ExitVehicle_Button has no hold in the game's own mappings, so the vehicle state machine acts on
// the first frame it sees -- and at speed that means jumping out. Passed straight through, B ejected
// the player the moment it was brushed while driving, which reads as a physics bug rather than as a
// button. 400 ms is the timeout the game uses for its own hold bindings.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_VehicleExitHoldMs = 400;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_DashStickUp = 1;
// HOW FAR THE RIGHT STICK MUST GO TO SNAP-TURN, and how far back before it can snap again. 0.90 puts
// the snap where every other gesture in this file already is (sprint, crouch, dash) -- a deliberate
// push to the stop, not the half-deflection it used to be, which is where a thumb rests and where a
// wrist drifts while walking. The re-arm is not back at centre on purpose: 90 degrees is three snaps,
// and a full return between each makes that slow; 0.90/0.50 is a 0.40 band no jitter crosses.
extern "C" __declspec(dllexport) float CyberpunkVR_SnapTurnStickFire  = 0.90f;
extern "C" __declspec(dllexport) float CyberpunkVR_SnapTurnStickRearm = 0.50f;
// WHAT THE RIGHT STICK ACTUALLY REACHES, published for the Controls tab.
//
// The fire threshold above is applied to the stick AFTER the 0.18 deadzone has been rescaled out,
// so 0.90 means the raw axis has to reach 0.90 * (1 - 0.18) + 0.18 = 0.918 -- within a hair of the
// hardware stop. That is fine when the two directions travel equally and merciless when they do
// not: a stick whose right side tops out at 0.90 raw snaps left every time and right almost never,
// with nothing in the log to say so, because the code is symmetric and the hardware is not.
//
// These are the measurement that settles it. Peaks are held so a flick can be read after the fact,
// and reset when the tab asks.
extern "C" __declspec(dllexport) float CyberpunkVR_DebugRightStickX     = 0.0f;   // post-deadzone, live
extern "C" __declspec(dllexport) float CyberpunkVR_DebugRightStickRaw   = 0.0f;   // straight from OpenXR
extern "C" __declspec(dllexport) float CyberpunkVR_DebugRightStickPeakR = 0.0f;   // max raw reached pushing RIGHT
extern "C" __declspec(dllexport) float CyberpunkVR_DebugRightStickPeakL = 0.0f;   // max raw reached pushing LEFT
extern "C" __declspec(dllexport) int   CyberpunkVR_DebugRightStickReset = 0;      // set 1 to clear the peaks
// How long the synthetic dodge press is held, milliseconds. A dodge is a button PRESS, so the pulse
// only has to be long enough for the game to see an edge; held level it would dodge over and over.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_DashPulseMs = 100;

// The left trigger is claimed by the lighter gesture while it is armed; the gate says so.
extern "C" __declspec(dllexport) int CyberpunkVR_LtLighterGate;
// How many frames the wheel-hub horn gesture held the horn down. Kept from the period when the
// gesture had no button to press (see the horn block below): non-zero here with xr_wheel_horn on
// means the hub detection works, so anything still silent is the binding and not the gesture.
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_DebugWheelHornGestures = 0;

namespace { volatile bool g_sprintInputActive = false; }  // private to this hook

// XInput merge: hook XInputGetState in the loaded XInput1_*.dll and OR the VR
// controller snapshot from OpenXRManager into the gamepad state the game reads
// every frame. CP2077 already has full native gamepad bindings so movement,
// jump, dodge, fire/aim, weapon switch, reload, etc. all "just work" once the
// VR state lands in XINPUT_GAMEPAD.
// ===========================================================================

using XInputGetState_t = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetState_t g_realXInputGetState = nullptr;
static bool     g_xinputHooked = false;
static int      g_xinputSnapArmedDir = 0;       // currently latched stick direction so we don't fire while held
static DWORD    g_xinputSnapPulseStartMs = 0;   // when the current pulse began
static int      g_xinputSnapPulseDir = 0;       // direction of the active pulse (0 = idle)

extern "C" int GetSnapTurnPulseMs();

static SHORT FloatToSHORT(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (v >= 0.0f) ? static_cast<SHORT>(v * 32767.0f) : static_cast<SHORT>(v * 32768.0f);
}
static BYTE FloatToBYTE(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return static_cast<BYTE>(v * 255.0f);
}
// ONE KEYSTROKE, FOR THE SCANNER'S ZOOM. The game's own gamepad zoom IS the D pad: ZoomIn_Button
// is IK_Pad_DigitUp and ZoomOut_Button is IK_Pad_DigitDown. Emitting those worked and then paged
// the script list instead, because while the quickhack panel is focused UI_QuickHackPanel binds
// the same D pad to UI_MoveUp / UI_MoveDown -- the game's own collision, and every pad input that
// context leaves free falls through to something worse (D pad left/right are QH_MoveLeft/Right,
// R3 is Tag_Button and crouch, the shoulders carry a dozen actions between them).
//
// So the ACTION stays the game's and the KEY becomes ours: r6\input\CyberpunkVRPort_ScannerHud.xml
// declares ZoomIn / ZoomOut on IK_Insert and IK_Delete, which nothing in inputUserMappings.xml
// binds. This presses them. Down and up in one call: the game consumes raw input per frame, and a
// press that never releases would repeat by itself and defeat the step timer above.
static void SendZoomKey(bool zoomIn) {
    INPUT in[2] = {};
    // NOT VK_INSERT, and that is not a preference: this port's own ImGui overlay toggles on
    // VK_F10 OR VK_INSERT (Overlay/ImGuiOverlay.cpp), so zooming in was opening the overlay every
    // step. VK_END is the next key the game binds to nothing.
    const WORD vk = zoomIn ? VK_END : VK_DELETE;
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// THE HACK LIST, AS A KEY, and for the same reason the zoom above is one: the pad button the game binds
// the list to is the button it also binds the ZOOM to.
//
//     ZoomIn_Button   IK_Pad_DigitUp    IK_MouseWheelUp
//     UI_MoveUp       IK_Pad_DigitUp    IK_Q  IK_W  IK_MouseWheelUp  IK_Up
//
// (read out of r6\cache\inputUserMappings.xml, the merged file the game actually loads). So a synthetic
// D-pad press for the list also zoomed, which is what was reported. IK_Up / IK_Down are on UI_MoveUp /
// UI_MoveDown and on neither zoom mapping, so the arrows page the list and do nothing else. Nothing to
// declare: the game ships those keys on the action already.
//
// Extended-key flag for the same reason as the zoom's: the arrows ARE extended keys, and down+up in one
// call because the game consumes raw input per frame and a press that never releases repeats by itself.
static void SendListKey(bool up) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = up ? VK_UP : VK_DOWN;
    in[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static float ApplyStickDeadzone(float v, float dz) {
    float a = v < 0.0f ? -v : v;
    if (a < dz) return 0.0f;
    float s = v < 0.0f ? -1.0f : 1.0f;
    return s * (a - dz) / (1.0f - dz);
}

DWORD WINAPI HookedXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    DWORD r = ERROR_DEVICE_NOT_CONNECTED;
    if (g_realXInputGetState) r = g_realXInputGetState(dwUserIndex, pState);

    if (!pState) return r;
    if (dwUserIndex != 0) return r;
    if (g_liveControls.xrXInputHook == 0) return r;

    VRControllerState vr{};
    if (!OpenXRManager::Get().GetControllerState(&vr)) return r;

    if (r != ERROR_SUCCESS) {
        memset(pState, 0, sizeof(*pState));
        r = ERROR_SUCCESS;
    }

    // Buttons: OR (so a physical pad can still augment, and vice versa) -- except the two the port has
    // taken for itself, which are masked out here instead of reaching the pad:
    //
    //   RIGHT STICK CLICK (0x0080) is the physical reload's SLIDE RELEASE. The game had it on crouch,
    //   and crouch is the same stick pushed fully down (further below), so nothing is lost and a slide
    //   release no longer squats the player.
    //
    //   B (0x2000) is the physical reload's MAGAZINE DROP, which is what a thumb does on a real pistol.
    //   The game had it on DODGE, and a dodge on every magazine drop is not a compromise: it threw the
    //   player sideways in the middle of a reload. Dodge moves to a gesture of its own (see the dash
    //   block below) and is emitted from there, so the raw press means one thing again.
    // AND IT IS GAMEPLAY-ONLY, which the first version of this was not. Both of those buttons mean
    // something else outside gameplay, and taking them everywhere broke more than it fixed:
    //
    //   B is Exit_Button and ExitWheel_Button in menus, and ExitVehicle_Button in a car -- all three
    //   from the same mappings file. Masked unconditionally, there was no way to back out of a menu or
    //   to get out of a vehicle at all. A magazine drop only exists while a gameplay screen is up, so
    //   that is the only place the port needs the button.
    //
    //   IN A VEHICLE X IS THE EXIT, not the horn. Vehicle_Horn is X and ExitVehicle_Button is B, so
    //   the mask above had removed the only pad exit while leaving the horn -- exactly backwards for
    //   a seated player. X is taken here and re-emitted as B further down; B itself is left alone in a
    //   vehicle, both because it is the vanilla exit and because there is no physical reload in a car
    //   for the mask to protect.
    // B ONLY WHILE A WEAPON IS IN HAND, and that is a correction. The mask exists to protect the
    // physical reload's magazine drop -- and a magazine drop cannot happen with nothing to drop it
    // from. Taken on every gameplay screen instead, it also swallowed B where B is Exit_Button, so
    // the PHONE could not be closed: it is an overlay, not a menu, so g_menuModeValue stays 0 and
    // the mask applied. Holstered, B reaches the game again and closes it.
    //
    // (With a weapon actually drawn the phone still cannot be closed by B. Fixing that needs a
    // phone-open signal, and the game's own is the blackboard bool UI_ComDevice.ContactsActive --
    // which is what PhoneSystem.IsPhoneOpened reads. Not wired yet; this covers the common case.)
    const uint16_t kPortOwnedButtons =
        static_cast<uint16_t>(0x0080 | (g_hasWeaponEquipped ? 0x2000 : 0x0000));
    // B ONLY, and X is given back. B is ExitVehicle_Button, and the game acts on the first frame it
    // sees it -- at speed that means being thrown across the street. So B is held out of the merge
    // and re-emitted further down only after a deliberate hold; nothing else can eject.
    //
    // X USED TO BE MASKED HERE TOO, to feed a held-X-becomes-B exit. That cost the horn for nothing:
    // r6\config\inputUserMappings.xml puts Vehicle_Horn on IK_Pad_X_SQUARE / IK_Z, so masking X left
    // the button doing literally nothing in a car while the exit it was borrowed for could just as
    // well wait on B itself. X passes through again and honks, exactly as in flat, and the wheel-hub
    // gesture below synthesises the same X -- one horn, reached by button or by hand.
    //
    // THE RIGHT STICK CLICK IS TAKEN TOO. It is VehicleInverseCameraToggle_Button in flat -- the
    // look-back camera -- and it carries the autopilot here instead (see the autodrive block below),
    // so the physical press must not also reach the game as itself. Turning the view round is what a
    // head does in VR; the button is better spent.
    constexpr uint16_t kVehicleOwnedButtons = 0x2000 | 0x0080;   // B = held only; R3 = the autopilot
    const bool gameplayScreen = (g_menuModeValue == 0);
    const bool mounted = g_isInVehicle;
    uint16_t ownedNow = 0;
    if (gameplayScreen) ownedNow = mounted ? kVehicleOwnedButtons : kPortOwnedButtons;
    // ---- X AND A SWAPPED, SO THE MOVING THUMB NEVER HAS TO LEAVE THE STICK -----------------------
    //
    // X is Interact AND Reload, and on Touch controllers X is on the LEFT -- the same hand as the
    // movement stick. So every reload in a firefight and every prompt taken while walking costs the
    // player their locomotion for as long as the thumb is away. A is Jump, on the right, under a
    // thumb whose stick only turns -- and turning is intermittent where walking is continuous, which
    // makes the right thumb the cheaper one to borrow.
    //
    // THE COST IS JUMP, and it is real: jumping is the action most tied to moving, and this puts it
    // on the left hand. It is paid for separately by the jump GESTURE, which asks for no thumb at
    // all; until that is armed, this switch is a straight trade and is off by default.
    //
    // THREE THINGS IT DELIBERATELY DOES NOT TOUCH:
    //   * MENUS. A is Confirm in every dialog the game shows. Moving Confirm to the other controller
    //     mid-menu is disorienting in a way no amount of getting used to fixes, so the swap is
    //     gameplay only.
    //   * VEHICLES. In a car X is Vehicle_Horn and this port already has its own rules for B and R3;
    //     one change at a time.
    //   * THE SYNTHETIC PRESSES. Only the PHYSICAL bits are swapped, here, before the merge. The X
    //     the hand gestures emit further down stays X -- otherwise the first shard picked up with a
    //     grip would make the player jump.
    // The scanner block below keeps reading vr.buttons directly, so it still sees the physical left
    // X as its own; it masks the OUTPUT, which is the right place for it to act.
    uint16_t physicalButtons = vr.buttons;
    if (g_liveControls.xrSwapXA && gameplayScreen && !mounted) {
        const bool aDown = (physicalButtons & 0x1000) != 0;   // right controller
        const bool xDown = (physicalButtons & 0x4000) != 0;   // left controller
        physicalButtons &= static_cast<uint16_t>(~(0x1000 | 0x4000));
        if (aDown) physicalButtons |= 0x4000;   // right A -> the game's X: interact, reload
        if (xDown) physicalButtons |= 0x1000;   // left  X -> the game's A: jump
    }
    pState->Gamepad.wButtons |=
        (physicalButtons & static_cast<uint16_t>(~ownedNow));

    // MENU-ONLY: right grip = RB (right shoulder) for tab navigation to the RIGHT,
    // symmetric with the left grip's LB. The right grip is deliberately NEVER merged as
    // RB in gameplay -- there it is reserved for the hand-to-holster equip (published as
    // shared[49] above) and the D-pad modifier, and RB is a gameplay action that would
    // misfire on every holster reach. Menus run no holster logic and can't fire gameplay
    // actions, so the grip is safe as RB while one is open. Menu state = the native
    // menu-mode hook OR the redscript world-map bridge flag (shared[81]).
    {
        bool menuOpenForRb = (g_menuModeValue != 0);
        if (!menuOpenForRb) {
            if (float* sh = GetShotShared()) {
                if (reinterpret_cast<volatile uint32_t*>(sh)[81] != 0u) menuOpenForRb = true;
            }
        }
        if (menuOpenForRb && vr.rightGrip >= 0.7f) {
            pState->Gamepad.wButtons |= 0x0200; // XINPUT_GAMEPAD_RIGHT_SHOULDER
        }
        // And the LEFT grip's LB, on the same terms. It used to be emitted unconditionally from the frameloop,
        // which meant every gameplay squeeze of the left grip opened the SCANNER -- LB's gameplay binding. That
        // hand now grabs the magazine for the reload, so the two cannot share the input.
        if (menuOpenForRb && vr.leftGrip >= 0.7f) {
            pState->Gamepad.wButtons |= 0x0100; // XINPUT_GAMEPAD_LEFT_SHOULDER
        }
    }

    // Triggers: take the max so a physical squeeze isn't lost.
    // (VR melee block does NOT inject LT here: the gesture guard is STAT-driven — the CET weapon
    // mod sets IsBlocking/IsDeflecting directly, damageManager.script mitigates on those stats,
    // and the PSM Block state with its AimWalk/sprint debuffs is never entered. A physical left
    // trigger still reaches the game's own 'MeleeBlock' action through this merge as in flat.)
    BYTE lt = FloatToBYTE(vr.leftTrigger);
    BYTE rt = FloatToBYTE(vr.rightTrigger);
    // The VR left trigger reaches the gamepad LT (aim / zoom, melee block, VEHICLE BRAKE) unless
    // the smoking lighter has a claim on it, which is only true on foot with empty hands.
    //
    // The weapon test alone was too wide and cost the brake: no weapon is equipped while driving,
    // so LT did nothing at all in a vehicle -- reported as "LT does not work" and correctly so.
    // The lighter is not in anyone's hand behind a steering wheel, so the vehicle flag we already
    // maintain for VRIK settles it. xr_lt_needs_weapon=0 drops the gate entirely for anyone who
    // does not use the smoking mod and would rather have vanilla LT everywhere.
    const bool ltClaimedByLighter =
        CyberpunkVR_LtLighterGate != 0 && !g_hasWeaponEquipped && !g_isInVehicle;
    if (!ltClaimedByLighter && lt > pState->Gamepad.bLeftTrigger)  pState->Gamepad.bLeftTrigger  = lt;

    // ---- SCANNER: left hand to the left ear, right stick click held -----------------------------
    //
    // The game's own scanner is a HOLD on LB (Vision_Hold_Button = IK_Tab + IK_Pad_LeftShoulder), so
    // SOMETHING has to keep LB asserted the whole time it is open. By default that something is now a
    // latch this port flips on the gesture (CyberpunkVR_ScannerToggle) rather than the player's grip. Computed HERE, above the slot publishes, because it
    // CONSUMES the stick click -- see the kRightStickClick publish below.
    //
    // The hand arms the zone on its own and the click gates the output, so a click with the hand
    // already at the ear takes effect on the same frame.
    bool scannerHold = false;
    {
        static bool   s_earArmed = false;
        static bool   s_gesture = false;      // the raw gesture this frame: hand at the ear + grip
        static bool   s_prevGesture = false;  // ...and the previous frame, for the rising edge
        static bool   s_latch = false;        // the toggled state the port asserts LB from
        static double s_inMs = 0.0;    // continuous time INSIDE the zone
        static double s_outMs = 0.0;   // continuous time OUTSIDE it
        static LARGE_INTEGER s_qpc = {};

        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        double dtMs = 0.0;
        if (s_qpc.QuadPart != 0 && freq.QuadPart > 0) {
            dtMs = static_cast<double>(now.QuadPart - s_qpc.QuadPart) * 1000.0
                 / static_cast<double>(freq.QuadPart);
        }
        s_qpc = now;
        if (dtMs < 0.0) dtMs = 0.0;
        if (dtMs > 100.0) dtMs = 100.0;   // a hitch must not satisfy a whole window by itself

        if (CyberpunkVR_ScannerGesture != 0) {
            // Left hand, HMD-local: [0] valid, [1..3] position. An INVALID sample counts as absence --
            // it feeds the exit timer rather than dropping the scan, because a dropout is exactly what
            // that timer exists to ride out.
            bool inZone = false;
            if (float* shScan = GetShotShared()) {
                if (shScan[0] > 0.5f) {
                    float rEnter = CyberpunkVR_ScannerEarRadius;
                    if (!(rEnter > 0.02f) || rEnter > 0.60f) rEnter = 0.18f;
                    float margin = CyberpunkVR_ScannerEarMargin;
                    if (!(margin >= 0.0f) || margin > 0.30f) margin = 0.04f;
                    const float r = s_earArmed ? (rEnter + margin) : rEnter;
                    const float dx = shScan[1] - CyberpunkVR_ScannerEarX;
                    const float dy = shScan[2] - CyberpunkVR_ScannerEarY;
                    const float dz = shScan[3] - CyberpunkVR_ScannerEarZ;
                    inZone = (dx * dx + dy * dy + dz * dz) <= (r * r);
                }
            }

            if (inZone) { s_inMs += dtMs; s_outMs = 0.0; }
            else        { s_outMs += dtMs; s_inMs = 0.0; }

            const double enterMs = (CyberpunkVR_ScannerEnterMs > 0) ? CyberpunkVR_ScannerEnterMs : 120;
            const double exitMs  = (CyberpunkVR_ScannerExitMs  > 0) ? CyberpunkVR_ScannerExitMs  : 250;
            if (!s_earArmed && s_inMs  >= enterMs) s_earArmed = true;
            if ( s_earArmed && s_outMs >= exitMs)  s_earArmed = false;

            // THE LEFT GRIP, which is the hand that is already at the ear. The right stick click
            // used to do this, and it asked for two hands and a thumb reach for a one-hand gesture;
            // squeezing the hand you just raised is the whole motion. Threshold matches the menu
            // LB mapping above so one number describes "the left grip is held" everywhere.
            //
            // Gameplay screen only: in menus LB pages the popup list, which is not what a hand near
            // a headset should do. And the ear zone is what keeps this off the reload -- the
            // magazine grab is a left grip too, but it happens at the weapon, not at the head.
            const bool gripHeld = vr.leftGrip >= 0.7f;
            s_gesture = s_earArmed && gripHeld && (g_menuModeValue == 0);
        } else {
            s_earArmed = false;
            s_inMs = 0.0;
            s_outMs = 0.0;
            s_gesture = false;
            s_latch = false;          // the gesture is switched off: nothing may stay asserted
        }

        // THE LATCH, so the hand does not have to stay squeezed at the ear for as long as the scanner is
        // wanted. Only the RISING edge of the gesture is used and it flips the state; the port then keeps
        // LB asserted for as long as the latch is on, which is what Vision_Hold_Button needs.
        //
        // Evaluated OUTSIDE the ear-zone block on purpose: s_earArmed drops as soon as the hand leaves
        // (with its exit hysteresis), and that must not close a scanner the latch says is open.
        if (CyberpunkVR_ScannerToggle != 0) {
            if (s_gesture && !s_prevGesture) s_latch = !s_latch;
            if (g_menuModeValue != 0) s_latch = false;   // a menu owns LB; never leave it stuck
            scannerHold = s_latch;
        } else {
            s_latch = false;
            scannerHold = s_gesture;
        }
        s_prevGesture = s_gesture;
    }

    // ---- THE SCANNER, WORKED WITH THE HAND THAT RAISED IT ---------------------------------------
    //
    // WHAT THE GAME LISTENS FOR, read out of its own files rather than guessed:
    //
    //   quickhacks.swift registers UI_MoveUp, UI_MoveDown and UI_ApplyAndClose. inputContexts.xml
    //   resolves the first two to the mappings of the same name and the third to
    //   ApplyAndCloseQHackWidget; inputUserMappings.xml binds those to IK_Pad_DigitUp,
    //   IK_Pad_DigitDown and IK_Pad_X_SQUARE. hudManager.swift changes the scanner's TAB on
    //   DescriptionChange, which resolves to ToggleQHackDescription = IK_Pad_RightShoulder. The tag
    //   is Tag_Button = IK_Pad_RightThumb.
    //
    // So: the hack list is a D-pad, applying is X, the tab is RB, the tag is the right stick click --
    // and not one of those is reachable one-handed in VR. The D-pad only existed behind the
    // left-stick-click chord, which asks for the second hand and a thumb reach in the middle of a
    // gesture the other hand is already holding, and RB is never emitted in gameplay at all.
    //
    //   left stick up     -> IK_Up        page up the hack list   (NOT the D-pad: that is also the zoom)
    //   left stick down   -> IK_Down      page down
    //   X                 -> X            apply the selected hack
    //   right trigger     -> R3            tag the target
    //   right stick click -> RB            change the scanner tab
    //
    // THE TAP/HOLD SPLIT ON X IS DECIDED AT THE RELEASE, and it has to be. X is both the page-down
    // and the apply, so acting on the press would step the selection and then apply -- applying the
    // hack BELOW the one that was looked at, every single time. Held past the threshold the apply
    // fires and the release is swallowed; released before it, only the page-down fires.
    //
    // X AND Y ARE TAKEN OUT OF THE MERGE while the gesture is held, or the game sees their gameplay
    // meanings underneath -- X is Takedown_Button, PickUpBodyFromTakedown_Button and Reload_Button,
    // Y is SwitchItem_Button and WeaponWheel_Button. The right stick click is already the port's own
    // (the slide release) and never reaches the game, so it needs no mask.
    //
    // EDGE-TRIGGERED WITH A RE-ARM, like the dash: the listeners act on BUTTON_PRESSED, so a held bit
    // is one press and anything else would be inventing a repeat rate. A pulse already in flight is
    // deliberately NOT cut short when the gesture ends -- applying a hack closes the panel and the
    // hand relaxes in the same breath, and a swallowed apply is worse than a 90 ms D-pad tail.
    bool scanDpadUp = false, scanDpadDown = false, scanApply = false, scanTag = false, scanTab = false;
    {
        static bool     s_xWasDown     = false;
        static bool     s_r3WasDown    = false;
        static bool     s_rtWasDown    = false;
        static uint64_t s_upUntilMs = 0, s_downUntilMs = 0, s_applyUntilMs = 0,
                        s_tagUntilMs = 0, s_tabUntilMs = 0;

        const uint64_t now = GetTickCount64();
        const uint64_t pulseMs = (CyberpunkVR_ScannerPulseMs > 0)
                                     ? static_cast<uint64_t>(CyberpunkVR_ScannerPulseMs) : 90;
        // CyberpunkVR_ScannerApplyHoldMs is no longer read: the tap/hold split it timed existed only
        // while X was both the page-down and the apply. The knob stays exported so an existing
        // vrport.ini keeps parsing.

        if (scannerHold && CyberpunkVR_ScannerFaceNav != 0) {
            const bool xDown = (vr.buttons & 0x4000) != 0;
            const bool yDown = (vr.buttons & 0x8000) != 0;

            // APPLY IS A PLAIN PRESS OF X. The tap/hold split existed only because X had to be both
            // the page-down and the apply, and acting on the press would then have stepped the selection
            // and applied the hack BELOW the one being looked at, every single time. With the paging on
            // the stick there is nothing left to disambiguate.
            if (xDown && !s_xWasDown) s_applyUntilMs = now + pulseMs;
            s_xWasDown = xDown;

            // Y NO LONGER PAGES THE LIST. It stays masked below all the same: its gameplay meanings are
            // SwitchItem_Button and WeaponWheel_Button, and opening the weapon wheel over an open
            // scanner is not an improvement on doing nothing.
            (void)yDown;

            // The tag, on a travel the finger cannot chatter through: 0.65 to arm, 0.35 to release, so
            // one squeeze is one tag and a finger resting on the trigger is not a stream of them.
            if (CyberpunkVR_ScannerTriggerTag != 0) {
                const bool rtDown = s_rtWasDown ? (vr.rightTrigger > 0.35f)
                                                : (vr.rightTrigger > 0.65f);
                if (rtDown && !s_rtWasDown) s_tagUntilMs = now + pulseMs;
                s_rtWasDown = rtDown;
            } else {
                s_rtWasDown = false;
            }

            if (CyberpunkVR_ScannerStickTab != 0) {
                const bool r3Down = (vr.buttons & 0x0080) != 0;
                if (r3Down && !s_r3WasDown) s_tabUntilMs = now + pulseMs;
                s_r3WasDown = r3Down;
            } else {
                s_r3WasDown = false;
            }

            // Both face buttons belong to the scanner for as long as it is up.
            pState->Gamepad.wButtons &= static_cast<uint16_t>(~(0x4000 | 0x8000));
        } else {
            s_xWasDown  = false;
            s_r3WasDown = false;
            s_rtWasDown = false;
        }

        scanDpadUp   = (now < s_upUntilMs);
        scanDpadDown = (now < s_downUntilMs);
        scanApply    = (now < s_applyUntilMs);
        scanTag      = (now < s_tagUntilMs);
        scanTab      = (now < s_tabUntilMs);
    }

    // Publish whether the VR right trigger is held (shared[30]) so the CET melee mod can use it as the
    // power/strong modifier (hold = strong attack).
    OpenXRManager::Get().SetSharedSlot(30, (vr.rightTrigger > 0.5f) ? 1.0f : 0.0f);
    // ...and the same trigger as an ANALOG value, which a flag cannot stand in for: an action worked progressively
    // needs the whole travel, not the half-way mark. The physical reload rides a revolver's hammer on it.
    OpenXRManager::Get().SetSharedSlot(vrshared::kRightTriggerAnalog, vr.rightTrigger);
    // Right grip is published as a BINARY pressed/not-pressed flag in shared[49] (a previously free
    // slot — [50]/[51]/[52] are owned by the camera-trace producer and we were stomping on them).
    // The CET hand-to-holster mod reads this + the IN-GAME wrist-to-hip distances the plugin
    // publishes from the live FK pose to decide whether reaching for a visual holster + a grip
    // press should equip / unequip the corresponding weapon.
    OpenXRManager::Get().SetSharedSlot(49, vr.rightGrip > 0.5f ? 1.0f : 0.0f);
    // LEFT hand, for the smoking mod: the lighter is ignited by the left trigger and the cigarette
    // is taken to and from the mouth with either grip, so it needs all three. Only the right grip
    // was ever published; the left pair had no channel at all, and the CET bridge was reading [67]
    // and [68] on the strength of a map comment that called them free. They are not: [67] carries
    // the hand-sample millisecond stamp and [68] a QPC timestamp, both far above any threshold, so
    // the lighter read as permanently at full trigger and the left grip as permanently held.
    OpenXRManager::Get().SetSharedSlot(vrshared::kLeftTriggerAnalog, vr.leftTrigger);
    OpenXRManager::Get().SetSharedSlot(vrshared::kLeftGripPressed,
                                       vr.leftGrip > 0.5f ? 1.0f : 0.0f);
    // THE LEFT GRIP IS NOT REACHING THE INTERACT GESTURE, and the CET log says so plainly: every
    // ON IT line of a session carries `grip L=false`, including the ones where only the LEFT hand is
    // armed, while `grip R=true` appears whenever the right is squeezed. That leaves two causes with
    // opposite fixes -- the runtime never reports the left squeeze at all, or it reports it and the
    // slot is overwritten downstream -- and one measurement separates them. Both the ANALOG input
    // and the SLOT AS READ BACK are printed, because only the pair can tell them apart.
    {
        static bool     s_loggedR = false, s_loggedL = false;
        static float    s_maxR = 0.0f, s_maxL = 0.0f;
        static uint64_t s_reportAt = 0;
        static int      s_reports = 0;
        if (vr.rightGrip > s_maxR) s_maxR = vr.rightGrip;
        if (vr.leftGrip  > s_maxL) s_maxL = vr.leftGrip;
        if (!s_loggedR && vr.rightGrip > 0.5f) {
            s_loggedR = true;
            Log("grip probe: RIGHT crossed 0.5 (analog %.2f)\n", vr.rightGrip);
        }
        if (!s_loggedL && vr.leftGrip > 0.5f) {
            s_loggedL = true;
            const float slotBack = g_pSharedHands ? g_pSharedHands[vrshared::kLeftGripPressed] : -1.0f;
            Log("grip probe: LEFT crossed 0.5 (analog %.2f, slot[155] reads %.2f)\n",
                vr.leftGrip, slotBack);
        }
        // Only until the left has been seen once, and capped: a report every ten seconds forever is
        // the kind of logging this port has had to delete before.
        const uint64_t nowMs = GetTickCount64();
        if (!s_loggedL && nowMs > s_reportAt && s_reports < 12) {
            s_reportAt = nowMs + 10000;
            ++s_reports;
            const float slotBack = g_pSharedHands ? g_pSharedHands[vrshared::kLeftGripPressed] : -1.0f;
            Log("grip probe: max analog so far R=%.2f L=%.2f, slot[155] reads %.2f -- left not seen yet\n",
                s_maxR, s_maxL, slotBack);
        }
    }
    // Face buttons B / Y as flags, for gestures that want a button (the physical reload drops the magazine on
    // B). Y still reaches the game through the merge above; B no longer does -- it is the port's now, so a
    // magazine drop cannot also dodge.
    OpenXRManager::Get().SetSharedSlot(vrshared::kRightSecondaryBtn, (vr.buttons & 0x2000) ? 1.0f : 0.0f);
    OpenXRManager::Get().SetSharedSlot(vrshared::kLeftSecondaryBtn,  (vr.buttons & 0x8000) ? 1.0f : 0.0f);
    // The right stick click, which the merge above deliberately does NOT pass on: the physical reload uses it as
    // the slide release. Published as a flag; the Lua side takes the rising edge.
    // The scanner used to hold this click and the publish was suppressed while it did. It fires on
    // the left grip now, so the click means one thing again and needs no guard.
    OpenXRManager::Get().SetSharedSlot(vrshared::kRightStickClick,
                                       (vr.buttons & 0x0080) ? 1.0f : 0.0f);
    // One switch for the whole port. The launcher's DEBUG checkbox already gates the plugin's own
    // chatter; republishing it here lets the CET bridges obey it too, live, without each of them
    // growing a setting of its own that nobody remembers to turn off.
    OpenXRManager::Get().SetSharedSlot(vrshared::kDebugLog, g_verboseLog ? 1.0f : 0.0f);
    OpenXRManager::Get().SetSharedSlot(vrshared::kJumpGestureOn,
                                       g_liveControls.xrJumpGesture ? 1.0f : 0.0f);
    // THE BODY-ROTATION GATE, CLOSED FROM A PATH THAT ACTUALLY RUNS WHILE MOUNTED.
    //
    // The mirror and the realign are both maintained by the ON-FOOT heading hook, and that hook does
    // not execute in a vehicle -- so a first attempt at this closed the gate INSIDE that hook and
    // never ran at all in the case it was written for. The values simply stayed at whatever they
    // held when the player got in.
    //
    // What that costs is not subtle, and the field named it exactly: with the mirror stuck on, the
    // pose path swaps the hand frame's yaw for the engine's body-yaw census (AnimPose ~1301/1770) --
    // a value already known to be wrong while mounted, since the [vehyaw] census reads flat 0 deg
    // through a 260 deg turn. The skeleton is meanwhile rotating the hands with the car by itself.
    // Two applications of the same rotation, same direction: steer 45 degrees and the hands swing 90,
    // and they keep running away for as long as the wheel is turned. There is no recovery in it --
    // what looked like "the hands realign after a few seconds" was the wheel happening to pass under
    // them on the way round.
    if (g_isInVehicle) {
        CyberpunkVR_BodyYawFollow = 0;
        if (CyberpunkVR_BodyYawRealignRad != 0.0f) {
            CyberpunkVR_BodyYawRealignRad = 0.0f;
            CyberpunkVR_DebugBodyFollowOffsetDeg = 0.0f;
        }
    }

    VRUpdatePointingHand();
    // 2 = the hand-pointing feature is OFF, so nothing is restricted to one hand and the reach
    // gesture goes back to accepting either. Without this the last hand chosen before the checkbox
    // was unticked would keep restricting the gesture -- a setting that is off would still be
    // deciding something, which is the worst kind of leftover.
    OpenXRManager::Get().SetSharedSlot(
        vrshared::kPointingHand,
        g_liveControls.xrHandInteract ? static_cast<float>(VRPointingHand()) : 2.0f);
    // Melee RT IMPULSE (shared[29] = a frame countdown the CET mod raises on a detected VR swing): tap RT
    // so the game enters its NATIVE melee-attack state (full native damage/combo/numbers/markers), then
    // count it down. Otherwise merge the physical trigger into RT normally (guns shooting / held attack).
    float meleeImpulse = OpenXRManager::Get().GetSharedSlot(29);
    if (meleeImpulse > 0.5f) {
        pState->Gamepad.bRightTrigger = 255;
        OpenXRManager::Get().SetSharedSlot(29, meleeImpulse - 1.0f);
    } else {
        if (rt > pState->Gamepad.bRightTrigger) pState->Gamepad.bRightTrigger = rt;
    }
    // THE PORT'S OWN SAY OVER THE TRIGGER, last, so it beats both the physical pad and the merge above.
    //   1 = SWALLOW it. A revolver with its cylinder swung out has nothing under the hammer and must not fire, and
    //       this is the only honest way to say so: the alternative tried first was emptying the magazine behind the
    //       game's back, and putting it back cost the player real ammunition out of the reserve every time.
    //   2 = PRESS it fully. A hammer already cocked lets go under a touch -- single action -- so the port decides
    //       where in the travel the shot falls instead of the game's own threshold.
    const float trgMode = OpenXRManager::Get().GetSharedSlot(vrshared::kTriggerOverride);
    if (trgMode > 1.5f)      pState->Gamepad.bRightTrigger = 255;
    else if (trgMode > 0.5f) pState->Gamepad.bRightTrigger = 0;

    // THE SCANNER'S TRIGGER IS A TAG AND NOT A SHOT. While the gesture is held the trigger marks the
    // target (Tag_Button, emitted further down), and a tag that also fires a round is a tag nobody can
    // use where tagging matters: it spends real ammunition and announces the player. Zeroed AFTER the
    // override above so the scanner wins over a cocked hammer, which costs nothing -- one of them wants
    // the left hand at the ear and the other wants it at the weapon, so they cannot both be true.
    if (scannerHold && CyberpunkVR_ScannerTriggerTag != 0) pState->Gamepad.bRightTrigger = 0;

    // Left stick = locomotion (always merged when magnitude exceeds the
    // physical pad's so the game uses our values).
    float lx = ApplyStickDeadzone(vr.leftThumbX, 0.12f);
    float ly = ApplyStickDeadzone(vr.leftThumbY, 0.12f);

    // HOW FAR THE STICK IS ACTUALLY PUSHED, kept before the quantiser below rewrites it. The gesture
    // that means "to the stop" -- the sprint detent further down -- has to read the player's own
    // deflection: after quantising, every forward push is exactly 1.0, so a detent tested on the
    // output would sprint on every single step.
    // THE HACK LIST ON THE LEFT STICK, UP AND DOWN, TO THE STOP.
    //
    // The left hand is the one that raised the scanner, so its stick is under the thumb that is already
    // there. Reaching across to the face buttons in the middle of a held gesture is the very thing the
    // one-handed scanner exists to avoid, which is why X and Y no longer page the list at all.
    //
    // HERE, AND NOT LOWER DOWN. The sprint gesture below reads the RAW deflection (lyDetent, taken on the
    // next line) and asserts L3 at full forward, so a push meant to page the list would have sprinted
    // instead. Taking the axis after that point is too late; taking it here is seen by the sprint, by the
    // detent quantiser and by the movement the game receives.
    //
    // AND THE AXIS IS ONLY TAKEN AT THE STOP. Below the threshold the stick still walks -- the same trade
    // the sprint and the crouch make in this file, "a partial push is left as the game's normal jog" --
    // so a scanning player can still move.
    //
    // EDGE-TRIGGERED WITH A RE-ARM, like every other gesture here: the stick must come back below the
    // re-arm before it can fire again, so a held stick is one page rather than a repeat rate we invented.
    //
    // AND IT SENDS A KEY, NOT A D-PAD PRESS. IK_Pad_DigitUp/Down are on UI_MoveUp/UI_MoveDown AND on
    // ZoomIn_Button/ZoomOut_Button, so a synthetic D-pad press paged the list and zoomed at the same
    // time. See SendListKey for the mappings this was read out of.
    if (scannerHold && CyberpunkVR_ScannerFaceNav != 0 && CyberpunkVR_ScannerStickNav != 0 &&
        !g_isInVehicle && (g_menuModeValue == 0)) {
        static int s_navArmedDir = 0;

        float navFire = CyberpunkVR_ScannerStickNavFire;
        if (!(navFire > 0.05f) || navFire > 1.0f) navFire = 0.90f;
        float navRearm = CyberpunkVR_ScannerStickNavRearm;
        if (!(navRearm >= 0.0f) || navRearm >= navFire) navRearm = navFire * 0.55f;

        int navDir = 0;
        if (ly > navFire) navDir = +1;
        else if (ly < -navFire) navDir = -1;

        if (fabsf(ly) < navRearm) s_navArmedDir = 0;
        if (navDir != 0 && navDir != s_navArmedDir) {
            s_navArmedDir = navDir;
            SendListKey(navDir > 0);
        }

        if (navDir != 0) ly = 0.0f;   // at the stop the axis is the list's, not the legs'
    }

    const float lyDetent = ly;

    // ONE SPEED PER PUSH. The pad's analogue magnitude is the odd one out in this game: the keyboard
    // binds the same axis at val="1.0", so W runs, sprint is its own key and walk is its own toggle. A
    // thumbstick on a headset is the worst place for an analogue speed -- it rides every tremor, and a
    // push meant as a run comes out as a walk. So past the deadzone the magnitude is constant and only
    // the DIRECTION comes from the stick.
    //
    // Scaled as a 2D vector, not per axis: normalising the components separately would send (1,1) on a
    // diagonal, which is sqrt(2) of the speed sideways-forward -- the classic diagonal-is-faster bug.
    //
    // NOT IN A VEHICLE. There the left stick is not locomotion at all: X is steering, Y is lean, and Y
    // is what the shoot-while-driving throttle trim integrates -- a quantised +/-1 would take the
    // throttle from idle to floored in one frame.
    {
        static bool s_walking = false;
        const float mag = sqrtf(lx * lx + ly * ly);
        if (CyberpunkVR_MoveTiers != 0 && !g_isInVehicle && mag > 1e-4f) {
            float outMag = 1.0f;
            float walkMax = CyberpunkVR_MoveWalkMax;
            if (!(walkMax > 0.0f) || walkMax > 0.95f) walkMax = 0.0f;   // 0 = no band
            if (walkMax > 0.0f) {
                float margin = CyberpunkVR_MoveWalkMargin;
                if (!(margin >= 0.0f) || margin > 0.30f) margin = 0.06f;
                // Wider on the way out than on the way in, so the boundary cannot chatter.
                s_walking = (mag <= (s_walking ? walkMax + margin : walkMax));
                if (s_walking) {
                    outMag = CyberpunkVR_MoveWalkMag;
                    if (!(outMag > 0.05f) || outMag > 1.0f) outMag = 0.35f;
                }
            }
            const float k = outMag / mag;
            lx *= k;
            ly *= k;
        } else if (mag <= 1e-4f) {
            s_walking = false;   // released: the next push decides again from scratch
        }
    }

    if (fabsf(lx) > fabsf(pState->Gamepad.sThumbLX / 32767.0f)) pState->Gamepad.sThumbLX = FloatToSHORT(lx);
    if (fabsf(ly) > fabsf(pState->Gamepad.sThumbLY / 32767.0f)) pState->Gamepad.sThumbLY = FloatToSHORT(ly);

    // WHEEL STEERING. While a hand is holding the wheel, the tilt of the line through the controllers
    // IS the left stick X -- the pose hook has already turned it into a stick value and faded it by the
    // grab blend, so releasing the wheel hands the steering back over the same tenth of a second the
    // arm takes to return. ASSIGNMENT, not max(): the whole point is that the wheel drives the car, and
    // a thumb still resting on the stick must not fight it. Y is left alone (throttle and brake are the
    // triggers).
    //
    // ONE DEPARTURE from the upstream version, and it is about the 110 ms AFTER you leave a car: his
    // condition is the grab blends alone, and those fade out over kReleaseSec rather than dropping, so
    // for a tenth of a second on foot the left stick's X was still being ASSIGNED the (now zero)
    // steering -- i.e. no strafing right after stepping out of a vehicle. Gated on driving as well, the
    // fade still does its job where it was meant to (releasing the grip while still at the wheel) and
    // the stick comes back the instant the seat is empty.
    {
        const float bR = cvr::anim::g_wheelBlendRight.load(std::memory_order_relaxed);
        const float bL = cvr::anim::g_wheelBlendLeft.load(std::memory_order_relaxed);
        if (g_isDriving.load(std::memory_order_relaxed) && (bR > 0.01f || bL > 0.01f)) {
            float steer = cvr::anim::g_wheelSteer.load(std::memory_order_relaxed);
            if (steer >  1.0f) steer =  1.0f;
            if (steer < -1.0f) steer = -1.0f;
            pState->Gamepad.sThumbLX = FloatToSHORT(steer);
        }
    }

    // HORN. A hand laid on the middle of the wheel = pad X, which is what the game's own Vehicle_Horn
    // mapping listens to (r6\config\inputUserMappings.xml: IK_Pad_X_SQUARE / IK_Z). Level-triggered,
    // so the horn sounds for as long as the hand stays on the hub.
    //
    // Gated on g_isDriving here as well as in the pose hook: the mask is only rewritten inside a fresh
    // VRIK solve, and a mask left raised by a solve that stopped must not press X on foot, where the
    // same button is a gameplay action (reload).
    // THE HORN WAS TAKEN OUT TO PAY FOR A DETOUR, and it is back. The removal (0.1.2) argued that
    // Vehicle_Horn is X, the exit is X, therefore a hand on the hub would eject the player -- so the
    // gesture was left counting itself with nothing to press and xr_wheel_horn was defaulted off.
    //
    // The premise was half right and the conclusion was wrong. Vehicle_Horn IS X -- verified against
    // the shipped r6\config\inputUserMappings.xml, which has it on IK_Pad_X_SQUARE / IK_Z. What was
    // never true is that the exit needed X: ExitVehicle_Button is IK_F + IK_Pad_B_CIRCLE, i.e. B, and
    // the only reason the port had borrowed X was to put a HOLD in front of a button the game acts on
    // instantly. That hold now sits on B itself, where it belongs, and X is nobody's but the horn's.
    //
    // Level-triggered, so the horn sounds for exactly as long as the hand stays on the hub, which is
    // what a real horn does. Gated on g_isDriving here as well as in the pose hook: the mask is only
    // rewritten inside a fresh VRIK solve, and a mask left raised by a solve that stopped must not
    // press X on foot, where the same button is a gameplay action.
    bool hornPress = false;
    if (g_isDriving.load(std::memory_order_relaxed) && g_liveControls.xrWheelHorn != 0) {
        const int hornMask = cvr::anim::g_wheelHornMask.load(std::memory_order_relaxed);
        if (hornMask != 0) {
            hornPress = true;
            ++CyberpunkVR_DebugWheelHornGestures;
        }
        // A TAP ON THE HAND THAT PRESSED IT. The hub gives no feedback of its own: a hand laid on it
        // sounds the horn, and until the sound comes back there is nothing telling the player the
        // gesture was even recognised -- which in a noisy street is a real gap.
        //
        // ON THE EDGE, not on the level. The horn is deliberately level-triggered so it sounds for
        // as long as the hand rests there; pulsing on the level would run the motor for the whole
        // blast. The hand is taken from the mask, so it is the hand that actually did it.
        {
            static int s_wasHorn = 0;
            if (hornMask != 0 && s_wasHorn == 0) {
                const bool right = (hornMask & vrshared::kWheelArmedRightBit) != 0;
                OpenXRManager::Get().ApplyHaptic(right ? 1 : 0, 0.55f, 0.030f);
            }
            s_wasHorn = hornMask;
        }
    }

    // DRIVING WITH A GUN OUT. Vanilla puts the throttle and the trigger finger on the same hand: the
    // throttle is RT (Acceleration_Axis) and firing your own weapon from the driver seat is RB
    // (VehicleDriverCombatRangedAttack_Button) -- both from the game's own inputUserMappings.xml. That
    // split works with a thumb and a finger on one pad; it does not work when the right hand is holding
    // the gun and the left is on the wheel.
    //
    // So while a weapon is equipped in the driver seat:
    //   * the right trigger becomes FIRE (pressed through as RB),
    //   * the throttle is LATCHED at whatever it was the instant the weapon came out, so the car keeps
    //     rolling with nothing holding RT,
    //   * the left stick's Y TRIMS that latched throttle (forward = more, back = less).
    // Holstering ends all three and the trigger is the throttle again, no state left behind.
    //
    // The trim axis is CONSUMED (zeroed, and the sprint gesture on it suppressed): in a vehicle stick Y
    // is lean/rock and its CLICK is Vehicle_Autodrive, which our full-forward gesture asserts as L3 --
    // a throttle trim that handed the car to the autopilot at the top of its travel would be worse than
    // no trim at all. Stick X is untouched, so stick steering still works for anyone not holding the
    // wheel.
    bool vehGunMode = false;
    {
        static bool  s_vehGunPrev = false;
        static float s_vehThrottle = 0.0f;
        static LARGE_INTEGER s_vehGunQpc = {};

        vehGunMode = (g_isDriving.load(std::memory_order_relaxed) && g_hasWeaponEquipped
                      && g_liveControls.xrVehicleGunTrigger != 0);
        if (vehGunMode) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            float dt = 0.0f;
            if (s_vehGunPrev && s_vehGunQpc.QuadPart != 0 && freq.QuadPart > 0) {
                dt = static_cast<float>(static_cast<double>(now.QuadPart - s_vehGunQpc.QuadPart)
                                        / static_cast<double>(freq.QuadPart));
            }
            if (dt < 0.0f) dt = 0.0f;
            if (dt > 0.10f) dt = 0.10f;   // a hitch must not slam the throttle
            s_vehGunQpc = now;

            // Freeze on the way in: whatever the trigger (ours or a physical pad's) was asking for on
            // the last frame before the weapon appeared is the speed the car holds.
            if (!s_vehGunPrev) s_vehThrottle = pState->Gamepad.bRightTrigger / 255.0f;

            const float trimRate = g_liveControls.xrVehicleThrottleTrim > 0.0f
                                 ? g_liveControls.xrVehicleThrottleTrim : 0.5f;
            s_vehThrottle += ly * trimRate * dt;
            if (s_vehThrottle < 0.0f) s_vehThrottle = 0.0f;
            if (s_vehThrottle > 1.0f) s_vehThrottle = 1.0f;

            // Assignment, not max(): the trigger is the gun's now, and the value it reports must not
            // leak back into the throttle it no longer drives.
            pState->Gamepad.bRightTrigger = static_cast<BYTE>(s_vehThrottle * 255.0f + 0.5f);
            pState->Gamepad.sThumbLY = 0;                                   // consumed by the trim
            // RB = ranged attack. THE PORT'S TRIGGER OVERRIDE STILL DECIDES, because in this mode the
            // shot leaves through RB and not through the trigger byte the override was written for: a
            // revolver with its cylinder swung out would otherwise fire quite happily from the driver
            // seat, which is the one thing that channel exists to prevent. 1 = swallow, 2 = force.
            const bool fireHeld = (trgMode > 1.5f) ? true
                                : (trgMode > 0.5f) ? false
                                : (vr.rightTrigger > 0.5f);
            if (fireHeld) pState->Gamepad.wButtons |= 0x0200;
        }
        s_vehGunPrev = vehGunMode;
    }

    // Left stick pushed near FULL forward => SPRINT. A partial push is left as the
    // game's normal jog; only "to the stop" sprints. CP2077 sprint is the left-stick
    // click (L3), so we just assert L3 while the stick is forward past the threshold
    // -- no more clicking the stick. Level-triggered (held while past the threshold)
    // mirrors physically holding L3: correct for hold-to-sprint, and toggle-sprint
    // auto-cancels on slow-down so it stays in sync as well.
    // THE RAW DEFLECTION, not the quantised output -- see lyDetent above.
    //
    // NOT IN A VEHICLE, which is a bug found while reading the game's mappings: Vehicle_Autodrive is
    // IK_Pad_LeftThumb, the very L3 this gesture asserts, so pushing the left stick to the stop in a
    // car was handing it to the autopilot. The vehicle test subsumes the gun-mode one that used to be
    // here, that being a vehicle state as well.
    //
    // NOT OUT OF A CROUCH either. A sneaking player holds the stick forward like anyone else, and
    // standing them up to sprint is the opposite of what they asked for. The state comes from the
    // game's own locomotion state machine (gamePSMLocomotionStates, published by the VRIK CET mod);
    // negative means nobody told us, and then there is no gate rather than a guess.
    //
    // AND IT HAS TO BE HELD. See CyberpunkVR_SprintHoldMs.
    bool wantSprint = false;
    {
        static double s_detentMs = 0.0;
        static double s_phaseMs = 0.0;    // where we are in the press/release square wave
        static double s_askMs = 0.0;      // how long we have been asking for a state we cannot reach
        static bool   s_gaveUp = false;
        static LARGE_INTEGER s_sprintQpc = {};
        LARGE_INTEGER nowS, freqS;
        QueryPerformanceCounter(&nowS);
        QueryPerformanceFrequency(&freqS);
        double dtMs = 0.0;
        if (s_sprintQpc.QuadPart != 0 && freqS.QuadPart > 0) {
            dtMs = static_cast<double>(nowS.QuadPart - s_sprintQpc.QuadPart) * 1000.0
                 / static_cast<double>(freqS.QuadPart);
        }
        s_sprintQpc = nowS;
        if (dtMs < 0.0) dtMs = 0.0;
        if (dtMs > 100.0) dtMs = 100.0;   // a hitch must not satisfy the whole window by itself

        const int loco = g_VRLocomotionState;
        // Crouch, and the two states that ARE a crouch (the game's own sprint-from-crouch and
        // dodge-from-crouch). Slide is deliberately not here: it is entered FROM a sprint and is over
        // in a moment, so blocking there would only interrupt the player's own momentum.
        const bool crouched = (loco == 1 || loco == 12 || loco == 13);
        // CROUCH SPRINT IS A REAL STATE (PSMLocomotionStates.CrouchSprint = 12), so by default the
        // detent asks for a sprint from a crouch too and the game decides what that means. The knob
        // puts the old outright block back.
        const bool crouchBlocks = (CyberpunkVR_SprintFromCrouch == 0) && crouched;

        const bool detent = (lyDetent > 0.90f) && !g_isInVehicle && !crouchBlocks;
        if (detent) s_detentMs += dtMs; else s_detentMs = 0.0;
        const double holdMs = (CyberpunkVR_SprintHoldMs >= 0) ? CyberpunkVR_SprintHoldMs : 200;
        const bool want = detent && (s_detentMs >= holdMs);

        // ---- L3 IS A TOGGLE, SO DRIVE IT WITH EDGES -------------------------------------------
        //
        // ToggleSprint_Button (the game's own mapping) reacts to a PRESS, not to a held button. Held
        // down, it fires once -- and when the game leaves the sprint state by itself (a dash, a jump,
        // a mantle, stamina) there is no second edge, so the sprint never comes back until the player
        // releases the stick and pushes again. That was the reported dash bug.
        //
        // So: while what we want and what the game is disagree, pulse. When they agree, stop pressing.
        // THE SPRINT STATE MACHINE, not the blackboard. Measured: PlayerStateMachine.Locomotion stays
        // at Default through a player sprint, so a loop waiting for Sprint(2) never saw its target and
        // kept toggling its own sprint back off -- which is why a held detent produced a stutter
        // standing and nothing but a stand-up from a crouch. SprintEvents is the sprint.
        // HELD, NOT PULSED. Measured twice: ten press/release edges never entered the sprint state,
        // while the level-held button this port shipped with always did. The game wants the button
        // down. What it will NOT do is re-enter a sprint it has just left while the button is already
        // down -- so the only thing the feedback is used for is a short RELEASE, which makes the next
        // hold an edge. That is the dash case, and nothing else needs to change.
        const int sprintFlag = g_VRSprintActive;
        const bool haveFeedback = (sprintFlag >= 0);
        const bool sprinting = haveFeedback && (sprintFlag != 0);

        if (!want) {
            wantSprint = false;
            s_phaseMs = 0.0;
            s_askMs = 0.0;
            s_gaveUp = false;
        } else if (!haveFeedback || sprinting || s_gaveUp) {
            // Hold. This is the whole mechanism: sprinting -> keep holding; no feedback at all -> hold,
            // because that is what worked before any of this; given up -> hold, for the same reason.
            wantSprint = true;
            s_phaseMs = 0.0;
        } else {
            // Wanted, and the game is NOT in the sprint state: hold, but let go briefly so the next
            // hold is a fresh edge. Mostly-held by design -- the release is the short half.
            s_askMs += dtMs;
            const double giveUp = (CyberpunkVR_SprintGiveUpMs > 0) ? CyberpunkVR_SprintGiveUpMs : 1500;
            if (s_askMs > giveUp) {
                s_gaveUp = true;      // stop blipping; the hold above takes over
                wantSprint = true;
            } else {
                double hold = (CyberpunkVR_SprintPulseMs > 0) ? CyberpunkVR_SprintPulseMs : 240;
                double gap  = (CyberpunkVR_SprintGapMs   > 0) ? CyberpunkVR_SprintGapMs   : 60;
                s_phaseMs += dtMs;
                const double period = hold + gap;
                while (s_phaseMs >= period) s_phaseMs -= period;
                wantSprint = (s_phaseMs < hold);
            }
        }

    }
    // Published for the snap-event machinery: DURING SPRINT the game RATE-LIMITS heading
    // changes (sprint turns arc over several frames instead of jumping), so the instant
    // packet pre-rotation must be suppressed there (OnFootDeltaHeadCallback).
    // THE STATE, NOT THE BUTTON. This gates the snap-turn packet pre-rotation because the game
    // rate-limits heading changes DURING SPRINT -- so it wants to know whether the player IS sprinting,
    // and it used to be handed our input instead. Now that the loop above pulses the button, the input
    // is not even a good proxy: it is low for most of a sprint. Published from the game's own state
    // when there is one, and from the intent when there is not (which is what it was before).
    {
        // SprintEvents, not the locomotion blackboard: measured, that blackboard never reports the
        // player's sprint at all, so this suppression has been reading a constant false.
        const int sfPub = g_VRSprintActive;
        g_sprintInputActive = (sfPub >= 0) ? (sfPub != 0) : wantSprint;
    }

    // Right stick = camera turn / pitch.
    float rx = ApplyStickDeadzone(vr.rightThumbX, 0.18f);
    float ry = ApplyStickDeadzone(vr.rightThumbY, 0.18f);

    // Snap-turn diagnostics: the raw axis, the deflection the threshold is actually compared
    // against, and how far each side has ever got. Written unconditionally -- it costs four stores
    // and it is the only way to tell a stick that cannot reach the threshold from a bug in the code
    // above, which is symmetric and therefore cannot be the cause of a one-sided failure.
    if (CyberpunkVR_DebugRightStickReset != 0) {
        CyberpunkVR_DebugRightStickReset = 0;
        CyberpunkVR_DebugRightStickPeakR = 0.0f;
        CyberpunkVR_DebugRightStickPeakL = 0.0f;
    }
    CyberpunkVR_DebugRightStickRaw = vr.rightThumbX;
    CyberpunkVR_DebugRightStickX   = rx;
    if (vr.rightThumbX > CyberpunkVR_DebugRightStickPeakR) CyberpunkVR_DebugRightStickPeakR = vr.rightThumbX;
    if (-vr.rightThumbX > CyberpunkVR_DebugRightStickPeakL) CyberpunkVR_DebugRightStickPeakL = -vr.rightThumbX;

    // Right stick pushed near FULL down => CROUCH. Same bind as the right-stick click
    // (R3) used today; we assert R3 while the stick is held fully down and consume the
    // downward Y so it doesn't also drive camera pitch. Detected here, before the snap
    // turn block may zero ry, so it works regardless of the turn mode.
    // CROUCH IS THIS, AND ONLY THIS, since the click became the slide release: the stick past 0.90 down.
    // ON FOOT ONLY, for the same reason the dash below is: crouching means nothing in a car, and
    // R3 there is VehicleInverseCameraToggle_Button -- so the right stick pushed down was
    // flipping the driving camera. Found while fixing the exit button; same family of bug.
    // A DEVICE SCREEN IS UP: HAND THE RIGHT STICK'S Y BACK TO THE GAME.
    //
    // The game scrolls a device screen -- a computer's message list, a terminal -- with
    // UI_MoveY_Axis, and its own r6\config\inputUserMappings.xml binds that to IK_Pad_RightAxisY
    // and to nothing else. Three things here were eating exactly that: the crouch gesture and the
    // dash gesture each consume their half of the axis, and xr_disable_mouse_y zeroes it outright
    // for anyone who wants pitch from the headset only -- which is the shipped setting. So on a
    // computer the list could not be scrolled at all, and pushing the stick to read it dodged or
    // crouched instead.
    //
    // The flag is [164], published by the CyberpunkVRPort_DeviceCam redscript at the same two
    // points the game pushes and pops UIGameContext.DeviceZoom. It is NOT the world-map flag [81]:
    // that one also stops the HMD driving the game camera, and a device screen must keep the head
    // free to look around it.
    bool deviceScreen = false;
    if (float* shDev = GetShotShared()) {
        if (reinterpret_cast<volatile uint32_t*>(shDev)[vrshared::kDeviceScreenOpen] != 0u) {
            deviceScreen = true;
        }
    }

    // THE SCANNER'S ZOOM TAKES THE RIGHT STICK, and it takes it here -- before the crouch, the dash,
    // the pitch suppression and the snap turn, all of which read this same axis below. Consuming rx/ry
    // is what keeps the gesture from squatting the player or dodging while a zoom is being nudged.
    {
        static bool     s_ltZoomWas   = false;
        static uint64_t s_nextStepMs  = 0;
        const uint64_t now = GetTickCount64();
        // Hysteresis on the trigger for the same reason the tag has it: one squeeze is one gesture,
        // and a finger resting at the break point is not a stream of them.
        const bool ltDown = s_ltZoomWas ? (vr.leftTrigger > 0.35f) : (vr.leftTrigger > 0.60f);
        s_ltZoomWas = ltDown;
        const bool armed = scannerHold && (CyberpunkVR_ScannerZoom != 0) && ltDown
                           && !g_isInVehicle && (g_menuModeValue == 0);
        if (armed) {
            float th = CyberpunkVR_ScannerZoomStick;
            if (!(th > 0.05f) || th > 1.0f) th = 0.50f;
            const int32_t rep = (CyberpunkVR_ScannerZoomRepeatMs > 0)
                                    ? CyberpunkVR_ScannerZoomRepeatMs : 200;
            if (ry > th || ry < -th) {
                if (now >= s_nextStepMs) {
                    SendZoomKey(ry > 0.0f);
                    s_nextStepMs = now + static_cast<uint64_t>(rep);
                }
            } else {
                s_nextStepMs = 0;   // back to centre re-arms the next step immediately
            }
            rx = 0.0f;
            ry = 0.0f;
        } else {
            s_nextStepMs = 0;
        }
    }

    const bool wantCrouch = (ry < -0.90f) && !g_isInVehicle && !deviceScreen && !scannerHold
                            && !DeviceCamActive();   // in a camera the stick aims the camera
    if (wantCrouch) ry = 0.0f;

    // GOING DOWN IN THE ROOM CROUCHES THE CHARACTER, off by default (xr_physical_crouch).
    //
    // It asserts the SAME R3 the stick gesture above does, held for as long as the player stays
    // down, and that is a decision rather than an oversight: a crouch bound as a hold wants the
    // button down the whole time, one bound as a toggle takes the single press edge and ignores the
    // rest, and both give the player exactly what pushing the stick down already gives them. Any
    // cleverer scheme -- pulsing until the game's locomotion state agrees -- has to guess which
    // binding is in force, and guesses wrong once per player.
    //
    // The same gates as the stick gesture, and for the same reasons documented there: R3 in a car is
    // the camera toggle, a device screen and the scanner own the right stick, and a placed camera
    // aims with it. This gesture touches no axis, so it only needs the ones about MEANING.
    static bool s_physCrouchDown = false;
    {
        const bool switchedOn = g_liveControls.xrPhysicalCrouch != 0;
        const bool contextOk = !g_isInVehicle && !deviceScreen && !scannerHold && !DeviceCamActive();
        const bool allowed = switchedOn && contextOk;
        int gate = (switchedOn ? 1 : 0) | (contextOk ? 2 : 0);
        {
            // Reported even when the gate is shut, so the readout can say WHICH of the four
            // reasons is the one -- a zero here means the height is not arriving at all.
            float h90 = OpenXRManager::Get().GetSharedSlot(90);
            float h89 = OpenXRManager::Get().GetSharedSlot(89);
            CyberpunkVR_DebugPhysCrouchRaw = (h90 != 0.0f) ? h90 : h89;
            CyberpunkVR_DebugPhysCrouchDrop = -CyberpunkVR_DebugPhysCrouchRaw;
            if (h90 != 0.0f || h89 != 0.0f) gate |= 4;
        }
        if (!allowed) {
            // Dropped rather than remembered. Coming back from a car or a terminal with the latch
            // still set would crouch the player for reasons that happened in another context.
            s_physCrouchDown = false;
        } else {
            // Neck pivot first, raw HMD height as the fallback for the same reason CharacterRig
            // does it: [90] is written by this DLL every frame, but a build where it never arrives
            // should degrade to a working-if-cruder signal instead of to nothing.
            float head = OpenXRManager::Get().GetSharedSlot(90);
            if (head == 0.0f) head = OpenXRManager::Get().GetSharedSlot(89);
            const float drop = -head;   // shared height is ~0 standing and negative going down
            float enter = g_liveControls.xrPhysicalCrouchDepth;
            if (!(enter > 0.05f)) enter = 0.30f;   // a settings file that never had the key
            const float exitAt = enter * kPhysCrouchReleaseFrac;
            if (!s_physCrouchDown) {
                if (drop > enter) s_physCrouchDown = true;
            } else if (drop < exitAt) {
                s_physCrouchDown = false;
            }
        }
        if (s_physCrouchDown) gate |= 8;
        CyberpunkVR_DebugPhysCrouchGate = gate;
    }

    // ONE R3 PULSE PER EDGE. Every word of that was paid for by a measurement:
    //
    //   * A LEVEL DOES NOT WORK. Holding R3 down for the whole squat left the game's locomotion
    //     state at 0 -- Stand -- with the bit set on every poll. ToggleCrouch takes BUTTON_PRESSED,
    //     so what it wants is a transition, and a bit that is simply always on after a point never
    //     gives it a second one.
    //
    //   * BOTH EDGES, because it is a toggle: going down asks for the crouch, coming up has to ask
    //     again to undo it. The first version pulsed only while the body was DOWN, which read as
    //     working -- the player crouched -- and then stranded them crouched when they stood up.
    //
    //   * THE PAD, NOT THE KEY. ToggleCrouch_Button also carries IK_C, and a synthetic tap of it
    //     through SendInput does nothing here: with the pulse removed and the key alone, the crouch
    //     stopped responding entirely. The key stays unused rather than kept "just in case" -- a
    //     second path that fires on the same edge as a working one would toggle twice and cancel it.
    //
    // The locomotion state reads 0 even while the player is visibly crouched, so nothing here is
    // allowed to depend on it. It is shown on the readout as information and trusted for nothing.
    static bool     s_physCrouchLast = false;
    static uint64_t s_physPulseUntil = 0;
    static uint64_t s_physLastEdgeMs = 0;
    const uint64_t nowMs = GetTickCount64();
    if (s_physCrouchDown != s_physCrouchLast && nowMs - s_physLastEdgeMs > 250) {
        s_physCrouchLast = s_physCrouchDown;
        s_physLastEdgeMs = nowMs;
        // Long enough that a slow poll cannot miss it, short enough to read as a tap rather than a
        // hold -- R3 held is a different action to the game (Tag, and the vehicle camera).
        s_physPulseUntil = nowMs + 150;
        ++CyberpunkVR_DebugPhysCrouchToggles;
    }
    const bool physCrouch = nowMs < s_physPulseUntil;

    // Right stick pushed near FULL UP => DASH (the game's Dodge_Button, pad B). The mirror image of
    // the crouch gesture above, in every respect: same 0.90 threshold, detected here BEFORE the pitch
    // suppression so it works whichever way "Disable Mouse Y" is set, and its half of the axis is
    // consumed so a dash never also pitches the camera.
    //
    // DASH LIVES HERE AND NOT ON A. A is Jump_Button and carries three things already -- jump, the
    // double jump, and Charge Jump on the hold -- so a tap/hold split there has to spend one of them:
    // either the jump moves to the release (late, and the charge can never charge) or the jump always
    // fires first and the dash is mid-air only. This half of this axis was doing nothing.
    //
    // EDGE-TRIGGERED, with the same re-arm rule the snap turn uses: the stick must come back before
    // another dash can fire, so holding it up dodges exactly once. The direction is the LEFT stick's,
    // as it is on a pad -- B plus a held direction, nothing to invent.
    bool dashPulse = false;
    {
        static int      s_dashArmedDir = 0;   // 1 = fired on this push, waiting for the stick to return
        static uint64_t s_dashUntilMs  = 0;
        const bool allowDash = (CyberpunkVR_DashStickUp != 0)
                               && !scannerHold          // the scanner owns this stick
                               && !deviceScreen         // the stick is scrolling a screen
                               && !DeviceCamActive()    // in a camera it is aiming the camera
                               && !g_isInVehicle            // in a car the right stick is free look
                               && (g_menuModeValue == 0);    // in menus it navigates
        if (allowDash) {
            const uint64_t now = GetTickCount64();
            if (ry > 0.90f) {
                if (s_dashArmedDir == 0) {
                    s_dashArmedDir = 1;
                    const int ms = (CyberpunkVR_DashPulseMs > 0) ? CyberpunkVR_DashPulseMs : 100;
                    s_dashUntilMs = now + static_cast<uint64_t>(ms);
                }
            } else if (ry < 0.50f) {
                s_dashArmedDir = 0;
            }
            dashPulse = (now < s_dashUntilMs);
        } else {
            s_dashArmedDir = 0;
            s_dashUntilMs = 0;
        }
    }
    if (ry > 0.90f && !deviceScreen) ry = 0.0f;   // consumed, exactly as the crouch half is

    // Suppress pitch from the stick if the user wants HMD-only pitch.
    // ...but never on a device screen: there this axis is not camera pitch at all, it is the
    // game's own list navigation, and zeroing it there is what made a computer unreadable.
    if (g_liveControls.xrDisableMouseY != 0 && !deviceScreen) ry = 0.0f;

    // NOT INSIDE A SURVEILLANCE CAMERA. The block below consumes stick X and turns the PLAYER's
    // on-foot heading instead, so in a camera it turned a body nobody can see while the view -- composed
    // from the lens -- did not move, and the stick could not pan the camera because its X never reached
    // the game. Off, the game's own continuous camera aim works exactly as it does on a flat screen.
    if (g_liveControls.xrSnapTurn != 0 && !DeviceCamActive()) {
        // True instant snap turn: route the right-stick flick directly into a
        // yaw delta the game applies in ONE frame via the OnFootDeltaHook.
        // Stick X is consumed (zeroed) so the game never sees stick-driven
        // smooth rotation. The stick must come back below the re-arm threshold
        // before another snap can fire -- a held stick produces exactly one snap.
        //
        // TO THE STOP, like every other gesture here. This used to fire at HALF deflection and re-arm
        // at 0.15, so a resting thumb or a wrist drifting while walking turned the player -- the
        // accidental snaps. See CyberpunkVR_SnapTurnStickFire for the numbers and why the re-arm sits
        // where it does.
        float fire = CyberpunkVR_SnapTurnStickFire;
        if (!(fire > 0.05f) || fire > 1.0f) fire = 0.90f;
        float rearm = CyberpunkVR_SnapTurnStickRearm;
        if (!(rearm >= 0.0f) || rearm >= fire) rearm = fire * 0.55f;
        int wantDir = 0;
        if (rx > fire) wantDir = +1;
        else if (rx < -fire) wantDir = -1;

        if (fabsf(rx) < rearm) g_xinputSnapArmedDir = 0;

        if (wantDir != 0 && wantDir != g_xinputSnapArmedDir) {
            g_xinputSnapArmedDir = wantDir;
            const float angleDeg = g_liveControls.xrSnapTurnAngleDeg > 0.0f
                ? g_liveControls.xrSnapTurnAngleDeg : 30.0f;
            // In CP2077 the on-foot yaw delta is signed such that positive =
            // turn LEFT, so we negate wantDir to make stick-right -> turn right.
            const float deltaDeg = -(float)wantDir * angleDeg;
            LONG bits;
            memcpy(&bits, &deltaDeg, sizeof(bits));
            InterlockedExchange(&g_pendingSnapYawDeltaBits, bits);
        }
        // Stick X is consumed by the snap turn, so do not pass it to the game. Y is NOT: snap
        // turning is about yaw, and zeroing ry here took the stick's pitch away even from users who
        // asked to keep it -- the check above already zeroes it for those who did not (dabinn,
        // TofuExpress 11974ee5).
        rx = 0.0f;
    }

    if (fabsf(rx) > fabsf(pState->Gamepad.sThumbRX / 32767.0f)) pState->Gamepad.sThumbRX = FloatToSHORT(rx);
    if (fabsf(ry) > fabsf(pState->Gamepad.sThumbRY / 32767.0f)) pState->Gamepad.sThumbRY = FloatToSHORT(ry);

    // Stick-gesture buttons: full-forward left stick => sprint (L3), full-down right
    // stick => crouch (R3). OR'd in on top of any physical / VR button press.
    uint16_t synthButtons = 0;
    if (wantSprint) synthButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (wantCrouch || physCrouch) synthButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    CyberpunkVR_DebugPhysCrouchSent = s_physCrouchLast ? 1 : 0;
    CyberpunkVR_DebugPhysCrouchLoco = g_VRLocomotionState;
    // The dash, as the B the game's Dodge_Button listens for. Emitted HERE rather than merged with the
    // controller's own buttons, because the physical B is the port's now (the magazine drop) and is
    // masked out of that merge -- so this pulse is the only thing in the process that can dodge.
    if (dashPulse) synthButtons |= 0x2000;   // XINPUT_GAMEPAD_B
    // The scanner, as the LB the game's Vision_Hold_Button listens for. Level-triggered by
    // construction: it is a HOLD binding, so it stays down exactly as long as the gesture does.
    if (scannerHold) synthButtons |= 0x0100;   // XINPUT_GAMEPAD_LEFT_SHOULDER
    // ...and the scanner's own five, as the buttons the game's listeners are actually bound to. The
    // block that computes these names the file each binding was read out of.
    if (scanDpadUp)   synthButtons |= 0x0001;   // DPAD_UP       -> UI_MoveUp
    if (scanDpadDown) synthButtons |= 0x0002;   // DPAD_DOWN     -> UI_MoveDown
    if (scanApply)    synthButtons |= 0x4000;   // X             -> UI_ApplyAndClose
    if (scanTag)      synthButtons |= 0x0080;   // RIGHT_THUMB   -> Tag_Button
    if (scanTab)      synthButtons |= 0x0200;   // RIGHT_SHOULDER-> DescriptionChange
    // THE HORN, as the X the game's Vehicle_Horn listens for (IK_Pad_X_SQUARE / IK_Z in
    // r6\config\inputUserMappings.xml). Emitted here for the same reason the dash is: the physical X
    // is the port's own while mounted and is masked out of the merge, so this is the only X that can
    // reach the game in a car. Held level -- the horn lasts as long as the hand is on the hub. See
    // the horn block above for why this cannot trip the exit.
    if (hornPress) synthButtons |= 0x4000;   // XINPUT_GAMEPAD_X = Vehicle_Horn
    // HAND INTERACT: the hand is on the thing the game is already offering an X interaction for, and
    // the gesture for it has completed. X is the game's Choice1 -- pick up, open, press, loot.
    //
    // LEVEL, NOT A PULSE. Half the game's interactions are hold bindings (open a container, enter a
    // vehicle, most terminals), and a pulse would fire only the tap ones. The producer holds the
    // deadline up for as long as the gesture lasts, so the button follows the hand.
    //
    // ON FOOT ONLY, and not for the reason this comment used to give. It said a synthetic X in a car
    // would eject the player; it never could -- the exit is B, both in the game's mappings and in
    // this port. The real reason is simpler: in a vehicle X is Vehicle_Horn, not Choice1, so an
    // interact press there would honk instead of interacting.
    //
    // The DEADLINE is the guard, not a flag: see g_vrInteractPressUntilMs. A CET mod that dies mid
    // gesture releases the button by itself within 250 ms.
    //
    // TWO ONE-SHOT LINES, because a field report said the first object of a session answers neither
    // the grip nor the touch until one PHYSICAL X has been pressed, after which both work for the
    // rest of the session. The CET side is not the suspect -- its log already shows `press=true` on
    // that first object -- so the question is whether this merge emitted the X at all, and the two
    // answers call for opposite fixes: nothing emitted means a gate here, emitted-and-ignored means
    // the engine did not accept the press and the fix is about edges, not gates. Logged once each so
    // a 60 Hz poll cannot flood the file.
    {
        const bool pressWanted = GetTickCount64() < g_vrInteractPressUntilMs.load(std::memory_order_relaxed);
        static bool s_loggedWanted = false, s_loggedEmitted = false;
        if (pressWanted && !s_loggedWanted) {
            s_loggedWanted = true;
            Log("VRInteract merge: first press wanted. mounted=%d gameplayScreen=%d menuMode=%d\n",
                mounted ? 1 : 0, gameplayScreen ? 1 : 0, static_cast<int>(g_menuModeValue));
        }
        if (pressWanted && !mounted && gameplayScreen && !s_loggedEmitted) {
            s_loggedEmitted = true;
            Log("VRInteract merge: first X emitted into the pad\n");
        }
        // AND HOW LONG EACH ONE LASTED. Most of this game's interactions are HOLD bindings, so a
        // press that goes out for 120 ms is indistinguishable, from the player's side, from one that
        // never went out at all -- both leave the shard unopened. The first twenty windows tell a
        // squeeze that was too short from a press the engine ignored, and twenty is few enough that
        // a session cannot fill the log with them.
        static bool s_emitting = false;
        static uint64_t s_emitStartMs = 0;
        static int s_windows = 0;
        const bool emittingNow = pressWanted && !mounted && gameplayScreen;
        if (emittingNow && !s_emitting) {
            s_emitStartMs = GetTickCount64();
        } else if (!emittingNow && s_emitting && s_windows < 20) {
            ++s_windows;
            Log("VRInteract merge: X window #%d lasted %llu ms\n",
                s_windows, static_cast<unsigned long long>(GetTickCount64() - s_emitStartMs));
        }
        s_emitting = emittingNow;
    }
    if (!mounted && gameplayScreen
        && GetTickCount64() < g_vrInteractPressUntilMs.load(std::memory_order_relaxed)) {
        synthButtons |= 0x4000;   // XINPUT_GAMEPAD_X = Choice1 / interact
    }
    // THE JUMP GESTURE'S A. Same shape, same deadline discipline, and on foot for the same reason:
    // in a car A is not Jump. Note this is NOT affected by the X/A swap above -- that swaps the
    // PHYSICAL buttons only, so a gesture that means jump still emits the jump the game listens for.
    if (!mounted && gameplayScreen
        && GetTickCount64() < g_vrJumpPressUntilMs.load(std::memory_order_relaxed)) {
        synthButtons |= 0x1000;   // XINPUT_GAMEPAD_A = Jump
    }
    // B GETS YOU OUT OF THE CAR, HELD -- the game's own exit button, on a delay.
    //
    // IT USED TO BE X, and that was a detour built on a misread of the mappings. The reasoning went:
    // the exit is B, a stray B at speed ejects the player, so mask B and translate a held X into it.
    // But X in a vehicle is not free -- r6\config\inputUserMappings.xml has Vehicle_Horn on
    // IK_Pad_X_SQUARE / IK_Z -- so the translation spent the horn to protect a button that only ever
    // needed a delay. The horn went silent for three releases because of it.
    //
    // Now the two buttons keep their own jobs and only the timing is ours: B stays masked out of the
    // merge above (kVehicleOwnedButtons), so a brush of it does nothing, and a hold past
    // CyberpunkVR_VehicleExitHoldMs re-emits it as the ExitVehicle_Button the game is listening for
    // (IK_F + IK_Pad_B_CIRCLE). X is no longer touched at all and honks, as it does in flat.
    //
    // A HOLD, not a level pass-through, and that part was right the first time. ExitVehicle_Button
    // carries no <hold> and no acceptedEvents in the game's mappings, so the vehicle state machine acts
    // on the first frame it sees the action -- and at speed acting on it means throwing the player out
    // of the car. Passed straight through, brushing B while driving ejected them into a ragdoll, which
    // reads as a collision or physics bug and not as a button at all. Held, a stray tap costs nothing.
    {
        static uint64_t s_exitDownSinceMs = 0;
        const bool bHeld = mounted && gameplayScreen && (vr.buttons & 0x2000) != 0;
        if (!bHeld) {
            s_exitDownSinceMs = 0;
        } else {
            const uint64_t now = GetTickCount64();
            if (s_exitDownSinceMs == 0) s_exitDownSinceMs = now;
            const uint64_t need = (CyberpunkVR_VehicleExitHoldMs > 0)
                                      ? static_cast<uint64_t>(CyberpunkVR_VehicleExitHoldMs) : 400;
            if (now - s_exitDownSinceMs >= need) {
                synthButtons |= 0x2000;   // XINPUT_GAMEPAD_B = ExitVehicle_Button
            }
        }
    }

    // AUTODRIVE, MOVED OFF THE LEFT STICK CLICK AND ONTO THE RIGHT ONE.
    //
    // The game puts Vehicle_Autodrive on IK_Pad_LeftThumb. In this port that button is the busiest
    // one there is: it is the D-pad modifier, it is the radio chord's other half, and released bare
    // it is the vanilla click -- so every one of those gestures ended a hair away from handing the
    // car to the autopilot, and a slip while driving does not read as a misclick, it reads as the car
    // taking itself over. The frame loop no longer emits L3 at all while mounted; the button is
    // synthesised here instead, from the RIGHT stick click.
    //
    // THE RIGHT CLICK IS FREE IN VR, whatever the mappings say. In flat it is the look-back camera,
    // and in a headset that camera is a head turn -- so there is nothing to share the button with and
    // no tap/hold split to build. The click is the autopilot, whole.
    //
    // EDGE, NOT LEVEL: one toggle per press, so holding the stick down cannot flip the autopilot on
    // and off. The physical R3 is masked out of the merge above, so this pulse is the only R3-family
    // press the game can see in a car.
    {
        static bool     s_r3WasDown        = false;
        static uint64_t s_autodriveUntilMs = 0;

        const uint64_t now    = GetTickCount64();
        const bool     r3Down = mounted && gameplayScreen && (vr.buttons & 0x0080) != 0;

        if (r3Down && !s_r3WasDown) {
            const uint64_t pulse = (CyberpunkVR_VehicleClickPulseMs > 0)
                                       ? static_cast<uint64_t>(CyberpunkVR_VehicleClickPulseMs) : 100;
            s_autodriveUntilMs = now + pulse;
        }
        s_r3WasDown = r3Down;

        if (now < s_autodriveUntilMs) synthButtons |= 0x0040;   // LEFT_THUMB = Vehicle_Autodrive
    }
    // WHO IS PRESSING X, AND IS IT EVER RELEASED.
    //
    // A braindance restarted itself in a loop with both controllers lying on the desk, and the
    // window log above could not see it: a window is only written when it ENDS, so an X that is
    // never released writes nothing at all. This watches the BIT THE GAME ACTUALLY RECEIVES, on
    // every change, and says which half put it there -- the physical merge or one of our
    // syntheses. Three outcomes, three different problems: physical means a stuck button or the
    // X/A swap, synth means a producer that never lowers its deadline, and NEITHER means the game
    // is restarting on its own and the port is not in it.
    {
        static int s_lastX = -1, s_logged = 0;
        const bool xPhysical = (physicalButtons & static_cast<uint16_t>(~ownedNow) & 0x4000) != 0;
        const bool xSynth    = (synthButtons & 0x4000) != 0;
        const int  state     = (xPhysical ? 2 : 0) | (xSynth ? 1 : 0);
        if (state != s_lastX && s_logged < 40) {
            s_lastX = state;
            ++s_logged;
            Log("X bit: physical=%d synth=%d  (interact deadline %s)\n",
                xPhysical ? 1 : 0, xSynth ? 1 : 0,
                GetTickCount64() < g_vrInteractPressUntilMs.load(std::memory_order_relaxed)
                    ? "live" : "expired");
        }
    }
    pState->Gamepad.wButtons |= synthButtons;

    // Bump packet number on any change so XInput consumers latch it.
    static uint16_t s_lastButtons = 0;
    static uint16_t s_lastSynth = 0;
    static BYTE s_lastLT = 0, s_lastRT = 0;
    // The trigger bytes compared here are the MERGED ones, not the raw VR values: the latched vehicle
    // throttle walks bRightTrigger up and down while the VR trigger sits still, and a consumer that
    // only re-reads on a new packet number would never see the trim move.
    const BYTE outLT = pState->Gamepad.bLeftTrigger;
    const BYTE outRT = pState->Gamepad.bRightTrigger;
    if (vr.buttons != s_lastButtons || synthButtons != s_lastSynth || outLT != s_lastLT || outRT != s_lastRT) {
        pState->dwPacketNumber++;
        s_lastButtons = vr.buttons;
        s_lastSynth = synthButtons;
        s_lastLT = outLT;
        s_lastRT = outRT;
    }
    return r;
}

static int PatchXInputIat(HMODULE mod, void* newFunc, void** outOrig) {
    auto base = reinterpret_cast<uint8_t*>(mod);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    const IMAGE_DATA_DIRECTORY& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0) return 0;

    int patched = 0;
    for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         imp->Name; ++imp) {
        const char* dll = reinterpret_cast<const char*>(base + imp->Name);
        if (_strnicmp(dll, "xinput", 6) != 0) continue;            // xinput1_4 / 1_3 / 9_1_0
        if (imp->OriginalFirstThunk == 0 || imp->FirstThunk == 0) continue;
        auto nameThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto iatThunk  = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; nameThunk->u1.AddressOfData; ++nameThunk, ++iatThunk) {
            if (nameThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;   // imported by ordinal: no name
            auto ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + nameThunk->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(ibn->Name), "XInputGetState") != 0) continue;
            void** slot = reinterpret_cast<void**>(&iatThunk->u1.Function);
            if (*slot == newFunc) continue;                            // already ours (re-scan)
            DWORD oldP = 0;
            if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldP)) {
                if (outOrig && !*outOrig) *outOrig = *slot;            // chain whatever was there
                *slot = newFunc;
                VirtualProtect(slot, sizeof(void*), oldP, &oldP);
                ++patched;
            }
        }
    }
    return patched;
}

bool InstallXInputHook() {
    // Make sure an XInput DLL is resolvable so a not-yet-bound import is live and
    // the GetProcAddress fallback below works. Not fatal if absent -- the IAT
    // match is by name, independent of which XInput variant the game imports.
    HMODULE xi = GetModuleHandleA("XInput1_4.dll");
    if (!xi) xi = LoadLibraryA("XInput1_4.dll");
    if (!xi) xi = LoadLibraryA("XInput1_3.dll");
    if (!xi) xi = LoadLibraryA("xinput9_1_0.dll");

    void* orig = nullptr;
    int patched = 0;
    void* hook = reinterpret_cast<void*>(&HookedXInputGetState);

    // Main executable first (CP2077 imports XInputGetState here), then every other
    // loaded module that imports it, so no caller is missed. The exe is also in the
    // EnumProcessModules list; the "already ours" guard makes the re-scan a no-op.
    if (HMODULE exe = GetModuleHandleW(nullptr))
        patched += PatchXInputIat(exe, hook, &orig);

    HMODULE mods[512];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const DWORD count = needed / sizeof(HMODULE);
        const DWORD n = count < 512 ? count : 512;
        for (DWORD i = 0; i < n; ++i)
            patched += PatchXInputIat(mods[i], hook, &orig);
    }

    if (patched > 0 && orig) {
        g_realXInputGetState = reinterpret_cast<XInputGetState_t>(orig);
        g_xinputHooked = true;
        Log("XInput: IAT hook installed (%d slot(s) patched, real=%p)\n", patched, orig);
        return true;
    }

    // No import slot found (game resolves XInput dynamically or by ordinal). Keep a
    // real pointer so the shim could still chain if ever invoked, and fail soft --
    // controller input is simply unavailable, the game is NOT patched, no crash.
    if (xi && !g_realXInputGetState)
        g_realXInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(xi, "XInputGetState"));
    Log("XInput: no XInputGetState import slot found (patched=%d) -- controller input unavailable\n", patched);
    return false;
}
// THE GUARD THIS HOOK ALWAYS HAD: the boot wrapped the call in
// `if (g_liveControls.xrXInputInstall != 0)`. It travels with the hook, so the setting cannot be
// lost by a boot function forgetting to ask.
namespace { bool XInputWanted() { return g_liveControls.xrXInputInstall != 0; } }

CVR_HOOK_IF("XInput", ::cvr::hooks::Stage::Boot, 100, InstallXInputHook, XInputWanted);
