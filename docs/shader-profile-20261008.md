# Shader profile: where the GPU frame waits (2026-10-08)

Question: can the game's shaders be rewritten (same quality, no FP16) for a real FPS gain?
This is the hardware answer for the current build on an RTX 5070 Ti (GB203, 70 SMs, driver 596.49).

## Capture

Nsight Graphics 2026.3.1 GPU Trace, real-time shader profiler on, 8 frames on the city roof
(-664.98 847.75 75), StageReplay on, FG off, clocks unaltered. Launched through the MCP
(`ngfx_gputrace_launched`, `start_after_ms=180000`, `disable_collect_external_shader_debug_info`,
no `--architecture`; a hotkey/`--architecture` launch died with "CGameEngine: Failed to initialize
viewport"), with `show_launcher=0` in vrport-launcher.ini and a QuickBoot run file
(`requireNativeReady=false`) during the launch; both files were restored afterwards. Readiness:
`build/gpu-audit-20260930/launch_gpu_ready.py --pid`. Trace:
`build/single-pass-20261008/shader-trace/Cyberpunk2077_2026_10_08_22_56_02.ngfx-gputrace`
(frames 13.2-16.3 ms under the profiler vs 12.65 ms normally, about 1.2x).

Decoder: `tools/single_pass_tests/gpu_trace_shaders.py` (+ test), run with the Nsight MCP venv.
It attributes the 3.26 M hardware PC samples to shader code blocks, links each block to its PSO and
draw/dispatch calls through the ELF `ShaderModuleHash` = PipelineSets `apphash`, and recovers the
blob XOR mask from known plaintext (native DXBC in `build/gpu-audit-20260930/native-shaders`).
SASS: CUDA 13.3 `nvdisasm -b SM120 -hex` on the deobfuscated instruction blobs.

### Stall codes

The trace stores a hardware reason code per sample. CUPTI only reports its own alphabetical metric
indices, so the codes were identified from the SASS instruction each one is sampled on:

| code | reason | evidence |
|---|---|---|
| 8 | long_scoreboard | consumer instructions right after `DEPBAR`, i.e. waiting for TEX/LDG |
| 19 | drain | 99% on `EXIT`, preceded by `SUST`/`TLD`/`TEX` |
| 12 | tex_throttle | 81% on `TEX`, plus `TLD`/`TLD4`/`TXD` |
| 14 | lg_throttle | 100% on `LDG` |
| 9 | short_scoreboard | after `LDCU`/`MUFU` |
| 15 | mio_throttle | on `MUFU`/`F2I`/`FRND` |
| 2 | barrier | right after `BAR` |
| 6 | wait | fixed-latency ALU dependencies |
| 16 | math_pipe_throttle (probable) | on FFMA/FMUL/FADD |
| 17 / 18 | selected / not_selected, order unproven | distributed like the instruction mix |

## Results

Frame-wide (Nsight per-frame metrics, median): SM throughput 25% of peak, FMA pipes 12-15%, ALU-heavy
8%, XU 12%, L2 25%, L1TEX 15%. Average active warps are about 6 of 48 per SM: for most of the frame
the SMs hold few warps and mostly wait.

33% of the samples fall outside every game shader block (driver/SDK code, mostly DLSS). Inside game
shaders, long_scoreboard (waiting for memory) is about 40%.

| share | shader | regs | warps/SM | calls/frame, range | top stalls | pass |
|---|---|---|---|---|---|---|
| 13.0% | CS ff61509fc81de04c | 54 | 36 | 2 ExecuteIndirect, ~555 us each | long_scoreboard 85% | SSR ray march |
| 5.8% | PS fcff9bc721a6dc9e | 127 | 16 | 2 fullscreen draws, ~250 us | long_scoreboard 31%, wait 19% | after SSR, likely RenderLightsIntegrate |
| 5.0% | PS 289204d64c902e66 | 64 | 32 | ~2256 draws | long_scoreboard 68% | G-buffer material |
| 2.6% | CS 68cdf9eed55e8015 | 72 | 24 | 4 ExecuteIndirect | long_scoreboard 31% | SSR (before the march) |
| 2.2% | CS c6ed5cc7a88b95d4 | 73 | 24 | 2, async queue | selected/not_selected 55% | async compute |

SSR identification: per eye the sequence Dispatch 42e4ed65 (65 us) + 2x ExecuteIndirect 68cdf9ee
(136 us) + ExecuteIndirect ff61509f (556 us) sums to 756 us, the profiler-node SSR time (0.631 ms)
times the trace overhead. The ray march has no shared memory; occupancy is limited only by its 54
registers.

## Verdict on shader rewriting

- ALU rewrites (division to `rcp`, `fma`, Horner, `dot`, variable reuse) cannot move the frame: the
  game's DXIL is already `fdiv fast`/`fmul fast`, the driver fuses and reorders, and the FMA pipes run
  at 12-15%. Fewer ALU instructions do not shorten time spent waiting for memory.
- The 2026-09-30 exact-output DXIL rewrites (terrain lookup, vector loads, loop exit, mask pruning,
  AO tiles) gave no gain; the profile explains why.
- The one shader-level lever that matches this profile is occupancy of latency-bound shaders. The SSR
  ray march at 54 registers runs 36 of 48 warps; at <= 40 registers it would run 48. For an 85%
  long_scoreboard loop that can be worth roughly 10-25% of its ~0.47 ms per eye, i.e. about
  0.1-0.25 ms per frame, IF the driver actually allocates fewer registers for a rewritten DXIL (D3D12
  has no register cap). The 127-register PS is the second candidate, harder to shrink.
- Most of the frame is not shader-bound at all (6 of 48 warps on average): dependent passes, small
  dispatches, fixed-function and DLSS phases.
