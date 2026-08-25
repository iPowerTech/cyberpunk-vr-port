---
name: cp2077-hitscan-physicalray-muzzle
description: Verified CP2077 2.31 native hitscan path from BulletImpact back to PhysicalRay, the rejected camera/VFX candidates, the manual direction proof, and the final VR muzzle-origin/forward callsite patch
metadata:
  type: project
  status: verified-live
  game_version: "2.31"
  date: "2026-08-16"
---

# Native hitscan from the VR muzzle: PhysicalRay proof and implementation

## Result

The native, non-projectile firearm hit in Cyberpunk 2077 2.31 is controlled by
`gameEffectObjectProvider_PhysicalRay::Execute`, not by the targeting-system async ray, the
look-at/aim-point setters, the `TriggerWeaponEffects(Shoot)` VFX graph, or the legacy function at
RVA `0x1303EC` that had been labeled as a trace.

The verified combat path builds the ray here:

```text
PhysicalRay::Execute                         RVA 0x84E2D0
  evaluate inputPosition                    callsite RVA 0x84E31F
  evaluate inputForward                     callsite RVA 0x84E354
  evaluate inputRange                       callsite RVA 0x84E369
  normalize forward                         callsite RVA 0x84E376
  end = origin + normalize(forward) * range RVA 0x84E384..0x84E38B
  submit the physics query                   RVA 0x84E47A -> 0x84ED50
```

The production fix patches the two evaluator callsites, after the game has evaluated its camera
values but before it normalizes the direction or constructs the query:

```text
origin  = VR muzzle world position          shared[200..202], valid shared[203]
forward = rotate(rawForward, R), where
          R = rotationBetween(cameraForward, muzzleForward)
          cameraForward = rotate((0,1,0), g_lastLocateQuat)
          muzzleForward = muzzle transform world +Y axis, shared[24..26], valid shared[27]
```

The forward is a ROTATION of the game's own direction, not a replacement of it. Writing the barrel
axis in flat put every shotgun pellet on one point; rotating the game's cone from the camera axis
onto the barrel axis keeps the pattern and moves only its center. A single-ray weapon is the same
arithmetic with a zero offset.

The final implementation is in:

- `include/Anim/WeaponAimState.hpp`: verified RVAs.
- `src/Hooks/WeaponAim.cpp`: callsite relays, player-ray filter, origin/forward writes, counters.
- `src/Natives/OrientationProvider.cpp`: muzzle quaternion publication and the established `+Y`
  barrel-axis convention.
- `src/Overlay/OverlayDebugDraw.cpp`: barrel-dot world-point mode, zeroed at 20 metres.

This was proven live by replacing only the `PhysicalRay` stack-local forward with world-up
`(0, 0, 1, 0)` before normalization. The same stopped shot ceased hitting the camera crosshair and
went elsewhere/up. The test was repeated because the first result was not watched. The second test
produced the same result. That single-variable intervention is the decisive proof that this is the
combat ray, not a correlated VFX or targeting probe.

## Scope and address convention

All engine addresses in this document are RVAs relative to `Cyberpunk2077.exe` unless explicitly
shown as live virtual addresses. The live module base during the investigation was commonly:

```text
Cyberpunk2077.exe base = 0x00007FF643060000
```

Heap addresses such as `0x20D32603930` are examples from one run and are not stable. RVAs and
runtime vtable offsets are the reusable information.

The result applies to the tested Cyberpunk 2077 PC build 2.31. Every callsite patch validates the
original `E8 rel32` and its target before writing, so a different executable should fail closed
instead of silently patching unrelated code.

## Why the problem was deceptive

Three separate systems run when the player presses fire:

1. Input and weapon state select the attack and drive animation state.
2. `TriggerWeaponEffects(Shoot)` starts muzzle smoke, tracer, audio, and other effect-spawner work.
3. A native effect graph performs the physical ray, damage/hit representation, and BulletImpact.

The first two systems are easy to observe and contain many values that look like ray origins,
directions, endpoints, and hit records. They are not necessarily the source of the combat hit.

The visible tracer and smoke already came from the weapon muzzle before this fix. The actual hit and
bullet hole still followed the camera. Therefore, making tracer or VFX data look correct was never
sufficient evidence. Only changing a value before the physics query and observing the real impact
could prove the lever.

## Investigation rules that mattered

The investigation followed these constraints:

- Start at the exact player bind, not at a broad list of functions containing `Shoot` or `Attack`.
- Preserve one stopped click while following synchronous branches.
- Separate animation, VFX, targeting, and combat paths instead of assuming they converge.
- Do not build a permanent hook until a one-variable x64dbg edit changes the real impact.
- Prefer RTTI, live vtables, and hardware watchpoints over names assigned to unknown functions.
- Treat old comments and hook names as hypotheses, not facts.

The static database used by the headless scripts was:

```text
C:\Users\dariulone\Desktop\ida_headless\cp2077.i64
```

The main extraction script and generated report are:

```text
weapons_RE/scripts/ranged_attack_input.py
weapons_RE/fire/ranged_attack_input.md
```

