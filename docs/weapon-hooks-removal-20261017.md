# Legacy shot-finding weapon hooks removed (2026-10-17)

## Why

A tester hit a reproducible interaction blocker in q303 ("You know my name"): the suit duffle had no
Take/Equip prompt in VR, and Jackie's ofrenda items had the same problem. They bisected it to
`Hooked_WaXhUpd`. That detour on the targeting update at exe+0x4B9D84 wrote the VR muzzle position
(`g_provMuzzlePos`) into the targeting cache's origin (`[this+0x30CC8]+0x350`). That cache is read by
every look-at query, not only by the shot. With the origin at the muzzle, the interaction ray started
at the hand, so prompts for some objects never appeared. The switch `g_waXhPosFromMuzzle` was 1.
Setting it to 0 fixed both cases and left hip fire and ADS unchanged.

The rest of the WeaponAim family dates from the search for the shot. It was still installed at boot,
as counters or as levers behind switches that were all 0. The user decided to remove all of it.

## What is left (`src/Hooks/WeaponAim.cpp`, 1783 -> ~715 lines)

- **PhysicalRay**: callsite patches at 0x84E31F / 0x84E354 (evaluator 0x1203B0). The hitscan comes
  from the muzzle and along the barrel, with the cone and `RecoilOnShot`. Unchanged, except for one
  thing in `WaMuzzle`: the `g_waFwd` fallback is gone, because nothing has published `g_waFwd` since
  `SetVRWeaponAim` was removed.
- **Pose copy @0x28D4B8**: only `CapturePoseAddress` / `TransferPoseAddress`, for Camera/PoseIdentity.
  `EnsureCameraPoseCopyHook` is unchanged.
- **Targeting update @0x4B9D84**: only the device-camera takeover refresh after the original
  (`RefreshTakeoverTargeting`). It no longer writes anything into the cache.
- The kill switch `kEnableWeaponAimFamily` and the boot install. The native `InstallWeaponAimHook` is
  kept. `GetVRWeaponAim` is kept and always returns 0, which is what it already returned; the
  crosshair module only checks that the native exists.

## What went

Hooks:

- the HW-breakpoint camera tracer;
- the heading offset (0x4E8A1C);
- TargetHelper (0x46F774) and the shot classifier;
- the CandA/CandB/SVP/SFVW counters;
- the projectile diagnostics/redirect in the copy hook;
- the PhysTrace callsite (0x46F1EA) and the two Normalize callsites;
- ShotSnap (0x79ACA0), GetWorldOrientation (0x802390), the camera transform (0x1D92A0) and the trace
  funnel (0x1303EC).

Natives, about 40 (none called by any CET or redscript, in the repo or in the game folder):

- dumps and getters: `DumpWeaponAimHookStats`, `GetWeaponAimStat`, `GetVRFire*`, `GetVRProj*`,
  `GetVRTrace*`, `GetVRPADump`;
- the camera tracer: `DumpVRCamAddr`, `Start/Stop/DumpVRCamTrace`;
- the levers: `SetVRShotCamera`, `SetVRHeadLocal`, `SetVRSkipHmdTest`, `SetVRGetOrient`,
  `SetVRXformOverride`, `SetVRFire*`, `SetVRTargetCtrl`, `SetVRProj*`, `SetVRShotOrigin`,
  `SetVRTraceOverride`, `ResetVRTrace`, `SetVRShotSnap`, `SetVRHeadingTest`, `SetVRWeaponAim`;
- the dormant files' natives: `InstallVRPrepareAttack`, `SetVRPrepareAttackSwap`,
  `DumpVRLiveProjectile`, `VRProjSteerTick`, `DumpVRProjectileRtti`.

Files and globals:

- Deleted `src/Natives/PrepareAttackHook.cpp`, `LiveProjectile.cpp` and `ProjectileRttiDump.cpp`.
  `IsReadable` moved from `LiveProjectile.cpp` to `Natives.cpp`, because the orientation provider
  uses it.
- Removed about 150 `g_wa*` / `g_xf*` / `g_go*` / `g_ss*` / `g_tr*` / `g_fire*` / `g_shot*` /
  `g_proj*` / `g_pa*` globals.
- Also removed `g_waXhPosFromMuzzle`, `DumpProviderSlots` and its write-only census arrays.

Shared slots and the camera:

- In `LocateCamera`, the skip-HMD test that read shared [57] (shot frame) together with [58] as a
  uint32 mode is gone. As a float, [58] stays the weapon-aim toggle for HeadAimWeapon and the
  provider, and its float value never matched mode 1/2. Slot [57] is now free.

Unchanged: the orientation-provider instrument (slot 33 projectile origin and orientation, the recoil
signal), `SetVRMuzzlePos` / `SetVRMuzzleQuat`, the barrel dot (`SetVRAimHit`) and SightLine.

## Checked

Probe: `build/monowire-housing-20261016/probe_shot.py` (garage save, bind 4, Lexington, three
hip-fire rounds, no teleport).

- The log has `[hooks] boot WeaponAim ok`; `CyberpunkVR_DebugPhysicalRayPatched` = 1;
  `CyberpunkVR_DebugWaInstalled` = 1.
- The magazine went 20 -> 17. With `CyberpunkVR_RuntimeDiagnostics` = 1, three rounds gave three of
  each: PhysicalRay Calls, Mine, OriginWrites, ForwardWrites and Recoil. StaleMuzzle was 0.
- These counters only count while RuntimeDiagnostics is on. With it at 0 they stay at zero, and that
  is expected.
- The first launch of this build crashed at startup (exe+0x14BB4A, non-canonical read, Codeware in the
  stack). The same crash happened at 15:22 with the previous DLL (exe+0x14BA9E). The next two launches
  were clean.

Not checked in the simulator: the q303 duffle and the ofrenda, because there is no save at that point
and teleporting is not allowed. The tester's fix was exactly the removed cache write.
