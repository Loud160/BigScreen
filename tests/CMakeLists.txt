# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.20)
project(BigScreenCoreTests LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()
add_executable(bigscreen-core-tests CoreLogicTests.cpp)
target_compile_features(bigscreen-core-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-core-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
add_test(NAME bigscreen-core-tests COMMAND bigscreen-core-tests)

add_executable(
    bigscreen-downloader-activation-tests
    DownloaderActivationTests.cpp)
target_compile_features(bigscreen-downloader-activation-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-downloader-activation-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
add_test(
    NAME bigscreen-downloader-activation-tests
    COMMAND bigscreen-downloader-activation-tests)
if(NOT MSVC)
    target_compile_options(bigscreen-downloader-activation-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(
    bigscreen-rotating-backup-store-tests
    RotatingBackupStoreTests.cpp)
target_compile_features(bigscreen-rotating-backup-store-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-rotating-backup-store-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
add_test(
    NAME bigscreen-rotating-backup-store-tests
    COMMAND bigscreen-rotating-backup-store-tests)
if(NOT MSVC)
    target_compile_options(bigscreen-rotating-backup-store-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(
    bigscreen-up-down-showcase-tests
    UpDownShowcaseTimelineTests.cpp
    ../src/UpDownShowcaseTimeline.cpp)
target_compile_features(bigscreen-up-down-showcase-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-up-down-showcase-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
target_compile_definitions(bigscreen-up-down-showcase-tests PRIVATE
    BIGSCREEN_UP_DOWN_SHOWCASE=1)
add_test(
    NAME bigscreen-up-down-showcase-tests
    COMMAND bigscreen-up-down-showcase-tests)
if(NOT MSVC)
    target_compile_options(bigscreen-up-down-showcase-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

if(NOT MSVC)
    target_compile_options(bigscreen-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(
    bigscreen-map-config-tests
    MapVideoConfigTests.cpp
    ../src/MapVideoConfig.cpp
    ../src/ChromaMapDetector.cpp)
target_compile_features(bigscreen-map-config-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-map-config-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${CMAKE_CURRENT_LIST_DIR}/../extern/includes/rapidjson/rapidjson/include")
add_test(NAME bigscreen-map-config-tests COMMAND bigscreen-map-config-tests)

add_executable(
    bigscreen-quickjs-tests
    QuickJsEngineTests.cpp
    ../src/QuickJsEngine.cpp
    ../.cache/dependencies/quickjs-ng/source/quickjs-amalgam.c)
target_compile_features(bigscreen-quickjs-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-quickjs-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${CMAKE_CURRENT_LIST_DIR}/../.cache/dependencies/quickjs-ng/source")
if(NOT WIN32)
    target_link_libraries(bigscreen-quickjs-tests PRIVATE m)
elseif(MSVC)
    # MSVC exposes <stdatomic.h> only when its C11 atomics implementation is
    # explicitly enabled. Android Clang and Linux GCC need no equivalent flag.
    target_compile_options(
        bigscreen-quickjs-tests PRIVATE /experimental:c11atomics)
endif()
add_test(NAME bigscreen-quickjs-tests COMMAND bigscreen-quickjs-tests)

# The downloader is embedded as Python source in the native library. Validate
# the exact raw strings when a host interpreter is available; this remains
# optional for developers who only installed the C++ toolchain.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME bigscreen-downloader-script-tests
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/DownloaderScriptTests.py"
                "${CMAKE_CURRENT_LIST_DIR}/../src/DownloadManager.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/../python/bigscreen_jsc_provider.py")
    add_test(
        NAME bigscreen-downloader-runtime-build-tests
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/DownloaderRuntimeBuildTests.py"
                "${CMAKE_CURRENT_LIST_DIR}/../scripts/build_downloader_runtime.py")
    add_test(
        NAME bigscreen-repository-invariant-tests
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/RepositoryInvariantTests.py"
                "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

# FrameDecoder has no Unity dependency, so Linux CI can exercise the actual
# FFmpeg worker, timestamp path, resolution scaling, and reusable RGBA pool.
# Host FFmpeg is intentionally optional for ordinary Windows contributors.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HOST_FFMPEG QUIET IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale)
endif()
if(HOST_FFMPEG_FOUND AND Python3_Interpreter_FOUND)
    find_package(Threads REQUIRED)
    add_executable(
        bigscreen-frame-decoder-tests
        FrameDecoderTests.cpp
        ../src/FrameDecoder.cpp)
    target_compile_features(bigscreen-frame-decoder-tests PRIVATE cxx_std_20)
    target_compile_definitions(bigscreen-frame-decoder-tests PRIVATE
        BIGSCREEN_SINGLE_FFMPEG=1)
    target_compile_options(bigscreen-frame-decoder-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    target_include_directories(bigscreen-frame-decoder-tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../include")
    target_link_libraries(bigscreen-frame-decoder-tests PRIVATE
        PkgConfig::HOST_FFMPEG Threads::Threads)

    set(FRAME_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/frame-fixtures")
    add_test(
        NAME bigscreen-generate-frame-fixtures
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/GenerateVideoFixtures.py"
                "${FRAME_FIXTURE_DIR}")
    set_tests_properties(bigscreen-generate-frame-fixtures PROPERTIES
        FIXTURES_SETUP frame_media)
    add_test(
        NAME bigscreen-frame-decoder-tests
        COMMAND bigscreen-frame-decoder-tests
                "${FRAME_FIXTURE_DIR}/landscape.mp4"
                "${FRAME_FIXTURE_DIR}/portrait.mp4")
    set_tests_properties(bigscreen-frame-decoder-tests PROPERTIES
        FIXTURES_REQUIRED frame_media)
endif()