## Phase 1: anchor the exact RangedAttack click

The native `IsActionJustPressed` handler is at RVA `0xA2AD48`. The return-side breakpoint was placed
at `0xA2ADB4` and filtered for a true result plus the exact action CName.

```text
CName("RangedAttack") = 0xB8501EE760CBC0EE
condition              = al == 1 and action == RangedAttack
```

This prevented idle effects, NPC actions, and unrelated weapon state updates from being mistaken
for the player's click.

The relevant redscript order in
`cyberpunk/cyberpunk/player/psm/weaponTransitions.swift` is:

```text
SetAttack(attackID)
QueueEvent(WeaponPreFireEvent)
GetActionPressCount(...)
PushAnimationEvent("Shoot")
PlaySound(...)
TriggerWeaponEffects(Shoot)
```

Each sibling branch was checked separately.

### SetAttack was only attack selection/cache state

The native handler at `0xD6F184` calls `0xD6F20C`. Live disassembly showed a lookup/update in the
weapon's attack array around `weapon+0x3E0`, using the selected attack TweakDBID. It did not build a
physics query or hit record.

One live attack ID was:

```text
TweakDBID = 0x190724F285
```

### WeaponPreFireEvent was not the damage event

RTTI/source inspection identified this as the audio pre-fire event path (`gameaudioeventsPreFireEvent`),
not the native combat shot.

### PushAnimationEvent("Shoot") led to animation state

The pushed animation-event CName was captured as:

```text
CName("Shoot") = 0xB64036AF6988A250
```

The path copied a 0x38-byte event through the animation queue. A hardware watchpoint followed the
same event through queue relocation and reached the generic event-list `Contains(CName)` helper at
RVA `0x2CB2BD`. Filtering the watchpoint for `r10 == CName("Shoot")` found the listener at
approximately `0x2CB160`, which selected an animation state/transition. It was not the combat ray.

### gameweaponeventsShootEvent was not instantiated per player shot

RTTI exposed `gameweaponeventsShootEvent` as a large `0x1E0` type:

```text
RTTI name string                  RVA 0x2DA29B0
RTTI descriptor vtable           RVA 0x2DA29D0
constructor/factory               RVA 0x251C3E4
runtime instance vtable           RVA 0x2AEE4A8
```

The constructor did not run on the tested player shot. A heap search found an object containing that
vtable, but binary snapshots of all 0x1E0 bytes before and after a shot were identical. It was not an
active per-shot payload for this path.

## Phase 2: prove TriggerWeaponEffects was the VFX branch

The exact click reached the native `TriggerWeaponEffects` handler at RVA `0x659034` with:

```text
FxAction enum       = 16 (Shoot)
effect identifier   = 0x4665950F4451BEC5
```

The observed queue/start chain was:

```text
0x658EA0 -> 0x658FF4
0x658578 -> 0x65862C -> 0x6586E4 -> 0x6583F0 -> 0x658438
0x4EA4F0 -> 0x4EA514 -> 0x4EA564
```

`0x4EA564` enqueued effect handles for listeners. A hardware read/write watchpoint on the exact queue
slot found the real consumer at RVA `0x2F077C`.

The graph execution path was:

```text
queued Shoot effect
  -> consumer 0x2F077C
  -> indirect graph call 0x2F0892
  -> generic callable wrapper 0x1104E0
  -> concrete callback stored in the node
```

The first concrete callbacks from the exact root effects were:

| Callback RVA | Live interpretation |
|---|---|
| `0x1182240` | Sets a flag at action `+0x134`; not a hit. |
| `0x9D7110` | Service/event action; not a hit. |
| `0x5787A0` | Work on `entEffectSpawnerComponent`; queues component jobs. |

RTTI resolution of the two `0x5787A0` objects identified them as
`entEffectSpawnerComponent` instances. Their downstream path built transforms/spread-like data for
effect spawning. Both candidate `0x5B47E0` branches completed without queuing a combat child effect
through the expected `0x4EA4F0` sites.

This matched the visual evidence: tracer and muzzle smoke already followed the muzzle while damage
and bullet holes followed the camera. The entire branch was useful, but it was the VFX side of the
shot.

## The 0x1303EC correction

An existing MinHook and comments had labeled RVA `0x1303EC` as a synchronous trace and treated the
fifth argument as a ray structure with origin/end fields. The exact RangedAttack/VFX chain did reach
the function from callsite `0x5B4894`, which initially made the hypothesis look stronger.

The live entry ABI for one call was:

```text
caller return  = RVA 0x5B4899
callsite       = RVA 0x5B4894
rcx            = 0x20D3A633070
rdx            = stack/output object
r8             = stack CName/key wrapper
r9             = stack context
arg5 [rsp+28]  = stack config object
arg6 [rsp+30]  = stack context
```

Static decompilation of the original bytes showed the real fifth-argument layout:

```text
offset +0x00   int/enum
offset +0x08   ref-counted handle copied by 0x130500
offset +0x18   another handle copied by 0x103D94
offset +0x28   uint16 flags/value
offset +0x2A   uint8 flag
```

