cmake_minimum_required(VERSION 3.24)
project(GameSettingsTests LANGUAGES CXX)
enable_testing()
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(json "${root}/build/_deps/openxr-src/src/external/jsoncpp")
add_executable(game_settings_tests main.cpp ${root}/src/Core/GameSettings.cpp
    ${json}/src/lib_json/json_reader.cpp ${json}/src/lib_json/json_value.cpp ${json}/src/lib_json/json_writer.cpp)
target_include_directories(game_settings_tests PRIVATE ${root}/include ${json}/include)
target_compile_features(game_settings_tests PRIVATE cxx_std_20)
target_compile_definitions(game_settings_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME settings_merge_and_atomic_install COMMAND game_settings_tests
    "${root}/mods/config/UserSettings.json" "${CMAKE_CURRENT_BINARY_DIR}")
