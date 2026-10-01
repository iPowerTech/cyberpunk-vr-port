cmake_minimum_required(VERSION 3.24)
project(NativeFramegenTests LANGUAGES CXX)
enable_testing()
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(sdk "${root}/build/fidelityfx-native/sdk")
add_executable(framegen_tests main.cpp
    ${root}/src/Framegen/InputCopy.cpp
    ${root}/src/Framegen/Framegen.cpp ${root}/src/Framegen/Generator.cpp
    ${root}/src/Framegen/Presenter.cpp ${root}/src/Framegen/StatsOverlay.cpp ${root}/src/Framegen/GpuTimer.cpp
    ${root}/src/Framegen/HardwareTelemetry.cpp)
target_compile_features(framegen_tests PRIVATE cxx_std_20)
target_compile_definitions(framegen_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D12)
target_include_directories(framegen_tests PRIVATE ${root}/include ${root}/build/_deps/openxr-src/include
    ${sdk}/include ${root}/externals/nvidia-optical-flow)
target_compile_options(framegen_tests PRIVATE /Zi)
target_link_options(framegen_tests PRIVATE /DEBUG)
target_link_libraries(framegen_tests PRIVATE d3d12 dxgi d3dcompiler dbghelp psapi advapi32
    ${sdk}/bin/ffx_sdk/ffx_frameinterpolation_x64.lib
    ${sdk}/bin/ffx_sdk/ffx_opticalflow_x64.lib
    ${sdk}/bin/ffx_sdk/ffx_backend_dx12_x64.lib)
foreach(case policy cadence settings pacing metrics metrics_disabled debug_gate history gpu_fidelityfx gpu_fidelityfx_rgba16 input_copy gpu_fidelityfx_flow overlay overlay_follow planar_depth gpu_timer hardware)
    add_test(NAME framegen_${case} COMMAND framegen_tests ${case})
endforeach()
# Hardware NVIDIA OFA is exercised explicitly, never silently replaced with WARP.