The function creates a 208-byte request through `0x1305BC`/`0x130718`, wraps it, and passes it to
`0x5795C4`. Live and static analysis of `0x5795C4` showed shared-effect-data lookup by CName/type,
not a physics raycast.

The old write at argument 5 `+0x18` therefore changed a smart-handle field, not a direction or
endpoint. This explains why it never redirected the impact and why extending that hook would have
been unsafe.

The old code remains legacy diagnostic surface, but it is not the implementation basis for native
hitscan.

## Phase 3: reject the targeting-system async ray

The targeting async-ray chain was fully reconstructed before the combat provider was found:

```text
request path     0xB23678 -> 0xD290D4
worker/readback  0xB237D8 -> 0xC3E6B4 -> 0xC3E720 -> 0xBC7E5C
result handling  0xBC7E5C -> 0x397B78
builder          0x398050
```

The request builder's local record was identified as:

```text
origin       +0x08
direction    +0x14
range        +0x20
record size  72 bytes
```

The result side was identified as:

```text
metadata stride       72 bytes
result-buffer stride  3104 bytes
hit stride            96 bytes
hit position          +0x00/+0x04/+0x08
hit count             +3072
```

The request direction and `origin -> hit` vector had a measured dot product of approximately
`1.0000001`. A temporary x64dbg code cave modified all targeting rays with range greater than 10 m.
The changed direction reached the final 64-byte physics-query structure and was not restored by a
downstream stage.

Despite that, two firearm impacts still followed the camera. The targeting async ray was therefore
real physics data, but not the native firearm damage ray being sought. The temporary code cave was
fully restored and freed.

This negative result was important: seeing a final physics query is not enough. The query must be
shown to control the tested gameplay outcome.

## Phase 4: pivot from the downstream BulletImpact fact

Instead of continuing through generic input, animation, and effect schedulers, the investigation
pivoted to the event that must occur when a bullet hole appears: `gameEffectExecutor_BulletImpact`.

RTTI strings and constructors gave the real instance vtables:

| Type | RTTI name RVA | Runtime instance vtable RVA |
|---|---:|---:|
| `gameEffectExecutor_BulletImpact` | `0x2D8C000` | `0x2B96CF0` |
| `gameEffectObjectProvider_PhysicalRay` | `0x2DBB858` | `0x2B97D50` |
| `gameEffectObjectProvider_PhysicalRayFan` | `0x2DBB740` | `0x2B977A0` |

The game had many loaded BulletImpact assets. Heap searches used the absolute runtime-vtable bytes,
then hardware read watchpoints were placed on the executor's trailing configuration bytes at
instance `+0x48` (`isBackfaceImpact`, `noAudio`, `isMeleeAttack`).

One tested shot hit the watchpoint for this live asset:

```text
BulletImpact asset = 0x20D32603930    (run-specific heap address)
mid-function hit   = RVA 0x67E0B4
function owner     = RVA 0x67DA08
```

At `BulletImpact` entry, the third argument was a wrapper whose `+0x08` pointee held the hit
representation. A ready hit position was visible in that representation at approximately `+0x10`.
This proved the path was downstream of the actual query.

The caller at RVA `0x4EAB0C` invoked the executor through virtual slot `+0x128`. Reading the same
slot from the named runtime vtables resolved all relevant concrete methods:

| Runtime type | `[vtable + 0x128]` |
|---|---:|
| BulletImpact | `0x67DA08` |
| PhysicalRay | `0x84E2D0` |
| PhysicalRayFan | `0x84CD94` |

The tested pistol stopped in `PhysicalRay`, not `PhysicalRayFan`.

This vtable-slot method was the key shortcut: it converted RTTI type names into exact executable
callbacks without guessing callback identities from a global effect dispatcher.

## PhysicalRay::Execute layout

The `PhysicalRay` native type layout from the generated RED4ext headers is:

| Field | Object offset |
|---|---:|
| `inputPosition` | `+0x40` |
| `inputForward` | `+0x58` |
| `inputRange` | `+0x70` |
| `outputRaycastEnd` | `+0x88` |

Live disassembly of RVA `0x84E2D0` matched that layout exactly. In simplified pseudocode:

```cpp
Vector4 origin;
Vector4 rawForward;
float range;

Eval(asset + 0x40, context, &origin);       // callsite 0x84E31F
Eval(asset + 0x58, context, &rawForward);   // callsite 0x84E354
Eval(asset + 0x70, context, &range);        // callsite 0x84E369

Vector4 forward = Normalize(rawForward);    // callsite 0x84E376
Vector4 end = origin + forward * range;     // 0x84E384..0x84E38B

SubmitPhysicalRay(..., &end, ...);          // 0x84E47A -> 0x84ED50
```

The shared evaluator target used by all three input calls is RVA `0x1203B0`. The normalize target is
RVA `0x13DE80`.

A live camera-ray sample immediately before normalization was:

```text
origin  ~= (564.0, -2424.3, 188.2, 1.0)
forward ~= (0.1405, 0.9818, -0.1283, 0.0)
range   = 100.0
```

