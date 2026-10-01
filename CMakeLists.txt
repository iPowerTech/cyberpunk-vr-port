cmake_minimum_required(VERSION 3.24)

project(CyberpunkVRPort LANGUAGES CXX C)

include(FetchContent)
FetchContent_Declare(
  openxr
  GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
  GIT_TAG release-1.0.34
)
FetchContent_MakeAvailable(openxr)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.90.9
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

# THE FIDELITYFX SDK IS PULLED BY cmake/NativeFramegen.cmake NOW, not here.
#
# This fork used to fetch it at tag fsr3-v3.0.4 and embed it with add_subdirectory + FFX_API_CUSTOM,
# for its own FSR3 frame generator (now src/FsrFramegen). Upstream 0.1.7's native frame generator
# pins a LATER revision and builds the SDK out-of-tree with ExternalProject. Both link the same three
# static libraries -- ffx_frameinterpolation_x64, ffx_backend_dx12_x64, ffx_opticalflow_x64 -- under
# the same symbol names, so two revisions in one DLL is a duplicate-symbol link at best and one
# implementation silently serving both paths at worst. One SDK, and it is theirs while theirs is the
# generator being built; src/FsrFramegen is excluded from the source list below until that is settled.
#
# The pixlib shim went with it: it existed only to satisfy the DX12 backend's unconditional PIX link
# inside OUR add_subdirectory, and ExternalProject builds the SDK standalone where that is resolved.

set(MINHOOK_SOURCES
    externals/minhook/src/buffer.c
    externals/minhook/src/hook.c
    externals/minhook/src/trampoline.c
    externals/minhook/src/hde/hde32.c
    externals/minhook/src/hde/hde64.c
)


add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${MINHOOK_SOURCES}
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)



add_subdirectory(externals/RED4ext.SDK)

