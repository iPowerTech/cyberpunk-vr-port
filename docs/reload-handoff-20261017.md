# Native reload: both arms to the animation (2026-10-17)

## Camera kick

`CAMERA_KICK_SCALE` in `mods/cet/CyberpunkVRPort_Weapon/init.lua` is 0 again (it was 0.2 since
2026-10-16): the game's RecoilKick*/RecoilAngle* multiplier removes the camera kick completely. The hand
spring keeps its own scale (`CyberpunkVR_RecoilScale`, ini `xr_recoil_scale`).

## Reload hand-off

Pressing Reload (left X) runs the game's own reload (`ReloadEvents`); its animation moves both arms, but
VRIK solved them to the controllers over it, so the magazine moved through the gun on its own.

- `mods/redscript/CyberpunkVRPort_NoAnims/vrport_reload_anim.reds` wraps `ReloadEvents.OnEnter / OnExit /
  OnForcedExit` and calls `SetVRReloadAnimation(active, finishIn)`. `finishIn` = `ReloadLogicalDuration`
  plus `ReloadEndTime` / `EmptyReloadEndTime` (what `IsReloadDurationComplete` waits for). A shell of a
  looped reload (ReloadAmount > 0) that does not fill the magazine passes -1: the next shell follows.
- `src/Anim/ReloadHandOff.cpp`: the arms keep being SOLVED; only the IK target moves, from the controller
  to the animated hand and back (smoothstep). No hand-over in the wheel's sense (`armToAnim`): switching to
  the rig's own segment lengths and clavicle at the end of a blend steps the hand by the player-vs-rig arm
  difference. The animated hands are captured beside `WheelCaptureAnim` (pure animated pose) relative to
  the bone both clavicles hang from and re-placed on that bone as solved, so the reload happens in front of
  the player's chest and the two hands keep their relation (the left hand meets the magazine well).
- Fingers of both hands go to the animation with the same weight (the carried-body finger snapshot in
  `AnimPose.cpp`, now weighted). The finger weight is extrapolated to the call time, because the fingers are
  written on every pass and the ramp only advances on the fresh one.
- Timing: in 0.20 s, back 0.30 s, starting 0.15 s before `finishIn`; interrupted reloads (fire, sprint,
  quick melee) go back from wherever the blend is. Safety: the hold ends a second after its expected end,
  or after 10 s of a shell with an unknown end, or when no weapon is equipped.
- Knobs: `CyberpunkVR_VrikReloadHandOff` (1), `CyberpunkVR_ReloadHandOffInSec`, `...OutSec`, `...LeadSec`.
  Stats: `CyberpunkVR_ReloadHandOffStats[8]` = weight, wanted, seconds in, expected seconds, animated right
  hand relative to the chest (3), reloads entered.

Probe (`build/monowire-housing-20261016/probe_reload.py`, Lexington, 3 shots, X pressed, VRIK bind 4):
magazine 17 -> 20; weight 0.19 / 0.69 / 1.0 at 0.04 / 0.11 / 0.18 s; `finishIn` 1.40 s; back to 0 at
1.55 s. The animated right hand moves from (+0.114, -0.301, +0.055) m off the chest to (+0.20, -0.31,
+0.16) and returns to rest about 1 s after the state ends (the animation's return to the ready pose, which
the hands no longer follow). Captures: left hand on the grip pulling the magazine at 0.45 s, a new magazine
in the left hand at 1.2 s, both hands back on the controllers after.

Not covered: looped (shell-by-shell) reloads; head-aim mode, where VRIK is suspended anyway and head aim
keeps the weapon's rotation.

## Finishers suspend VRIK

A finisher with its paired animation (low-health enemy, melee) runs the player in a workspot tagged
`FinisherWorkspot`; `PlayerPuppet.OnWorkspotStartedEvent / OnWorkspotFinishedEvent` raise and drop
`m_isInFinisher`. `mods/redscript/CyberpunkVRPort_Finisher/vrport_finisher.reds` wraps both and calls
`SetVRFinisherActive(GetIsInWorkspotFinisher())`; `AnimPose.cpp` suspends the whole solve while it is set,
exactly like a grapple (roomscale is not part of the solve). Fast finishers are an ordinary melee hit and
start no workspot. A flag older than 20 s is ignored (an end that never arrived). Knob
`CyberpunkVR_VrikFinisherSuspend` (1).

Checked: the redscript compiles; `SetVRFinisherActive(true)` from CET takes the arms off the controllers to
the animation and `false` brings them back. A real finisher was not reproduced in the simulator.
