cmake_minimum_required(VERSION 3.20)
project(BigScreenCoreTests LANGUAGES CXX)
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

# The downloader is embedded as Python source in the native library. Validate
# the exact raw strings when a host interpreter is available; this remains
# optional for developers who only installed the C++ toolchain.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME bigscreen-downloader-script-tests
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/DownloaderScriptTests.py"
                "${CMAKE_CURRENT_LIST_DIR}/../src/DownloadManager.cpp")
endif()