# ============================================================================================
# CyberpunkVR_Stereo -- the RED4ext plugin. This is the whole mod.
# ============================================================================================
#
# It used to ship as a dxgi.dll proxy sitting next to the game exe, with DllMain and the seven
# DXGI factory exports forwarding to the system dxgi.dll. None of the code ever needed to BE
# the proxy -- it only needed a device, a queue and Present, and RED4ext hands over all three.
# The proxy is gone: no DllMain, no export forwarders, no LoadLibrary of the real dxgi.dll,
# and no second copy of every hook racing the first if both DLLs were installed by mistake.
#
# ONE PLUGIN, NOT TWO. CyberpunkVR_Hands.dll used to be a separate red4ext plugin, and this comment
# used to say it stayed that way. It does not: the glob below takes ALL of src/, which now includes the
# VRIK pose path (src/Hooks/AnimPose.cpp, src/Anim/, src/Natives/) that used to build into Hands. The
# single DLL registers all 219 natives and installs every hook.
#
# THE OLD CyberpunkVR_Hands.dll MUST BE REMOVED FROM red4ext\plugins. Both binaries register the same
# 219 natives and detour the same addresses, and two plugins detouring one address is the crash that
# reads as a fault at FFFFFFFFFFFFFFFF with no usable stack -- checked, not assumed: the deployed
# Hands.dll contains all 219 native name strings.
#
# The single file is also SMALLER than the two it replaces (2,199,552 against 1,291,776 + 1,425,920),
# because the code they shared is now emitted once.
# THE FILE LIST IS A GLOB, and deliberately so. This tree is mid-restructure: the monoliths are
# being split into one-hook-per-file units under src/Hooks, src/Anim and the rest, and a hand-kept
# list would be edited on every one of those splits and forgotten on one of them. CONFIGURE_DEPENDS
# makes the build re-glob when a file appears, so the usual objection to globbing does not apply.
file(GLOB_RECURSE CPVR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
# This fork's own FSR3 generator stays in the tree but out of the build: it is written against
# FidelityFX fsr3-v3.0.4 and the SDK pinned by cmake/NativeFramegen.cmake is a later revision, so
# the two cannot compile side by side. Drop this filter to build ours instead of upstream's -- and
# put the pin back with it, and delete src/FsrFramegen/FsrFramegenAbsent.cpp.
#
# Only the three implementation files are excluded. FsrFramegenAbsent.cpp stays IN the build: the
# FSR3 generator is wired into four callers (NgxEvalPassthrough, SwapChain, OverlayPanels,
# OpenXRFrameLoop) and dropping the implementation alone leaves nineteen unresolved externals. See
# that file for why the callers are left untouched.
list(FILTER CPVR_SOURCES EXCLUDE REGEX "/FsrFramegen/(FrameGenCapture|FrameGenDilate|FrameGenFsr3)\\.cpp$")

# Keep the folder shape in the IDE -- Hooks/, Runtimes/, Anim/, Utils/ are the reading order.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CPVR_SOURCES})

add_library(cyberpunkvrport_stereo SHARED
    ${CPVR_SOURCES}
    externals/im3d/im3d.cpp
)
# Fetches and builds the pinned FidelityFX SDK, then adds its include dirs and links its three
# static libraries onto the target -- so it has to come after the target exists.
include(cmake/NativeFramegen.cmake)
target_compile_features(cyberpunkvrport_stereo PRIVATE cxx_std_20)
target_compile_definitions(cyberpunkvrport_stereo PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX
    XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D12
    CPVR_RED4EXT_PLUGIN=1
    _CRT_SECURE_NO_WARNINGS=1
)
target_link_libraries(cyberpunkvrport_stereo PRIVATE
    openxr_loader
    imgui
    d3d12.lib
    d3dcompiler
    dxgi.lib        # the SYSTEM import lib -- bare "dxgi" would resolve to our own target
    psapi
    RED4ext::SDK
)
target_include_directories(cyberpunkvrport_stereo PRIVATE
    # ONE root for our own headers. Every internal include reads "Area/Name.hpp", so a header's
    # path states which area owns it -- which is the whole point of the layout. The old set of
    # eleven roots let any header be reached by its bare name from anywhere, and that is precisely
    # what let the monoliths grow: nothing about an include said where the thing belonged.
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # Streamline's public headers, for src/Framegen/Inputs.cpp's <sl_core_types.h>. Headers only --
    # nothing links against Streamline; the game's own sl.interposer.dll is what provides it.
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/streamline/include
    # --- third-party / vendored ---
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/im3d
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/minhook/include
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/minhook/src
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/minhook/src/hde
)
if(MSVC)
    target_compile_options(cyberpunkvrport_stereo PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /EHsc>)
    # SYMBOLS IN THE RELEASE BUILD. A crash landed inside this DLL and there was nothing to read it
    # with: no PDB here, no cdb or windbg on the machine, and dumpbin only opens PE files. Placing
    # CyberpunkVR_Stereo.dll+0x459AF took disassembling the whole image and matching the faulting
    # instruction by hand. That is affordable once.
    #
    # /Zi and /DEBUG do not change optimisation -- /OPT:REF and /OPT:ICF are restored explicitly
    # because /DEBUG turns them off by default, and without them the linker would stop folding and
    # stripping, which WOULD change the binary. The PDB ships nowhere; it stays in the build tree
    # so a dump from this machine resolves instantly and a tester's dump resolves against the build
    # that produced their DLL.
    target_compile_options(cyberpunkvrport_stereo PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(cyberpunkvrport_stereo PRIVATE
        $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()
set_target_properties(cyberpunkvrport_stereo PROPERTIES
    PREFIX ""
    OUTPUT_NAME CyberpunkVR_Stereo
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/red4ext/plugins/CyberpunkVR_Stereo"
)

# ============================================================================================
# Tools. Separate targets, never deployed with the plugin.
# ============================================================================================
# xr_probe_layer -- an OpenXR API layer that records what any VR application asks the runtime for
# and what it submits back. Used to read Quest 3 geometry out of R.E.A.L. VR (a dxgi.dll proxy)
# without touching its internals. Build it explicitly:
#     cmake --build build --config Release --target xr_probe_layer
add_subdirectory(tools/xr_probe)
