include(ExternalProject)
set(CVR_FIDELITYFX_ROOT "${CMAKE_BINARY_DIR}/fidelityfx-native" CACHE PATH "Pinned FidelityFX SDK source checkout")
if(NOT EXISTS "${CVR_FIDELITYFX_ROOT}/sdk/CMakeLists.txt")
    FetchContent_Declare(cvr_framegen_fidelityfx
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
        GIT_TAG c6efa6bf7f2027b3ec94f28578bb5965eabb9e55
        SOURCE_DIR "${CVR_FIDELITYFX_ROOT}")
    FetchContent_GetProperties(cvr_framegen_fidelityfx)
    if(NOT cvr_framegen_fidelityfx_POPULATED)
        FetchContent_Populate(cvr_framegen_fidelityfx)
    endif()
endif()
set(cvr_ffx_libs)
foreach(component backend_dx12 frameinterpolation opticalflow)
    list(APPEND cvr_ffx_libs "${CVR_FIDELITYFX_ROOT}/sdk/bin/ffx_sdk/ffx_${component}_x64.lib")
endforeach()
ExternalProject_Add(cvr_framegen_sdk
    SOURCE_DIR "${CVR_FIDELITYFX_ROOT}/sdk"
    BINARY_DIR "${CMAKE_BINARY_DIR}/fidelityfx-native-build"
    DOWNLOAD_COMMAND ""
    CMAKE_ARGS -DFFX_ALL=OFF -DFFX_OF=ON -DFFX_FI=ON
        -DFFX_API_BACKEND=DX12_X64 -DFFX_BUILD_AS_DLL=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --parallel 4
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${cvr_ffx_libs})
add_dependencies(cyberpunkvrport_stereo cvr_framegen_sdk)
target_include_directories(cyberpunkvrport_stereo PRIVATE
    "${CVR_FIDELITYFX_ROOT}/sdk/include" "${CMAKE_SOURCE_DIR}/externals/nvidia-optical-flow")
target_link_libraries(cyberpunkvrport_stereo PRIVATE ${cvr_ffx_libs})
