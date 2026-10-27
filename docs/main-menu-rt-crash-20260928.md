# Main-menu ray-tracing startup crash

Reports: `Cyberpunk2077-20260928-195930-12832-25756` and the subsequent
`Cyberpunk2077-20260928-200817-2276-4196`. The user confirmed no Continue/Load
button was pressed: this happened in the main menu, not during gameplay.

## Evidence

Both reports fault at EXE RVA `0x37E622`, reading address `0x78`, with R8=0.
This is the native AccelerationStructurePrepare helper `0x37E4F8`, called by
the node at `0x37E44C`. Its loop selects the first view whose rectangle is
nonempty; it dereferences the null result when no such view exists.

The first dump's work context is `0xF5E91FF600`. Its manager has one view,
`0x2E9AA882980`, whose name key is `0x77AD6D6871650500` (vrcam_feed_2560x2560).
Its rectangle at +0x14 is all zeros; render dimensions are 1485x1485 and output
dimensions 2560x2560. The second dump repeats the same instruction and null
selection. An internal VRCAM context already exists although no gameplay second
eye should be active in this menu.

The faulting DLL is SHA-256
`75D3C12786C26585EBF70C5DF7600D947AE40B74BDDDAE5F5CFD7EB810C175E5`.
Steam Frame's extension is reported unsupported/inactive in this launch. The
crash is in native RT scene preparation, not in controller polling or framegen
input copying. The exact first cause of the timing change is not established.

## Changes

The Lua selector previously treated `Game.GetPlayer()` as sufficient to enable
the RTT camera. A main-menu preview player violates that assumption. Check the
engine's `SystemRequestsHandler:IsPreGame()` before activation; missing/failing
session information also suspends activation. Keep preview/restored components
disabled, clear the active-camera bridge, and rebind after gameplay is ready.
Pause/inventory menus are not treated as pregame. Existing save-replacement,
same-ID player changes, user-disabled state, and story replacers retain their
existing behavior. Native registration of IsPreGame and its Boolean wrapper
were verified in EXE 2.31.

Separately, first view setup commits the rectangle before computing its render
dimensions. The old FlagCompute-to-RectCompute override was only armed when
those previous-frame dimensions were nonzero. Arm it for a named VRCAM even on
cold setup; when dimensions are absent, use RectCompute's native destination
bounds. Scope consumption to that view's exact input and the verified commit
caller `0x4E4FCC`, excluding the earlier dimension helper at `0x4E3E82`.
Initialized views retain the same width/height behavior. No RT node is skipped,
no RT feature bit is disabled, and no shared AS state is fabricated.

## Verification

- Release DLL built successfully.
- LuaJIT fixture fails on the previously installed Lua module: the menu preview
  enables VRCAM. It passes with the new main-menu/session gate, including return
  to menu, resumed gameplay, loading, same-ID replacement, and story replacers.
- New viewport regression reproduces the null selection and checks cold setup,
  initialized/non-square sizes, exact caller/view isolation, and invalid bounds.
- Render parity (6), autograss readiness (1), and framegen (17) CTests pass.
  The separate LuaJIT lifecycle test and Lua structural checks also pass.
- No user settings, RT/FG settings, calibration, or launcher selection changed.

Native disassembly and faulting DLL/PDB are archived in
`build/menu-rt-crash-20260928/`. The user confirmed successful startup on the
installed BCA3C4A0 candidate, PID 27628. A separate 3-FPS problem after disabling
RT was then diagnosed and fixed; see `rt-disabled-stale-flags-20260928.md`.