The stack locals relative to the function's established frame pointer were:

```text
origin       rbp - 0x69
raw forward  rbp - 0x49
range        rbp + 0x7F
end          rbp - 0x79
```

## Decisive manual proof

A breakpoint was placed at RVA `0x84E376`, after all three evaluations and before normalization.
Only the raw-forward local was changed:

```text
old: current camera forward
new: (0.0, 0.0, 1.0, 0.0)
hex: 00000000 00000000 0000803F 00000000
```

The stopped shot was then resumed without changing origin, range, effect assets, hit data, or any
downstream function.

Observed result:

- The shot no longer hit the camera-crosshair point.
- It went in the forced direction/upward.
- The test was repeated and observed explicitly.

This proved all of the following at once:

- `PhysicalRay::Execute` belongs to the real player firearm hit.
- The forward local survives into the submitted combat query.
- No downstream stage restores the camera direction.
- A permanent fix can be placed before normalization.

## Production hook design

### Why patch callsites instead of detouring the evaluator globally

RVA `0x1203B0` is a generic effect-input evaluator. Detouring it globally would add overhead and
risk to every effect input in the game. The implementation instead replaces the two exact `E8`
instructions inside `PhysicalRay::Execute`:

```text
kWaPhysicalRayOriginCallsite  = 0x84E31F
kWaPhysicalRayForwardCallsite = 0x84E354
kWaPhysicalRayEvalFn          = 0x1203B0
```

Before patching, each site is checked for:

```text
opcode == E8
decoded rel32 target == Cyberpunk2077.exe + 0x1203B0
```

The plugin DLL is not guaranteed to be within rel32 range of the executable callsite. Each patched
call therefore targets a small executable relay allocated near the callsite. The relay is:

```asm
mov rax, <64-bit hook address>
jmp rax
```

The original evaluator remains callable directly at `base + 0x1203B0`.

### Player-ray filtering

`PhysicalRay` is a generic effect provider, so not every invocation belongs to the local player's
weapon. The first filter compared the ray's origin against the VR muzzle and accepted anything
within 1.5 m. **That was wrong, and measurably so.**

The player's own character fires service rays through the same provider. One was caught in the
debugger while the player was simply running:

```text
ray origin        (376.7643, -2391.5874, 182.3734)     player entity position + 0.5 m
direction         (0, 0, -1)                           straight down
range             2.0
|origin - camera| 1.217 m
```

A ground probe. With a weapon in hand the muzzle sits roughly 0.5 m in front of and 0.25 m below the
camera, putting it about 1.0 m from that origin -- inside the 1.5 m gate. Every frame of movement was
rotating the character's own ground probe onto the barrel. It only escaped notice during testing
because the weapon was holstered, which left the muzzle slot stale 192 m away. There is more than
one such service effect; a second one with a different asset appeared during the same test.

An NPC firing at contact range would have passed the same gate.

Distance cannot answer the question, so the filter now asks the game. The effect's shared data
carries the answer, and the two patched callsites are handed that container in `rdx`.

#### The container

Read out of the blackboard's vtable slot `+0x108` (RVA `0x339710`) and verified live:

```text
keys  = *(uint64_t**)(bb + 0x48)     CName array, searched linearly
count = *(uint32_t*)(bb + 0x54)
vals  = *(uint8_t**)(bb + 0x58)      24 bytes per entry
  entry +0x00   type pointer, low bit is a tag; the pointer part must be non-zero
  entry +0x08   the value, inline for a small type
```

Reading it directly rather than calling the virtual getter is deliberate: the getter takes the
container's lock and returns a variant that must be released, and neither is worth doing from a
detour on a worker thread for three loads.

#### The fields, resolved

A CName here is FNV1a64 of the name, so both captured key sets resolve back to text:

```text
shot, 24 keys:
  weaponItemRecord  range  attackStatModList  ricochetData  ricochetCount  spreadingData
  spreadingCount  spreadingTargets  hitCooldown  muzzlePosition  charge  fxPackage
  playerOwnedWeapon  inTPP  ricochetInRow  minRayAngleDiff  lastRayDir  lastRayPos  shootTime
  randRoll  ignoreMountedVehicleCollision  attackData  position  forward

ground-check ray, 4 keys:
  range  position  forward  attackId
```

`playerOwnedWeapon` is a `Bool`, registered in the shared-data table at entry 148 by
`sub_140824108`, CName `0x8CB4C0891BD255ED`. On the measured player shot it was present with value
`1`; the entry after it shared the same type pointer and held `0`, which is what confirms the value
is read from `+0x08` rather than from the tag. On the ground-check ray the field is absent.

That field list is also what settles what the flag means. It is not "the player is holding a gun":
a global player state would not sit between `spreadingData` and `attackData` in the shared data of
one shot. It is a property of the weapon that fired *this* ray, next to `weaponItemRecord`, which is
the record of that same weapon.

The gate is therefore:

```cpp
mine = blackboardHas(weaponItemRecord) && blackboardBool(playerOwnedWeapon);
```

