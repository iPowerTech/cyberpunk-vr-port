# AMD FidelityFX frame interpolation for the FSR3 frame generator (src/Framegen/Fsr3.cpp), the fork's
# vr_framegen generator: tag fsr3-v3.0.4, the SDK version the game itself ships as
# ffx_frameinterpolation_x64.dll. Our own copy is built so the plugin does not depend on the game's DLLs.
#
# FFX_API_CUSTOM embeds sdk/CMakeLists.txt in this project instead of AMD's standalone solution. FFX_FI
# builds frame interpolation; FFX_FSR is what the DX12 backend checks to compile its shader blobs.
# Optical flow comes with them. Everything else stays off.
FetchContent_Declare(
  fidelityfxsdk
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
  GIT_TAG fsr3-v3.0.4
)
FetchContent_GetProperties(fidelityfxsdk)
if(NOT fidelityfxsdk_POPULATED)
  FetchContent_Populate(fidelityfxsdk)
endif()
set(FFX_API_CUSTOM ON)
set(FFX_API_DX12 ON)
set(FFX_API_VK OFF)
set(FFX_FI ON)
set(FFX_FSR ON)
set(FFX_BUILD_AS_DLL OFF)
set(FFX_AUTO_COMPILE_SHADERS ON)
set(BIN_OUTPUT ${CMAKE_BINARY_DIR}/ffx_sdk)
set(CMAKE_GENERATOR_PLATFORM x64)
# The SDK links its DX12 backend against `pixlib` (PIX markers of AMD's sample framework, not part of
# this build); none of the sources built here call PIX, so an empty target satisfies the link.
add_library(pixlib INTERFACE)
add_subdirectory(${fidelityfxsdk_SOURCE_DIR}/sdk ${CMAKE_BINARY_DIR}/ffx_sdk_build)
target_link_libraries(cyberpunkvrport_stereo PRIVATE
    ffx_frameinterpolation_x64
    ffx_backend_dx12_x64
    ffx_opticalflow_x64)
