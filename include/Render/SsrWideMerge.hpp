#pragma once
#include <cstdint>

// Screen-space reflections of the two eyes as ONE wide display. src/Render/SsrWideMerge.cpp has the
// reasoning; tools/asym_tests/asym_ssr_shaders.py (wide_merge) is the shader side of the same contract.
namespace cvr::ssr_merge {
// The trace's CB_SSRRaymarch (b6, node ScreenSpaceReflections) as the engine uploads it: 656 bytes on game 2.5
// (41 rows: the first-person plane's own projection and rotation in rows 16-31; 2.31: 384). The port grows it
// by three rows (41-43; 2.31: 24-26) that only the twins read -- asym_ssr_shaders.py as regen_twins.py LAYOUT
// sets it.
constexpr uint32_t kEngineBlockBytes=656;
constexpr uint32_t kBlockBytes=704;
constexpr uint32_t kPixelSizeRow=32;        // .zw 2 / width, 2 / height (2.31: row 16)
constexpr uint32_t kCameraRow=41;           // xyz camera world position, w kTag
constexpr uint32_t kStoreRow=42;            // x stamp, y width, z height, w store capacity (entries)
constexpr uint32_t kModeRow=43;             // x mode, y depth tolerance (relative)
constexpr uint32_t kTag=0x41734D31u;        // asym_ssr_shaders.py MERGE_MAGIC
constexpr uint32_t kHeaderEntries=52;       // entries before the pixels, asym_ssr_shaders.py HEADER
constexpr uint32_t kEntryBytes=12;          // uint3: colour, half depth | confidence, stamp (asym_ssr_shaders.py STRIDE)
// UAV slots of the stores (asym_ssr_shaders.py OWN_SLOT/OTHER_SLOT). Inside the compute root signature's
// u0+19 and used by no pass of the node: its tile classifier binds u0-u3 after the block is uploaded.
constexpr uint32_t kOwnSlot=16,kOtherSlot=17;
// The traces that share the block and the stores' layout, each with a store pair of its own:
// ScreenSpaceReflections (0x77A100) and ScreenSpacePlanarReflections (0x77D450).
enum Channel : int { kReflections=0, kPlanar=1, kConeAo=2, kShadowMask=3, kExposure=4, kChannels=5 };
// Called with the engine's block in the channel's node. True with the grown block in `out` (kBlockBytes)
// and the channel's two stores bound as u16 (this eye's) and u17 (the other eye's) on the node's command
// context.
bool Prepare(Channel channel,int side,uintptr_t view,const void* src,uint32_t size,uint8_t* out,uint32_t* ship);

// The cone AO's block (node DrawConeAO, CustomConstantBuffer, 240 bytes on the buffer uploader; 2.31: 208)
// after the centre write (Stereo/Grading.cpp): row 11 -- the 4th row of the history matrix in rows 8-11, which
// no pass of the node reads -- becomes x stamp, y kAoTag, z depth tolerance, w strength; 13.z the diagnostic
// mode (2.31: 12.y; 2.5's shaders read 12.xy, 13.xy and 14.xy -- a second pair of tangents and the old 12.x).
constexpr uint32_t kAoBlockBytes=240;
constexpr uint32_t kAoMergeRow=11;
constexpr uint32_t kAoModeRow=13;            // .z, asym_ao_shaders.py MODE_ROW/MODE_COMP as regen_twins.py sets them
constexpr uint32_t kAoCentreRow=12;          // .z the centre, .w kAoCentreTag (Stereo/Grading.cpp)
constexpr uint32_t kAoCentreTag=0x41734331u;
constexpr uint32_t kAoTag=0x41734D41u;       // asym_ao_shaders.py AO_TAG
// Rewrites `block` in place and binds the cone AO stores as u16/u17. False leaves the block as it was.
bool PrepareConeAo(int side,uint8_t* block,uint32_t size);
// The shadow mask's pixel-shader block (node RenderShadowmask, b6 = PIX: the contact-shadow settings, 56
// bytes uploaded as 64 by sub_14041A56C): its padding row 3.z becomes the pass stamp, 3.w kShadowTag | mode.
constexpr uint32_t kShadowBlockBytes=64;
constexpr uint32_t kShadowTag=0x41734D50u;   // asym_shadow_shaders.py SHADOW_TAG (low 4 bits: mode)
// Rewrites `block` in place and binds the shadow-mask stores as u16/u17 for the pixel stage.
bool PrepareShadowMask(int side,uintptr_t view,uint8_t* block,uint32_t size);
// The auto-exposure's environment cells (node HistogramUpdate, asym_expo_shaders.py environment_meter): a
// 12-byte entry per cell, then one per cell for the frame's sums, bound as u16 for the compute stage on the
// node's command context. `debug` as for the other channels ([3] binder refused, [4] allocation failed).
constexpr uint32_t kExposureCells=60;
// `snapshot`: the adaptation's bind, where a CyberpunkVR_DebugExpoCellsRequest is served.
bool BindExposureCells(int side,uint64_t* debug,bool snapshot=false);
// Diagnostic, from every draw/dispatch: snapshots the binding layout in the node CyberpunkVR_ProbeLayoutNode.
void ProbeLayout(uint32_t node);
}
