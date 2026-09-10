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
add_executable(bigscreen-audio-sync-model-tests
    AudioSyncModelTests.cpp ../src/AudioSyncModel.cpp)
target_compile_features(bigscreen-audio-sync-model-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-audio-sync-model-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${CMAKE_CURRENT_LIST_DIR}/../extern/includes/rapidjson/rapidjson/include")
if(NOT MSVC)
    target_compile_options(bigscreen-audio-sync-model-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME bigscreen-audio-sync-model-tests COMMAND bigscreen-audio-sync-model-tests)

add_executable(bigscreen-core-tests CoreLogicTests.cpp)
target_compile_features(bigscreen-core-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-core-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
add_test(NAME bigscreen-core-tests COMMAND bigscreen-core-tests)

add_executable(
    bigscreen-dependency-version-tests
    DependencyVersionTests.cpp
    ../src/DependencyVersion.cpp)
target_compile_features(bigscreen-dependency-version-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-dependency-version-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
add_test(
    NAME bigscreen-dependency-version-tests
    COMMAND bigscreen-dependency-version-tests)
if(NOT MSVC)
    target_compile_options(bigscreen-dependency-version-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

find_package(Threads REQUIRED)
add_executable(bigscreen-audio-sync-analysis-tests
    AudioSyncAnalysisTests.cpp ../src/AudioSyncModel.cpp ../src/AudioSyncAnalysis.cpp)
target_compile_features(bigscreen-audio-sync-analysis-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-audio-sync-analysis-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${CMAKE_CURRENT_LIST_DIR}/../extern/includes/rapidjson/rapidjson/include")
target_link_libraries(bigscreen-audio-sync-analysis-tests PRIVATE Threads::Threads)
if(NOT MSVC)
    target_compile_options(bigscreen-audio-sync-analysis-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME bigscreen-audio-sync-analysis-tests COMMAND bigscreen-audio-sync-analysis-tests)

set(BIGSCREEN_NATIVE_LOGGER_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../.cache/dependencies/native-logger-quest/source")
if(NOT EXISTS "${BIGSCREEN_NATIVE_LOGGER_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR
        "Native Logger Quest is not prepared. Run scripts/build_pipeline.py prepare-native-logger first.")
endif()
set(NATIVE_LOGGER_QUEST_TAG "BigScreen" CACHE STRING "" FORCE)
set(NATIVE_LOGGER_QUEST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${BIGSCREEN_NATIVE_LOGGER_SOURCE}"
    "${CMAKE_CURRENT_BINARY_DIR}/native-logger-quest")

add_executable(
    bigscreen-native-logger-tests
    NativeLoggerTests.cpp)
target_compile_features(bigscreen-native-logger-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-native-logger-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
target_link_libraries(bigscreen-native-logger-tests PRIVATE
    NativeLoggerQuest::NativeLoggerQuest)
add_test(NAME bigscreen-native-logger-tests COMMAND bigscreen-native-logger-tests)
if(NOT MSVC)
    target_compile_options(bigscreen-native-logger-tests PRIVATE
        -UNDEBUG -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(bigscreen-native-logger-tests PRIVATE /UNDEBUG)
endif()

add_executable(
    bigscreen-video-editor-notice-tests
    VideoEditorNoticeModelTests.cpp)
target_compile_features(bigscreen-video-editor-notice-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-video-editor-notice-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
add_test(
    NAME bigscreen-video-editor-notice-tests
    COMMAND bigscreen-video-editor-notice-tests)
if(NOT MSVC)
    target_compile_options(bigscreen-video-editor-notice-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(
    bigscreen-video-library-state-tests
    VideoLibraryMenuStateTests.cpp)
target_compile_features(bigscreen-video-library-state-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-video-library-state-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
add_test(
    NAME bigscreen-video-library-state-tests
    COMMAND bigscreen-video-library-state-tests)
if(NOT MSVC)
    target_compile_options(bigscreen-video-library-state-tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

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
else()
    message(WARNING
        "Python 3 was not found: downloader and repository-policy host tests "
        "will not be registered in this local CTest configuration.")
endif()

# FrameDecoder has no Unity dependency, so Linux CI can exercise the actual
# FFmpeg worker, timestamp path, resolution scaling, and reusable RGBA pool.
# Host FFmpeg is intentionally optional for ordinary Windows contributors.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HOST_AUDIO_FFMPEG QUIET IMPORTED_TARGET
        libavformat libavcodec libavutil libswresample)
    pkg_check_modules(HOST_FFMPEG QUIET IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale)
endif()
if(HOST_AUDIO_FFMPEG_FOUND AND Python3_Interpreter_FOUND)
    add_executable(bigscreen-audio-sync-capture-tests AudioSyncCaptureTests.cpp
        ../src/AudioSyncPcm.cpp ../src/AudioSyncReader.cpp)
    target_compile_features(bigscreen-audio-sync-capture-tests PRIVATE cxx_std_20)
    target_compile_options(bigscreen-audio-sync-capture-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_include_directories(bigscreen-audio-sync-capture-tests PRIVATE ../include)
    target_link_libraries(bigscreen-audio-sync-capture-tests PRIVATE PkgConfig::HOST_AUDIO_FFMPEG Threads::Threads)
    add_test(NAME bigscreen-audio-sync-capture-tests COMMAND bigscreen-audio-sync-capture-tests "${CMAKE_CURRENT_BINARY_DIR}/capture-cache")
    add_executable(bigscreen-audio-sync-service-tests AudioSyncServiceTests.cpp
        ../src/AudioSyncService.cpp ../src/AudioSyncReader.cpp ../src/AudioSyncPcm.cpp ../src/AudioSyncAnalysis.cpp ../src/AudioSyncModel.cpp ../src/AudioSyncVisualization.cpp)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/audio-sync-sonic.cmake)
    add_executable(bigscreen-audio-sync-audition-tests AudioSyncAuditionTests.cpp
        ../src/AudioSyncAudition.cpp ../src/AudioSyncService.cpp ../src/AudioSyncReader.cpp
        ../src/AudioSyncPcm.cpp ../src/AudioSyncAnalysis.cpp ../src/AudioSyncModel.cpp ../src/AudioSyncVisualization.cpp)
    target_compile_features(bigscreen-audio-sync-audition-tests PRIVATE cxx_std_20)
    target_include_directories(bigscreen-audio-sync-audition-tests PRIVATE ../include ../extern/includes/rapidjson/rapidjson/include)
    target_link_libraries(bigscreen-audio-sync-audition-tests PRIVATE bigscreen_sync_pitch PkgConfig::HOST_AUDIO_FFMPEG Threads::Threads)
    target_compile_options(bigscreen-audio-sync-audition-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_features(bigscreen-audio-sync-service-tests PRIVATE cxx_std_20)
    target_include_directories(bigscreen-audio-sync-service-tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../include"
        "${CMAKE_CURRENT_LIST_DIR}/../extern/includes/rapidjson/rapidjson/include")
    target_link_libraries(bigscreen-audio-sync-service-tests PRIVATE PkgConfig::HOST_AUDIO_FFMPEG Threads::Threads)
    target_compile_options(bigscreen-audio-sync-service-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(bigscreen-audio-sync-reader-tests AudioSyncReaderTests.cpp ../src/AudioSyncReader.cpp)
    target_compile_features(bigscreen-audio-sync-reader-tests PRIVATE cxx_std_20)
    target_include_directories(bigscreen-audio-sync-reader-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
    target_link_libraries(bigscreen-audio-sync-reader-tests PRIVATE PkgConfig::HOST_AUDIO_FFMPEG)
    target_compile_options(bigscreen-audio-sync-reader-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set(AUDIO_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/audio-sync-fixtures")
    add_test(NAME bigscreen-generate-audio-sync-fixtures
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/GenerateAudioSyncFixtures.py" "${AUDIO_FIXTURE_DIR}")
    set_tests_properties(bigscreen-generate-audio-sync-fixtures PROPERTIES FIXTURES_SETUP audio_sync_media)
    add_test(NAME bigscreen-audio-sync-reader-tests COMMAND bigscreen-audio-sync-reader-tests "${AUDIO_FIXTURE_DIR}")
    set_tests_properties(bigscreen-audio-sync-reader-tests PROPERTIES FIXTURES_REQUIRED audio_sync_media)
    add_test(NAME bigscreen-audio-sync-service-tests COMMAND bigscreen-audio-sync-service-tests "${AUDIO_FIXTURE_DIR}")
    set_tests_properties(bigscreen-audio-sync-service-tests PROPERTIES FIXTURES_REQUIRED audio_sync_media TIMEOUT 70)
    foreach(sync_case matching-video.m4a aligned-mastered.wav silent.wav unrelated.wav repeated.wav cut.wav)
        add_test(NAME bigscreen-audio-sync-${sync_case}
            COMMAND bigscreen-audio-sync-service-tests "${AUDIO_FIXTURE_DIR}" "${sync_case}")
        set_tests_properties(bigscreen-audio-sync-${sync_case} PROPERTIES
            FIXTURES_REQUIRED audio_sync_media TIMEOUT 90)
    endforeach()
    add_test(NAME bigscreen-audio-sync-audition-tests COMMAND bigscreen-audio-sync-audition-tests "${AUDIO_FIXTURE_DIR}")
    set_tests_properties(bigscreen-audio-sync-audition-tests PROPERTIES FIXTURES_REQUIRED audio_sync_media TIMEOUT 40)
endif()
if(HOST_FFMPEG_FOUND AND Python3_Interpreter_FOUND)
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
elseif(NOT HOST_FFMPEG_FOUND)
    message(WARNING
        "Host FFmpeg development packages were not found: FrameDecoder host "
        "tests will not be registered in this local CTest configuration.")
endif()
