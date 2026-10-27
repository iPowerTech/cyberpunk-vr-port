# Native Shader Sources 271: CPU Conversion

## Scope And Result

This run uses only the 14 creation-time source snapshots exported by
`build/single-pass-20261004/native-shader-sources-271/retained-replace.json`
and its `retained-replace-native-shaders/*.bin` files. Snapshot `id` plus exact
VS/PS/root SHA256 identifies a source. Pipeline addresses are retained only as
provenance; no shader identity was inferred from an older address join.

- Source capture PID: `1744`.
- Source DLL SHA256: `b7e18bfa5bfa6f99d2ec2296a60660da2bf77a9ad3722b347bb038f984b6773f`.
- All 14 VS/PS/root source hashes and byte lengths verified before processing.
- 23 unique shaders: 11 VS and 12 PS. All 23 converted and CPU-validated.
- All 14 source pairs passed camera conversion and VS/PS interface normalization.
- Zero conversion, assembly, DXIL validation, or interface-normalization failures.
- Camera stage masks: 7 VS-only (`1`), 7 VS+PS (`3`).
- 18 unique camera stages patched; 5 PS stages receive only the SM6.1 upgrade.
- Warm rerun: 23 batch-cache hits and 23 native-camera-cache hits.
- All 28 final VS/PS references rechecked against their recorded SHA256 and length.
- No native PSO creation, standalone pipeline executable, GPU replay, game run,
  deployment, or shared build was performed. Runtime admission remains false.

All outputs are confined to `build/single-pass-20261004/source-271-conversion/`.
No existing source, CMake file, or shared/deployment manifest was changed.

## Reproduce

Run from the repository root:

```powershell
& 'C:\Users\dariulone\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -B `
  build/single-pass-20261004/source-271-conversion/convert_sources.py `
  --source build/single-pass-20261004/native-shader-sources-271/retained-replace.json `
  --dxc-dir 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64' `
  --assembler build/single-pass-tests/Release/dxil_tool.exe
