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
    ../extern/quickjs-ng/source/quickjs-amalgam.c)
target_compile_features(bigscreen-quickjs-tests PRIVATE cxx_std_20)
target_include_directories(bigscreen-quickjs-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${CMAKE_CURRENT_LIST_DIR}/../extern/quickjs-ng/source")
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
endif()