`weaponItemRecord` on its own only proves the ray came out of a weapon -- an NPC's shot carries it
too -- but it costs nothing in the same pass and keeps a hypothetical non-weapon player effect that
happened to carry the flag from being treated as a shot.

Every failure mode -- unreadable memory, an absent field, a torn count -- falls out as "not ours",
which leaves the shot vanilla.

A single distance test survives, and it is not identity: the published muzzle must be within 3 m of
the ray's origin. The muzzle slot keeps its last value when the weapon is holstered, and one was
measured 192 m from the camera that way; firing along a stale barrel would throw the bullet sideways.
Counted separately as `StaleMuzzle`, and expected to stay at zero.

A `thread_local` boolean carries the match from the origin callsite to the immediately following
forward callsite of the same `PhysicalRay::Execute` invocation:

```text
origin wrapper:
  clear TLS flag
  call original evaluator
  validate muzzle data and distance
  write muzzle origin
  set TLS flag

forward wrapper:
  call original evaluator
  consume and clear TLS flag
  if set, rotate the original game forward by the sequence's spread delta
```

The forward wrapper never redirects a ray merely because it ran while a weapon was active; it must
be paired with a spatially matched origin on the same thread.

### Shotgun spread preservation

The first implementation wrote the exact muzzle `+Y` vector into every matched `PhysicalRay`. That
was correct for a pistol but collapsed every shotgun pellet onto one point.

The first spread-preserving attempt grouped pellets by `g_provMuzzleSeq` and used the first matched
raw forward as the cone center. A controlled one-shot counter reset disproved that grouping:

```text
one shotgun shot:
  PhysicalRay calls       6
  matched origins         6
  spread resets           6
  PhysicalRay recoil wins 1
```

The six calls also arrived on different worker threads with different evaluator `rdx` contexts.
Neither muzzle sequence, TLS state, nor the per-pellet execution context was a shot-group key.

The second attempt grouped pellets by `g_waTargetFromShot` and used `g_waTargetDir` as the shared
cone center, on the belief that the TargetHelper hook fires once per fired round. It fired the whole
group slightly away from the barrel and jittered between shots. A before/after counter read around
one single shotgun shot showed why:

```text
one shotgun shot, deltas:
  g_waTargetCalls        +12   (the hook incremented it twice per call -- a second defect)
  g_waTargetFromShot      +6
  PhysicalRay calls        6
```

`g_waTargetFromShot` advances **once per pellet**, not once per round. There was no round sequence
and no shared center: every pellet published its own already-spread direction, and the workers raced
to read whichever one was latest. Two consecutive samples of `g_waTargetDir` also had lengths of
6.36 and exactly 100.0, so the two accepted return addresses are two different kinds of call and
were being mixed in one variable.

No seqlock can fix a value that is per-pellet. The center had to come from somewhere that is
per-FRAME.

### The cone center is the game camera's forward

Measured, not assumed. With `CyberpunkVR_HitscanFromMuzzle` set to 0 in the debugger so the game ran
vanilla, a software breakpoint at RVA `0x84E38F` -- the instruction where both branches of
`PhysicalRay::Execute` meet, after `end` is built -- caught all six rays of one shotgun shot:

```text
origin, identical for all six   (558.6611, -2434.0386, 177.4059)
shared slot 204 (rendered cam)  (558.6534, -2434.0569, 177.4082)
range                            50
each raw forward                 already unit length
mean of the six unit forwards   (0.376530, 0.919394, -0.113648)
forward of g_lastLocateQuat     (0.382462, 0.917179, -0.111831)
angle between them               0.47 deg
per-pellet angle from that axis  0.38, 0.53, 1.50, 1.15, 0.80, 0.74 deg
```

The mean of six samples of a ~1 degree cone is expected to miss the true axis by about half a
degree, so within the resolution of six samples the camera forward **is** the axis the game spreads
around. The rays' shared origin is the rendered camera position, not the weapon.

That makes the center a property of the frame. It is identical for every pellet, so every worker
reads it for itself and nothing has to cross a thread:

```text
coneCenter  = rotate((0,1,0), g_lastLocateQuat)      // game forward is +Y
spreadDelta = rotationBetween(coneCenter, muzzleForward)
redirected  = normalize(spreadDelta * rawPelletForward)
```

Consequences:

- The camera axis the game spread around becomes the barrel axis.
- Every pellet keeps its exact angular offset, so the pattern is the game's own.
- A single-ray weapon has `raw == coneCenter` and comes out exactly on the barrel. The pistol is not
  a special case; it is the same arithmetic with a zero offset.
- No shot sequence, no first-pellet latch, no seqlock, no cross-thread handshake.

`g_lastLocateQuat` is deliberately the one used, not `g_headQuatComposed`. It mirrors the camera as
the locate site actually published it, which is the camera the engine had when it built the cone --
including the frames where the HMD orientation write is skipped. It is also the quaternion the
barrel dot is drawn from, so the dot and the bullet stay in one frame. Read live at the same instant,
the two globals were bit-identical.

Both bases are sampled in the ORIGIN wrapper and carried to the forward callsite in TLS. Re-reading
either one at the forward callsite would let a frame boundary land between the two halves of one
ray's rotation.

