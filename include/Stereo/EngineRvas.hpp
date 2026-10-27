#pragma once

#include <cstdint>

// ================================================================================================
// Verified offsets into Cyberpunk2077.exe.
//
// One table, because that is how they are checked. Every one of these was found by disassembly and
// confirmed against a running build; when the game is patched they are the first thing to re-verify,
// and doing that against a list is possible where doing it against forty scattered constants is not.
//
// They are also why the detour registry logs in ASCENDING RVA: the log then reads in the same order
// as the image.
// ================================================================================================

namespace cvr {
namespace detail {

constexpr uintptr_t CALLER1_RVA = 0x292A54;  // sub_140292A54 RenderFull
constexpr uintptr_t LIGHT_RVA   = 0x29A5B0;  // sub_14029A5B0 RenderLight (M-B2)
constexpr uintptr_t FLUSH_RENDER_SCENE_RVA = 0x293AF4;
constexpr uintptr_t RELEASE_HANDLE_RVA     = 0x1E6680;
constexpr uintptr_t APPEND_TYPEA_VIEW_RVA  = 0xD6E480;
constexpr uintptr_t VIEW_CONTEXT_ALLOC_RVA = 0x810818;  // sub_140810818: build view-tail owner X (own manager)
constexpr uintptr_t VIEW_ITEM_VTABLE_RVA   = 0x2AC8688;
constexpr uintptr_t CAMERA_WRITE_RVA       = 0x788A9C;
constexpr uintptr_t FRAME_GATE_RVA         = 0x291748;
constexpr uintptr_t VIEW_FINALIZE_RVA      = 0x29C81C;
constexpr uintptr_t FG_BUILD_RVA           = 0xAA3904;
constexpr uintptr_t TYPE_B_SUBMIT_RVA      = 0x293568;
constexpr uintptr_t VIEW_ITEM_FACTORY_RVA  = 0x293C7C;
constexpr uintptr_t JOB_CONTEXT_INIT_RVA   = 0x218B34;
constexpr uintptr_t JOB_CONTEXT_LINK_RVA   = 0x142838;
constexpr uintptr_t JOB_CONTEXT_ADVANCE_RVA = 0x141B10;
constexpr uintptr_t JOB_CONTEXT_DESTROY_RVA = 0x142F88;
constexpr uintptr_t RUN_RENDER_NODES_RVA   = 0x219730;
constexpr uintptr_t RUN_RENDER_MANAGER_LOAD_RVA = 0x2197A4;
constexpr uintptr_t RUN_NODE_BATCH_SUBMIT_RVA = 0xA9BA28;
constexpr uintptr_t RUN_NODE_BATCH_WORK_RVA   = 0xAC4A04;
constexpr uintptr_t GRAPH_REQUEST_BUILD_RVA = 0x36FCD0;
constexpr uintptr_t GRAPH_CONTEXT_PREPARE_RVA = 0x79ACA0;
constexpr uintptr_t GRAPH_CACHE_LOOKUP_RVA = 0x983C80;
constexpr uintptr_t PREPARE_COLLECTOR_WORK_RVA = 0x79B03C;
constexpr uintptr_t CAMERA_RESOURCE_SCOPE_WORK_RVA = 0xC992DC;
constexpr uintptr_t FRAME_BUILD_MARKER_RVA = 0x244AE0;
constexpr uintptr_t SYNCHRONIZE_NODE_WORK_RVA = 0x58DA9C;
constexpr uintptr_t PREPARE_SCENE_NODE_WORK_RVA = 0x784ABC;
constexpr uintptr_t GRAPH_CONTEXT_RESET_RVA = 0x79C05C;
constexpr uintptr_t GRAPH_CONTEXT_OWNER_MOVE_RVA = 0x79CDB8;
constexpr uintptr_t NODE_DISPATCH_RVA = 0x1EC404;
constexpr uintptr_t COPY_TO_TEXTURE_WORK_RVA = 0x377B58;
constexpr uintptr_t RENDER_FINAL2D_WORK_RVA = 0x209FF0;
constexpr uintptr_t DECLARE_FINAL_ONLY_WORK_RVA   = 0x1EE4A0; // DeclareCommonResourceAllocs_FinalOnly
// THE FIVE READERS OF viewData+0x168, found by walking the 68 callers of the viewData accessor
// sub_1401ED930 and keeping the ones that touch +0x168. Measured, not taken from a note: the
// declaration pass DeclareCommonResourceAllocs_FinalOnly (0x1EE4A0) is NOT among them, which is
// why lending to it did nothing.
constexpr uintptr_t COMPOSITION_WORK_RVA          = 0x1F8928; // CRenderNode_CompositionPostProcess
constexpr uintptr_t RT_DECLARE_WORK_RVA           = 0xC66188; // names RT_viewDepthTexture[%d] and
                                                              // RT_localShadowTexture[%d] -- the
                                                              // render-target declarations
constexpr uintptr_t GATE_READER_770268_RVA        = 0x770268; // the fifth reader, unnamed
constexpr uintptr_t COMP_ASSIGN_RVA               = 0x201A68; // the composition-state assign
constexpr uintptr_t COMP_ASSIGN_FRAME_RET_RVA     = 0x29AA53; // return address of that assign in the game
                                                              // frame sub_140292A54 (menus, loading)
constexpr uintptr_t COMP_ASSIGN_VIEW_RET_RVA      = 0x29CEB1; // ...and in the view fill sub_14029CDD8 (in the world)
constexpr uintptr_t HANDLE_COPY_RVA               = 0xACE86C; // copy of a refcounted {object, count} handle:
                                                              // takes a count on the source, releases the target
constexpr uintptr_t EXTRACTION_FINAL_COLOR_WORK_RVA = 0x209CD4; // ExtractionFinalColor
constexpr uintptr_t CLEAR_FINAL_COLOR_WORK_RVA    = 0x209DA0; // ClearFinalColorTarget
constexpr uintptr_t GRAPH_REQUEST_REGISTER_RVA = 0x2906A28;
constexpr uintptr_t GRAPH_REQUEST_ALLOC_RVA = 0x290654C;
constexpr uintptr_t GRAPH_REQUEST_POPULATE_RVA = 0x29067B0;
constexpr uintptr_t GRAPH_MANAGER_ALLOC_RVA = 0x2926550;
constexpr uintptr_t GRAPH_MANAGER_CTOR_RVA = 0x87B164;
constexpr uintptr_t TONEMAP_WORK_RVA = 0x768510;
constexpr uintptr_t RESOLVE_QUERY_RVA = 0x1F3D20;
constexpr uintptr_t DECLARE_RVA = 0x1F0F80;   // sub_1401F0F80
constexpr uintptr_t TYPE_A_JOB_VT_RVA      = 0x2ABC878;
constexpr uintptr_t TYPE_B_JOB_VT_RVA      = 0x2AC8668;
constexpr uintptr_t SPEC_EMPTY_INIT_RVA    = 0x2933C4;
constexpr uintptr_t SPEC_COPY_INIT_RVA     = 0x24EB78;
constexpr uintptr_t SPEC_EMPTY_DTOR_RVA    = 0x25214C;
constexpr uintptr_t BUILD_MATE_INIT_RVA    = 0x29112C;
constexpr uintptr_t BUILD_MATE_FILL_RVA    = 0x252034;
constexpr uintptr_t VIEW_PRODUCER_RVA      = 0x293978;
constexpr uintptr_t BUILD_MATE_DTOR_RVA    = 0x28CF20;
constexpr uintptr_t VIEW_SPEC_DTOR_RVA     = 0x28BD2C;
constexpr uintptr_t SPEC_FINAL_BIND_RVA    = 0x28C298;
constexpr uintptr_t CAMW_RVA = 0x788A9C;
constexpr uintptr_t CLOUDS_NODE_RVA = 0x61B5B4;   // CRenderNode_RenderVolumetricClouds
constexpr uintptr_t DESC_HEAP_SIZE_MOV_RVA = 0x91D64A;
// THE ONLY PLACE THE ENGINE SORTS A DRAW ITEM BY RENDERING PLANE. sub_14023A938 reads the item's
// flags dword, tests bit 5, and picks the per-view parameter block at view+0x70 + index*0x3A0 --
// index 0 for the scene, index 2 for the first-person weapon plane. The multiply by 0x3A0 occurs
// exactly ONCE in the whole binary, so this is the whole routing mechanism, not one of several:
//
//     14023AC04  A8 20     test al, 20h          ; bit 5 = weapon plane
//     14023AC06  74 1E     jz short 14023AC26    ; not set -> index 0 (scene)
//     14023AC08            lea rdx, WeaponPlane  ; set     -> is the category on for this view
//     14023AC1F            mov eax, 2            ;            yes -> index 2 (weapon plane)
//
// Turning the `jz` into an unconditional `jmp` sends EVERY item down the scene branch, which is
// what "draw the first-person plane with the scene's parameters" means at the lowest level: no
// asset edits, no per-item attach argument, no equipment-state re-assertion to fight.
constexpr uintptr_t PLANE_ROUTE_BRANCH_RVA = 0x23AC06;

// THE PER-OBJECT PLANE FILTER, found 2026-09-02 by diffing one object's render proxy between the two
// planes: the proxy carries the raw ERenderingPlane at byte +0x9D (0 scene, 2 weapon), and this is
// the collection that drops everything whose plane is not the one being gathered.
//
//     0x75AB94  mov rcx, [rsi+8]              ; the proxy
//     0x75AB98  cmp byte [rcx+0x9D], r12b     ; its plane against the one being gathered
//     0x75AB9F  jnz ...                       ; different -> the object is skipped entirely
//
// It sits next to the highlight walk (0x769DB8 and its predicate 0x76B9D8), which is what the
// hardware breakpoint on a stored highlight record led to. Turning the jnz into a six-byte nop lets
// objects of every plane through -- the experiment the live session could not finish, because this
// game dies at a debugger pause (three crashes, all at the same int3 stub, two of them with no patch
// applied at all).
constexpr uintptr_t PLANE_FILTER_JNZ_RVA = 0x75AB9F;

// WHERE THE PLANE BECOMES A DRAW-LIST INDEX -- the whole mechanism, found 2026-09-02 by following the
// one byte that differs between an object's two proxies (+0x9D, the raw ERenderingPlane):
//
//     0x623139  mov  r8b, [proxy+0x9D]           ; the plane
//     0x623140  call sub_1401E7D88  ->  sub_1401E7DEC(a1, pass, a2, plane)
//                   v7 = plane + 10 * pass;      ; THE BUCKET NUMBER
//                   v8 = a1 + 448 * v7;          ; the bucket
//                   v9 = *(v8 + 8*a2 + 224);     ; the list the item is appended to
//
// So the scene goes into bucket 0 + 10*pass and the first-person plane into 2 + 10*pass: two separate
// lists. Nothing anywhere compares a plane and drops a highlighted object -- which is why three
// separate "gate" patches all did nothing. The item is simply put on a different pile, and the
// highlight gather walks the scene pile.
//
// Zeroing the plane right here sends EVERY item to the scene bucket, globally, in one instruction.
constexpr uintptr_t PLANE_BUCKET_LOAD_RVA = 0x623139;
constexpr uintptr_t RTT_VIEWCREATE_RVA = 0x4FBAFC;   // sub_1404FBAFC
constexpr uintptr_t VIEW_PARAMS_ENTRY_RVA = 0x3BB0A8;  // sub_1403BB0A8, applies ONE override
                                                       // entry to a view; the outer pass is
                                                       // what erases, this does not
constexpr uintptr_t VIEW_PARAMS_APPLY_RVA = 0x3BAFFC;  // sub_1403BAFFC, the per-view parameter
                                                       // producer -- writes MAIN's viewData+0x798
constexpr uintptr_t VIEWDATA_COPY_RVA  = 0x294C94;   // sub_140294C94, carries the struct in
constexpr uintptr_t RTT_HOST_VTABLE_RVA = 0x307BFD0; // entRenderToTextureCameraComponent host vtable
constexpr uintptr_t RESIZE_DYNTEX_RVA = 0x291A4D4;   // sub_14291A4D4
constexpr uintptr_t FULL_BUILD_RVA = 0x1D43040;   // sub_141D43040
constexpr uintptr_t INCR_BUILD_RVA = 0x1D475B0;   // sub_141D475B0
constexpr uintptr_t EXTRACTION_ADDER_RVA   = 0x982B5C;   // sub_140982B5C (ExtractionFinalColor)
constexpr uintptr_t DECLARE_ADDER_RVA      = 0x982974;   // sub_140982974 (DeclareCommonResourceAllocs_FinalOnly)
constexpr uintptr_t CLEAR_ADDER_RVA        = 0x982A40;   // sub_140982A40 (ClearFinalColorTarget)
constexpr uintptr_t DRAWCOMP_ADDER_RVA     = 0x982AA0;   // sub_140982AA0 (DrawComposition)
constexpr uintptr_t COMPOSITION_ADDER_RVA  = 0x982B00;   // sub_140982B00 (CompositionPostProcess)
constexpr uintptr_t FSVIDEO_ADDER_RVA      = 0x986B30;   // sub_140986B30 (FullscreenVideo)
constexpr uintptr_t ADD_NAMED_PASS_RVA     = 0x9853B4;   // sub_1409853B4 (add pass by name)
constexpr uintptr_t SCENE_FULL_LO_RVA = 0x1D43040, SCENE_FULL_HI_RVA = 0x1D43040 + 0x456F;
constexpr uintptr_t SCENE_INCR_LO_RVA = 0x1D475B0, SCENE_INCR_HI_RVA = 0x1D475B0 + 0xA33;
constexpr uintptr_t SCENE_RTT_LO_RVA  = 0x1D47FF0, SCENE_RTT_HI_RVA  = 0x1D47FF0 + 0x12E7;
constexpr uintptr_t FLAG_COMPUTE_RVA = 0x1D49540;   // sub_141D49540
// The water-state assignment, sub_14036FA20(dst, src). Found by a hardware write breakpoint on the
// second view's +0x1800 while the port's own copy was in place: this is what overwrites it.
constexpr uintptr_t WATER_ASSIGN_RVA = 0x36FA20;    // sub_14036FA20
constexpr uintptr_t HANDLE_ASSIGN_RVA = 0x28DAE4;   // sub_1407CDAE4(dst, src)
constexpr uintptr_t RECT_COMPUTE_RVA = 0x4E3EB4;   // sub_1404E3EB4
constexpr uintptr_t SKY_WORK_RVA = 0x7818F8;   // sub_1407818F8, the body behind feature 35
constexpr uintptr_t DISTANT_RENDER_RVA  = 0x373998;   // sub_140373998
constexpr uintptr_t DISTANT_PREPARE_RVA = 0x374AD8;   // sub_140374AD8
constexpr uintptr_t CLOUD_CB_FILL_RVA = 0x784654;   // sub_140784654
constexpr uintptr_t LOCAL_SHADOW_RVA = 0xAD5770;   // sub_140AD5770 RenderLocalShadowMaps
constexpr uintptr_t REFLECTION_PROBES_RVA = 0x77E610;   // sub_14077E610 ReflectionProbes
constexpr uintptr_t SKY_SCATTERING_RVA = 0x7818B0;   // sub_1407818B0 RenderSkyScattering node work
constexpr uintptr_t VIEW_RECT_TEST_RVA = 0x1E4B60;   // sub_1401E4B60(p) = p[0]>=p[2] || p[1]>=p[3]
constexpr uintptr_t CB_UPLOAD_RVA   = 0x1EE3CC;
// The BUFFER uploader -- (descriptor index, size, src) rather than (size, src). The frame-global
// constant blocks travel this way and NOT through CB_UPLOAD above, which is why a 480-byte probe on
// the latter never fired once; the constant census named these rows "buf:".
constexpr uintptr_t BUF_UPLOAD_RVA  = 0x1F088C;   // sub_1401F088C(idx, size, src)
constexpr uintptr_t GRADING_COMPOSE_RVA = 0x77B538;
constexpr uintptr_t TONEMAP_LUT_RVA = 0xEFC110;   // sub_140EFC110 GenerateTonemappingLUT
constexpr uintptr_t GI_NODE_RVA         = 0x77E664;   // sub_14077E664 GlobalIllumination node
constexpr uintptr_t GI_EARLYCHK_RVA     = 0x1E4B60;   // sub_1401E4B60(ctx+0x14) early-out
// The foliage wind-impulse pass -- what bends a bush you walk into. Written for ONE view per frame,
// and which view claims it decides whether the vegetation is a frame stale; see the long note at
// Detour_WindImpulseNode.
constexpr uintptr_t WIND_IMPULSE_NODE_RVA = 0x6EAEDC; // sub_1406EAEDC WindImpulseVolumeUpdate work
constexpr uintptr_t GI_APPLY_RVA        = 0x77E74C;   // sub_14077E74C(applyMgr, a2) GI apply
constexpr uintptr_t RENDERER_GLOBAL_RVA = 0x3427C00;  // qword_143427C00
// The sun's shadow cascades. Each view renders its OWN (measured in a capture: two separated runs of
// cascade draws per frame), so what each one draws into its map is worth counting per view.
constexpr uintptr_t CASCADE_NODE_RVA = 0x153844;  // sub_140153844 RenderShadowCascade work
// ...and the clear that precedes it. Both belong to whichever view is rendering, and the atlas they touch
// is ONE shared resource (proven when the view that skipped its build sampled populated-but-stale content
// rather than nothing), so cutting the pair for one view makes that view reuse the other's atlas.
constexpr uintptr_t CASCADE_CLEAR_NODE_RVA = 0x1D59B40;  // sub_141D59B40 ClearShadowCascades work
// The node that uploads the lighting globals -- where the cascade SAMPLING matrices belong, as opposed to the
// cascade record the rasteriser builds its own constants from.
constexpr uintptr_t LIGHTING_GLOBALS_NODE_RVA = 0xBB8D40;  // CRenderNode_BindLightingGlobalConstants
// ...and it is only a BIND: 84 bytes, one callee, pointing the root signature at a buffer somebody else
// filled. It uploads nothing, which is why a probe aimed at it printed nothing at all.
// The volumetric fog pass, whose froxel grid is what the blocky sun shafts in a room actually are.
constexpr uintptr_t VOLUMETRIC_FOG_NODE_RVA = 0x61C3BC;  // CRenderNode_VolumetricFog work
// The cone ambient occlusion: five compute shaders over one block of its own (b3).
constexpr uintptr_t CONE_AO_NODE_RVA = 0x61EE78;  // DrawConeAO work
// The auto-exposure: luminance image, histogram and adaptation, each dispatch over a small block (b6).
constexpr uintptr_t HISTOGRAM_UPDATE_NODE_RVA = 0x774CF8;  // HistogramUpdate work
constexpr uintptr_t SSR_NODE_RVA = 0x157B24;  // ScreenSpaceReflections work
constexpr uintptr_t PS_B6_UPLOAD_RVA = 0xAD2254; // sub_140AD2254(size, src): pixel-stage b6 constant block
constexpr uintptr_t SSPR_NODE_RVA = 0x78A6B8; // ScreenSpacePlanarReflections work (same CB_SSRRaymarch upload)
// The per-BIT feature test the nodes themselves use: sub_14023AF5C(work_ctx, bit_index). NOT the same
// function as VIEW_FEATURE_CHECK_RVA (sub_14021BE28), which takes a render-mask DESCRIPTOR -- that
// confusion is why a probe hung on the latter never fired for the shadow-mask pass.
constexpr uintptr_t FEATURE_BIT_TEST_RVA = 0x23AF5C;
constexpr uintptr_t RENDER_SHADOWMASK_RVA = 0x786BCC;  // sub_140786BCC RenderShadowmask work
// The global (SpeedTree) wind. Behind the same once-per-frame latch, and measured to upload its
// 416-byte parameter blocks for MAIN only (29638 against 0) -- which is what leaves the second view's
// cascade one wind step behind. See Detour_SpeedTreeWind.
constexpr uintptr_t SPEEDTREE_WIND_RVA = 0xCC4DF4;  // sub_140CC4DF4 AdvanceSpeedTreeWind work
constexpr uintptr_t DOCULLING_RVA = 0xB2BEFC;   // sub_140B2BEFC CRenderNode_DoCulling
constexpr uintptr_t MAIN_CULL_PREP_RVA = 0x62463C; // sub_14062463C scene-system candidate gather
constexpr uintptr_t MAIN_CULL_CTX_INIT_RVA = 0x623FD8; // sub_140623FD8 build shared gather-context
constexpr uintptr_t MAIN_CULL_TEST_RVA = 0x624694; // sub_140624694 -> sub_14014DBC4 tester loop
constexpr uintptr_t VIS_COLLECTOR_RVA = 0x79CB6C;  // sub_14079CB6C tagged candidates -> fresh view output
constexpr uintptr_t FINE_MATERIALIZE_RVA = 0x14DFE8; // sub_14014DFE8 per-drawable fine test/materialize
constexpr uintptr_t VISIBLE_APPEND_RVA = 0x109A44;   // sub_140109A44 append visible drawable ID
constexpr uintptr_t MATERIALIZE_WORKER_RVA = 0x36DDC4; // sub_14036DDC4 worker wrapper
constexpr uintptr_t PREPARE_STAGE_RVA = 0x1D57210;   // sub_141D57210 gather/filter/finalize wrapper
constexpr uintptr_t PREPARE_GATHER_RVA = 0x15375C;    // sub_14015375C flatten worker buckets
constexpr uintptr_t PREPARE_FILTER_RVA = 0x1D57100;   // sub_141D57100 filter/classify descriptors
constexpr uintptr_t PREPARE_FINALIZE_RVA = 0x379568;  // sub_140379568 sort/finalize descriptors
constexpr uintptr_t PREPARE_SORT_A_RVA = 0x37A54C;    // first mode1 index-domain sort
constexpr uintptr_t PREPARE_SORT_B_RVA = 0x37A984;    // second mode1 index-domain sort
constexpr uintptr_t PREPARE_SORT_C_RVA = 0x37ADB4;    // third mode1 index-domain sort
constexpr uintptr_t PREPARE_SORT_FINAL_RVA = 0x45E33C; // final 16-byte descriptor sort
constexpr uintptr_t DRAWCOMP_RVA = 0x20A264;   // sub_14020A264
constexpr uintptr_t COMP_ELEM_KEYS_RVA = 0x3C6970;   // sub_1403C6970, the element's draw keys
constexpr uintptr_t COMP_ELEM_RECT_RVA = 0x3C6F5C;   // sub_1403C6F5C, the element's scissor rect
constexpr uintptr_t LIGHTBUFFERS_RVA = 0x77D308;   // sub_14077D308 RenderLightBuffers
constexpr uintptr_t SL_CONSTANTS_RVA = 0x788A9C;   // sub_140788A9C
constexpr uintptr_t DLSS_EVAL_RVA  = 0x1D4FDC0;   // sub_141D4FDC0 slSetTag+slEvaluateFeature
constexpr uintptr_t DLSS_CONST_RVA = 0x78933C;    // sub_14078933C slSetConstants
constexpr uintptr_t READBACK_NODE_RVA = 0x292DD50;   // sub_14292DD50
constexpr uintptr_t APPLYDLSS_WORK_RVA = 0x37D5C4;   // sub_14037D5C4 (ApplyDLSS node work)
constexpr uintptr_t FFX_API_PROVIDERS_RVA = 0x1D4C3D0;   // sub_141D4C3D0: FidelityFX API upscaler versions (FSR 2/3/4)
constexpr uintptr_t FFX_API_LOADER_RVA = 0x1D4DEA0;   // sub_141D4DEA0: GetProcAddress of amd_fidelityfx_dx12 (FSR 3.1+)
constexpr uintptr_t GETTER_RVA = 0x1ED8E4;   // sub_1401ED8E4 view-dims getter entry
constexpr uintptr_t RENDER_RES_RVA = 0x4E42A0;   // sub_1404E42A0 per-view DLSS render-res compute

}  // namespace detail
}  // namespace cvr
