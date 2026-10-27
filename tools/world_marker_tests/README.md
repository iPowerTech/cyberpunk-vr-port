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
callback, verifies the 2.5 publication call's original RCX argument and tail target,
and requires the native caller to survive stack unwinding.

For native 2.5 parity and linkage, first extract fixtures from the unpacked game
cache, then configure/build the tests:

```powershell
python -B tools/world_marker_tests/extract_fixtures.py build/patchday/base-2.5/engine/shader_final.cache build/world-markers-2.5
```

When those fixtures exist, CMake compares all six native vertex shader variants
in mono and both stereo eyes, and links their actual native pixel shaders.
The latter caught an SM5/SM6 mismatch that
VS-only tests cannot detect. The test first creates the original native pair so a
bad fixture/root signature is distinguishable from a replacement failure; native
ink uses t77/s2. Production shaders are built with Windows SDK DXC as SM6 and
embedded in the DLL. These game bytecode fixtures stay in the ignored build
directory. No RenderDoc replay or hardware GPU is used by these tests.

The `reprojection` and `world_shader` cases cover UI canvas unprojection, native
camera basis, removal of DLSS jitter, asymmetric eye projection during yaw,
sprite/glyph agreement, opacity/UV preservation and mixed cache ownership.
`marker_size` also checks that scaling an NPC cache keeps its native anchor fixed
when the texture bounds change.

After the user starts the game, run `live_probe.py --pid <fresh PID> --out <new JSON>`
with Python 3.13. It checks the installed/local DLL hashes, resolves the matching
PDB and uses read-only process access. Check that root/quad counters and both
camera counters advance. Engine stereo now decodes direct and batched text in the
VS; its old CPU text-upload counters are therefore no longer an acceptance gate.
Then compare marker attachment to the world target in the simulator. Equal
left/right pixel coordinates are not the acceptance condition.

`WorldMarkerMappinStages` counts entry, enabled, root locked, world controller type,
HUD layer, remembered, and valid world position, in that order. The corresponding
direct projection stages count entry, enabled, logic owner, HUD root and remembered.
Game controllers belong to the layer/controller system; the world-mappin container
and all its widget ancestors can legitimately have no primary logic controller.