### The counters were lying

One measured round reported 6 origin writes against 4 forward writes, which reads as "two pellets
flew unredirected". They had not. Six pellets run on six worker threads and a plain `++` on a shared
`unsigned long long` loses increments. The vanilla capture above proved all six rays reached the
forward evaluation. Every PhysicalRay counter is now an `InterlockedIncrement64`.

The same class of defect was in the TargetHelper hook itself: `++g_waTargetCalls` appeared twice, so
its reported call count was double the truth.

### The second branch of PhysicalRay::Execute

`PhysicalRay::Execute` is not one path. At RVA `0x84E33B` it evaluates a second input and, if that
returns non-zero, jumps to `0x84E512`, which skips the forward and range evaluations entirely:

```text
rcx = [rbp-0x59]            ; the object that evaluation produced
call [vtable + 0x108]       ; its world position
[rbp-0x79] = that position  ; end point written DIRECTLY
jmp 0x84E38F                ; rejoin the query
```

That branch aims the ray at a target entity's position and has no direction to redirect. In the
measured shotgun round the handle at `[rbp-0x59]` was null for all six rays, so the branch was not
taken and it is not currently handled. If a hitscan weapon is ever seen ignoring the barrel while
`OriginWrites` still advances, this is the first thing to check.

### Native hitscan hand recoil

The older recoil edge lived in the instrumented `entFunc` orientation-provider slots. Those slots
are dependable on the projectile path but can be silent or not installed for the native
`PhysicalRay` path. The exact matched PhysicalRay origin is now also a recoil edge.

Projectile/provider and PhysicalRay paths share one latch:

```text
g_provMuzzleSeq         current published muzzle sequence
g_provRecoilSeqSeen     last sequence that kicked the hands
```

Both paths use `InterlockedCompareExchange` before calling `RecoilOnShot()`. Therefore:

- The first matched path for a sequence produces one hand kick.
- Multiple shotgun pellets in the same sequence do not multiply recoil.
- If both entFunc and PhysicalRay run for one shot, they cannot produce two kicks.

### Muzzle data contract

The final `WaMuzzle` contract is:

```text
shared[200..202]  muzzle world position
shared[203]       muzzle-position valid flag
shared[24..26]    muzzle transform's world +Y axis
shared[27]        muzzle-forward valid flag
```

The returned forward is normalized before use. If either valid flag is absent, the exact PhysicalRay
rewrite is skipped.

The feature remains controlled by:

```text
CyberpunkVR_HitscanFromMuzzle
```

### Diagnostics

The plugin exports these counters:

| Export | Meaning |
|---|---|
| `CyberpunkVR_DebugPhysicalRayPatched` | `1` when both callsites were validated and patched; negative values identify installation failure. |
| `CyberpunkVR_DebugPhysicalRayCalls` | Origin-evaluator wrapper calls. |
| `CyberpunkVR_DebugPhysicalRayMine` | Calls whose evaluated origin passed the local muzzle-distance gate. |
| `CyberpunkVR_DebugPhysicalRayOriginWrites` | Muzzle-origin replacements. |
| `CyberpunkVR_DebugPhysicalRayForwardWrites` | Paired muzzle-forward replacements. |
| `CyberpunkVR_DebugPhysicalRayConeRays` | Rays redirected with a valid camera cone center. Was `SpreadResets`, which counted rounds back when the redirect believed rounds existed. |
| `CyberpunkVR_DebugPhysicalRayRecoil` | Sequences where PhysicalRay won the shared recoil latch and called `RecoilOnShot()`. |
| `CyberpunkVR_DebugPhysicalRayRawForward[4]` | Most recent game-evaluated pellet forward before cone rotation. |
| `CyberpunkVR_DebugPhysicalRayOutForward[4]` | Most recent pellet forward after cone rotation. |

All of them are `InterlockedIncrement64`, because they are written from every pellet's worker thread
at once and a plain `++` already produced one false conclusion.

For a healthy paired path, origin write, forward write and cone-ray counts all advance together and
equal the pellet count. A large call count with no `Mine` count means the engine callsite patch works
but no origin is close enough to the published muzzle, or muzzle validity is absent. Forward writes
short of origin writes now means something real: either the camera quaternion failed its unit check,
or the ray took the target-entity branch described above.

## The axis bug found after the first implementation

The first permanent build used `shared[60..62]` for forward because older weapon code described it
as controller/shot forward. The bullet then left neither along the camera nor the visible barrel.
Rotating or inverting the pistol could make it hit in apparently unrelated directions.

Live data showed why. One frame contained vectors roughly like:

```text
shared[60..62]  ~= (0.58, 0.75, +0.30)   old controller/projectile bridge
shared[24..26]  ~= (0.61, 0.44, -0.66)   actual muzzle +Y in world space
```

The sign and vertical component were materially different. `shared[60..62]` does not include the
weapon model/muzzle-slot axis correction and may only be meaningful on the older projectile bridge.

The working projectile instrumentation in `OrientationProvider.cpp` provided the correct answer:

