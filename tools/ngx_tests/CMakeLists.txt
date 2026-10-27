cmake_minimum_required(VERSION 3.24)
project(NgxTests LANGUAGES C CXX)
enable_testing()
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(ngx_tests main.cpp ${root}/src/Hooks/Ngx.cpp
    ${root}/externals/minhook/src/buffer.c
    ${root}/externals/minhook/src/hook.c
    ${root}/externals/minhook/src/trampoline.c
    ${root}/externals/minhook/src/hde/hde64.c)
target_include_directories(ngx_tests PRIVATE ${root}/include ${root}/externals/minhook/include)
target_compile_features(ngx_tests PRIVATE cxx_std_20)
target_compile_definitions(ngx_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ngx_tests PRIVATE d3d12 dxgi)
add_test(NAME ngx_diagnostic_gate COMMAND ngx_tests)