```

Both cold and warm executions returned exit code `0`. The cold execution made
106 successful tool calls: 23 original IR dumps and validations, 23 MAIN-first
IR dumps and validations, and 14 linked-PS validations. The warm execution
reused both stage caches and reran the 14 pair-specific linked-PS validations.

Exact child commands and their outputs are in `commands-cold.json` and
`commands.json` respectively. `toolchain.json` fingerprints DXC, both DXC DLLs,
the assembler, the driver, and the imported transformation helpers.

The driver uses these existing APIs without modifying them:

1. `batch_shaders.collect`, `process_shader`, `classify`, and `pipeline_report`.
2. `package_native_camera.convert_shader`, which calls
   `patch_camera.patch(source, main_first=True)` or `upgrade_stage.upgrade`.
3. `package_native_camera.link_native_stages`, which calls
   `link_stage_signatures.link_pixel_inputs` and revalidates the result.
4. `package_native_camera.save_binary` for content-addressed output blobs.

`dxil_tool.exe` runs the existing optimizer, assembler, and DXIL validator with
`-hlsl-dxilload -viewid-state -hlsl-dxilemit`; it does not create a D3D device.
The full `package_native_camera.package` entry point was not used because it
requires a pre-existing family manifest containing the separately audited depth
family. These 14 source records are instead packaged as explicit CPU candidates.

## Coverage

The table abbreviates hashes for readability. `coverage.json` contains each
record's full original VS/PS/root SHA256, final VS/PS SHA256 and file, input
layout, exact resource declarations, camera words, interface remapping, command
validation output, and an empty `failures` list. Resource columns are declaration
counts in `SRV / CBV / sampler` order, not texture sample counts or array lengths.

| Source ID | Native VS Prefix | Native PS Prefix | Camera Mask | VS Resources | PS Resources | PS Wave Calls | CPU Result |
|---|---|---|---:|---|---|---:|---|
| 647 | fe53e834aca0 | 8c5a09dee942 | 1 | 0/2/0 | 2/3/1 | 0 | PASS |
| 1777 | d13385d2f6f7 | 9a4947a3ec0e | 3 | 0/3/0 | 9/4/3 | 1 | PASS |
| 1338 | bba498cc4f95 | 87bbb91c70a7 | 3 | 5/8/3 | 6/5/4 | 0 | PASS |
| 1339 | bba498cc4f95 | 823bf269925a | 3 | 5/8/3 | 5/3/4 | 0 | PASS |
| 1368 | 53cdc364f2b7 | 3dd921bbe194 | 3 | 5/8/3 | 4/3/3 | 0 | PASS |
| 1748 | 84b48dd0ebdd | 0cf4b32c8231 | 3 | 0/2/0 | 2/4/2 | 0 | PASS |
| 1776 | 6fb9f374c781 | f826f65240d2 | 1 | 0/3/0 | 3/3/1 | 0 | PASS |
| 1816 | 44893a897a96 | 45c15e331f3b | 1 | 0/2/0 | 2/3/1 | 0 | PASS |
| 2166 | 59f66f3a0ee5 | 89d32ab698c3 | 3 | 1/3/0 | 5/5/3 | 0 | PASS |
| 2186 | 59f66f3a0ee5 | ffd3910ddc72 | 3 | 1/3/0 | 6/5/4 | 0 | PASS |
| 3047 | 966e6dc108a7 | 7f451184ac81 | 1 | 0/4/0 | 2/3/1 | 0 | PASS |
| 3148 | bbc3f9f53175 | 8c5a09dee942 | 1 | 0/4/0 | 2/3/1 | 0 | PASS |
| 3179 | 249a4a60e8ae | 7f451184ac81 | 1 | 0/4/0 | 2/3/1 | 0 | PASS |
| 3343 | 44893a897a96 | 661bfdc2443c | 1 | 0/2/0 | 2/3/1 | 0 | PASS |

All 14 source root signatures have SHA256
`030ea7831db8493ad9b03f05ae54aa8d70e27211b65a93f9c9e155a925110a7c`.
This is source-byte equality, not proof that all per-draw descriptor contents
or resources are equal or safe to share.

## Priority Sources

### Source 647: Family 40

Processed first as requested.

| Stage | Original SHA256 | Final Candidate SHA256 |
|---|---|---|
| VS | fe53e834aca0d9dacef4628fa10d2a6bbb698b02498673de99562733dd2904d1 | e23e6273d3959d492bd2a76a89d437e20b5c025217712e3c28f99204c1954a22 |
| PS | 8c5a09dee942322418112778412c6e08c43e1342f5bee642acdee6ff041e6cb5 | df9940ff7323744ba62908eb610fc21f3dfb72be0fff970f04205a7e0ac844b0 |

The VS camera is `b1, space0`, logical 848 bytes. MAIN-first conversion selects
two 1024-byte slots in a 2048-byte binding using `SV_ViewID xor 1`.
Its consumed inputs are POSITION0, TEXCOORD0, NORMAL0, TANGENT0, and the three
INSTANCE_TRANSFORM rows. COLOR0 and TEXCOORD1 are not consumed by this VS.

The PS has no camera block, but consumes SV_Position and SV_IsFrontFace; it is
not automatically eye-independent. Its resources are the bindless space1 SRV
array, t67, b7, b0, b4, and s0. The IR contains five sample calls, one texture
load, and two discard calls. No wave or UAV operation was identified.
Material, sampler, all reachable texture/view contents, dither controls, and
per-eye visibility still require the live proof planned by the parent task.

The signature helper adds the unused clip-distance input and moves the existing
PS front-face input from register6 to7, remapping its input instruction ID.
The helper verifies that no non-interface PS instruction changes.

### Source 1777: Stage 9

Processed second, matched by its exact shader pair, not a historical PSO address.

| Stage | Original SHA256 | Final Candidate SHA256 |
|---|---|---|
| VS | d13385d2f6f7206586c8927d9df03e9b2c6b83adb27e6d23c7589f7f496e31aa | 35534cdd1f4ee962cf050ad91768db1ff659ef9790d4796df4b31ed79b2fddf6 |
| PS | 9a4947a3ec0eb3d4605d88b663bb27a0bc809fe8a3d91c4ae67886bd77148db8 | a44f7f9eec64e8e0d586f473a978329a352f21e66454a8774540191c32e1618d |

Both stages use camera b1. The PS reads camera words144,145,146. Its nine SRV
declarations, four CBVs, and three samplers remain unchanged by the camera
patch. It contains one `waveActiveBit.i32` call, plus four sample calls, twenty
SampleGrad calls, and one SampleLevel call. These are static IR call counts,
not execution counts or GPU timing estimates. The wave operation remains intact;
its regrouping behavior needs a separate paired-render audit.

## Classification Limits

- All observed camera loads are statically indexed. None uses camera word147.
- No original shader already uses ViewID. No UAV binding or quad operation was
  found in this source set.
- Explicit derivative unary opcodes83..86 are counted separately in the audit.
  Implicit derivatives also occur through pixel-shader sample operations; an
  empty explicit-derivative count does not establish derivative independence.
- Camera absence is not eye independence. Screen coordinates, texture contents,
  non-camera constants, and other inputs can remain eye-dependent.
- Sources1338/1339/1368 use five VS SRV declarations and three VS samplers;
  sources2166/2186 use a VS SRV and BONEINDEX/INSTANCE_SKINNING_DATA inputs.
  Sources3148/3179 consume vehicle-damage position/normal inputs. None gains a
  geometry or lifetime exemption merely because its bytecode validates.
- The generic batch candidates use the helper's default eye order. The final
  `package/native-camera-candidates.json` references the MAIN-first variants;
  do not substitute generic `batch/cache/*/stereo.dxil` files at runtime.
- CPU normalization is not native graphics-pipeline creation or ViewID pipeline
  validation. The parent owns those checks, target ordering, live input receipts,
  per-eye fallback behavior, and every GPU/game test.

## Outputs

- `source-identities.json`: 14 exact source snapshots and capture provenance.
- `input-index.json`: adapter for the batch helper; its `pPipelineState` field
  deliberately contains source snapshot IDs, not historical pointer identities.
- `batch/manifest.json`: 23 shader classifications and content-addressed cache.
- `native-camera-cache/`: hash-verified MAIN-first stage conversion cache.
- `linked/<source-id>/`: pair-specific normalized PS IR and validated blobs.
- `coverage.json`: complete 14-record source/output/requirements/failure coverage.
- `package/native-camera-candidates.json`: 14 final CPU candidate pairs.
- `package/*.dxil`: 35 content-addressed blobs, including intermediate unlinked
  PS variants. The candidate JSON identifies the final23 unique referenced blobs.
- `commands-cold.json`, `commands.json`: exact cold/warm tool commands and outputs.
- `summary-cold.json`, `summary.json`, `toolchain.json`: results and fingerprints.

The package is not a deployable family manifest. Every candidate explicitly has
`runtimeEligible: false`; no source pipeline or command-list routing changed.