- `entFunc` real slot 33 is the projectile launch orientation.
- `entFunc` real slots 36 and 37 are hitscan-related orientation reads.
- Mode 6 rotates the shot cone onto `g_provMuzzleQ` while preserving pellet offsets.
- REDengine uses local `+Y` as forward for this muzzle orientation (`g_provFwdAxis == 1`).

`SetVRMuzzleQuat` already publishes that exact axis:

```cpp
shared[24] = 2 * (i*j - k*r);
shared[25] = 1 - 2 * (i*i + k*k);
shared[26] = 2 * (j*k + i*r);
shared[27] = 1;
```

After `WaMuzzle` switched from `shared[60..62]` to `shared[24..26]`, the user confirmed that the
native bullet travelled along the barrel.

## Barrel dot correction

Once the bullet genuinely started at the muzzle, the existing barrel dot appeared not to match it.
This was a separate projection issue, not another hitscan-axis failure.

The previous default was:

```text
CyberpunkVR_BarrelDotWorld = 0
```

That mode projected only the muzzle direction as a point at infinity. It ignored the muzzle origin.
A real bullet starting at the muzzle and hitting a finite wall does not project to the same screen
pixel as an infinite direction when the eye is offset from the barrel. The error is ordinary
eye-to-muzzle parallax.

The already implemented world-point path projects:

```text
dotWorld = muzzlePosition + muzzleForward * CyberpunkVR_BarrelDotDistM
```

The live values were:

```text
CyberpunkVR_BarrelDotDistM = 20.0
CyberpunkVR_SightZeroMeters = 20.0
```

`CyberpunkVR_BarrelDotWorld` was changed to default to `1`. A live memory toggle enabled the mode
before rebuilding, and the user confirmed that the dot and bullet agreed at approximately 20 m.

This is a zeroed sight, not a continuous wall-intersection laser:

- At 20 m, the point is on the physical bullet line and the dot is expected to agree.
- At other distances, parallax between the eye and muzzle produces an expected offset.
- A dot that always sticks to the first wall would require a continuously updated muzzle physics
  raycast or a depth-derived intersection; that is not implemented here.

## Rejected levers and what each one taught us

| Candidate | Live result | Conclusion |
|---|---|---|
| Look-at/aim-point setter | Stored plausible camera aim data; writes did not move the combat hit. | Camera targeting state is not the final firearm query. |
| Targeting async ray at `0x398050` | Direction changed all the way into its final physics query; bullet still hit camera point. | Real targeting physics, wrong gameplay consumer. |
| `TriggerWeaponEffects(Shoot)` graph | Exact click led to tracer/smoke/effect spawners. | VFX branch; visible correctness did not prove damage correctness. |
| `0x1303EC` legacy "trace" | Argument 5 was handles/config; `0x5795C4` was shared-data lookup. | Old hook layout was false and potentially corrupting. |
| `gameweaponeventsShootEvent` constructor | No per-shot construction; candidate heap object unchanged. | Not the active tested player-fire payload. |
| `PhysicalRayFan::Execute` | Breakpoint did not catch the tested pistol, while `PhysicalRay` did. | Do not reuse fan-specific offsets for the pistol. |
| `shared[60..62]` | Redirected hit, but not along visible barrel; inversion symptom. | Controller/legacy bridge vector lacks muzzle model-axis correction. |
| Barrel dot direction mode | Bullet followed barrel but missed dot at finite distance. | Dot ignored muzzle origin; use world-point zeroing. |
| `g_provMuzzleSeq` as the shot key | 6 spread resets for one 6-pellet round. | Muzzle sequence advances between pellets; it is not a round. |
| Evaluator context `rdx` as the shot key | Six pellets, six different contexts, six worker threads. | Nothing per-round is reachable from one pellet's call. |
| `g_waTargetFromShot` as the shot key and `g_waTargetDir` as the cone center | Group landed off the barrel and jittered; the counter rose by 6 for one round. | TargetHelper is called once per PELLET, and its direction already carries that pellet's spread. |
| A seqlock around that publication | Coherent snapshots of a value that was never per-round. | Making a wrong value race-free does not make it right. |

## Reproduction recipe

### Static preparation

1. Open the 2.31 IDA database.
2. Resolve the RTTI names for `gameEffectExecutor_BulletImpact` and
   `gameEffectObjectProvider_PhysicalRay`.
3. Follow their factories to the runtime instance vtables.
4. Read virtual slot `+0x128` to recover `0x67DA08` and `0x84E2D0`.
5. Disassemble `0x84E2D0` and verify the three calls to `0x1203B0` at the documented callsites.

### Live proof

1. Run in VR with an ordinary firearm and no projectile conversion for the tested shot.
2. Break at `Cyberpunk2077.exe + 0x84E376`.
3. Confirm the function was reached by the player shot and inspect:

```text
[rbp-0x69] origin
[rbp-0x49] forward
[rbp+0x7F] range
```

4. Replace `[rbp-0x49]` with `(0,0,1,0)`.
5. Resume the same shot.
6. Verify that the physical impact leaves the camera-crosshair direction.
7. Restore/remove all temporary breakpoints; no engine bytes need remain modified for the proof.

