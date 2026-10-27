# Projected world marker checks

```powershell
cmake -S tools/world_marker_tests -B build/world-marker-tests -A x64
cmake --build build/world-marker-tests --config Release
ctest --test-dir build/world-marker-tests -C Release --output-on-failure
```

The WARP tests capture the production vertex shader's stream output and compare
it with projections of world targets from two cameras 64 mm apart. Cases cover
near/far depth, both MAIN eye assignments, FOV, ADS zoom, unchanged MAIN placement,
UV/color preservation and text transforms at multiple output sizes. The depth
test distinguishes camera-forward depth from radial distance and rotates the camera.

The ASM fixture clobbers all Win64 volatile registers, XMM lanes and flags in the
callback, verifies the original LEA result, and captures a stack trace to require
the native caller to survive unwinding. The earlier mid-function JMP version
failed that last check; the call-site replacement passes it.

When local capture exports exist in `build/world-markers-re/shaders`, CMake also
compares outputs against the actual native vertex shaders and links the replacement
with the actual native pixel shaders. The latter caught an SM5/SM6 mismatch that
VS-only tests cannot detect. The test first creates the original native pair so a
bad fixture/root signature is distinguishable from a replacement failure; native
ink uses t77/s2. Production shaders are built with Windows SDK DXC as SM6 and
embedded in the DLL. These game bytecode fixtures stay in the ignored build
directory. No RenderDoc replay or hardware GPU is used by these tests.

After the user starts the game, run `live_probe.py --pid <fresh PID> --out <new JSON>`
with Python 3.13. It checks the installed/local DLL hashes, resolves the matching
PDB and uses read-only process access. Check that root/quad counters and both
camera counters advance; text tagging must also reach the per-eye text uploads.
Then compare marker attachment to the world target in the simulator. Equal
left/right pixel coordinates are not the acceptance condition.

`WorldMarkerMappinStages` counts entry, enabled, root locked, world controller type,
HUD layer, remembered, and valid world position, in that order. The corresponding
direct projection stages count entry, enabled, logic owner, HUD root and remembered.
Game controllers belong to the layer/controller system; the world-mappin container
and all its widget ancestors can legitimately have no primary logic controller.
