cmake_minimum_required(VERSION 3.24)
project(NativeFramegenTests LANGUAGES CXX)
enable_testing()
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(framegen_tests main.cpp
    ${root}/src/Framegen/InputCopy.cpp
    ${root}/src/Render/StereoDepth.cpp
    ${root}/src/Framegen/Framegen.cpp
    ${root}/src/Framegen/Presenter.cpp ${root}/src/Framegen/StatsOverlay.cpp ${root}/src/Framegen/GpuTimer.cpp
    ${root}/src/Framegen/HardwareTelemetry.cpp)
target_compile_features(framegen_tests PRIVATE cxx_std_20)
target_compile_definitions(framegen_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D12)
target_include_directories(framegen_tests PRIVATE ${root}/include ${root}/build/_deps/openxr-src/include)
target_compile_options(framegen_tests PRIVATE /Zi)
target_link_options(framegen_tests PRIVATE /DEBUG)
target_link_libraries(framegen_tests PRIVATE d3d12 dxgi d3dcompiler dbghelp psapi advapi32 windowscodecs ole32)
foreach(case cadence presenter stereo_depth settings metrics metrics_disabled debug_gate history input_copy overlay overlay_follow planar_depth gpu_timer hardware)
    add_test(NAME framegen_${case} COMMAND framegen_tests ${case})
endforeach()