### Production validation

1. Build and deploy the plugin.
2. Verify `CyberpunkVR_DebugPhysicalRayPatched == 1`.
3. Fire several shots and confirm `OriginWrites` and `ForwardWrites` rise together.
4. Verify impact direction against the physical muzzle, not only tracer smoke.
5. Check the barrel dot at the configured 20 m zero distance.
6. Test a close obstacle to verify the ray starts at the muzzle rather than passing through it from
   the camera.
7. Fire a shotgun and verify that several pellet impacts remain distributed around the barrel center.
8. Confirm one hand-recoil kick per trigger event, not one kick per pellet.
9. Verify `OriginWrites`, `ForwardWrites` and `ConeRays` all advance by the pellet count for the
   tested shot. They should be equal; any gap is a real defect now that the counters are atomic.

## Build and deployment command used

The tested Release build target was:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --build "C:\Users\dariulone\Desktop\CyberpunkVRPort\build" --config Release --target cyberpunkvrport_stereo
```

The output DLL was copied from:

```text
build\bin\red4ext\plugins\CyberpunkVR_Stereo\Release\CyberpunkVR_Stereo.dll
```

to:

```text
C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\red4ext\plugins\CyberpunkVR_Stereo\CyberpunkVR_Stereo.dll
```

The game must be stopped before replacing the loaded DLL.

## Known limitations and follow-up work

### Cone-center freshness

The center is the camera of whichever frame `g_lastLocateQuat` last described, while the pellet
directions were built from the camera of the frame the game scheduled the job in. If those are not
the same frame, the whole pattern shifts by the head's angular travel over one frame -- about
1.4 degrees at 72 Hz during a 100 deg/s turn, which is the order of the spread itself. It cannot
scatter the pattern, only translate it, and both bases are sampled at one instant so the two halves
of a single ray can never disagree.

If that ever needs to be tighter, the fix is not a smaller window: the game's own ray origin arrives
in the origin wrapper and equals the rendered camera position of the frame it used, so the correct
frame can be identified rather than assumed.

### The target-entity branch is unhandled

`PhysicalRay::Execute` can write its end point straight from a target entity's world position and
never evaluate a direction at all. Not observed on the player's shotgun, not handled. See the
section above for the exact addresses.

### playerOwnedWeapon has not been read off an NPC's shot

The gate rests on the game's own flag, and every measured case behaves: the player's shot sets it,
the player's service rays do not carry it at all. What has not been captured is an NPC firing, so
the flag is trusted rather than proven to be false for someone else's gun. Its name, its type and
the company it keeps all say it is per-weapon, but that is reasoning, not a measurement.

If an NPC's shot ever reads true, `weaponItemRecord` will not save it either, and the instigator has
to be dug out of the effect instead. The way in is known: `sub_14084EA48` -- the gate on
`PhysicalRay::Execute`'s second branch -- resolves a static descriptor against the same container
and then calls `IsA` on the result (`sub_140339AD0`), so the machinery for "fetch an object out of
the effect and test its class" is already sitting in the function being patched.

To capture one: breakpoint `0x84E36E` with `dword:[rbp+0x7F] > 0x41200000` to select long rays only
(service rays run 2 m), then read `[r14+0x48]` and walk the container. The condition is what makes
this bearable -- without it the breakpoint fires on the player's ground probes every few steps.

### Barrel dot is zeroed, not surface-locked

The dot is exact at the configured 20 m zero distance. It does not perform a continuous physics
raycast against the wall under the barrel. Surface-locked laser behavior is separate work.

### Version sensitivity

The RVAs are for Cyberpunk 2077 2.31. The byte/target checks prevent blind callsite patching, but a
new executable requires static re-verification of `PhysicalRay::Execute` and its evaluator calls.

## Durable lessons

1. Trace exact input, but do not force every sibling branch into one assumed shot pipeline.
2. VFX correctness is not damage correctness.
3. A final physics query can still belong to the wrong subsystem.
4. Named RTTI plus a live instance vtable can be more useful than thousands of generic callbacks.
5. Hardware-watch stable asset configuration fields to find the concrete executor.
6. Change one stack local immediately before the query; observe the gameplay outcome before coding.
7. Reuse the weapon model's published muzzle transform and established local axis. A controller
   forward vector is not automatically a weapon-barrel forward vector.
8. A screen-space direction marker and a finite world impact differ when the eye is not on the
   bullet line. State the sight's zero distance explicitly.

## Related files

```text
include/Anim/WeaponAimState.hpp
src/Hooks/WeaponAim.cpp
src/Natives/OrientationProvider.cpp
src/Overlay/OverlayDebugDraw.cpp
include/Utils/SharedSlots.hpp
weapons_RE/scripts/ranged_attack_input.py
weapons_RE/fire/ranged_attack_input.md
weapons_RE/scripts/aim_async_workers.py
weapons_RE/aim/aim_async_workers.md
```

See also `workflow-static-reverse-first.md` for the project's static-first hook policy.
