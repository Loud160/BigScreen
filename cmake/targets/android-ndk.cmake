include_guard()

if(NOT DEFINED CMAKE_ANDROID_NDK)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ndkpath.txt")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/ndkpath.txt" CMAKE_ANDROID_NDK)
    else()
        if(EXISTS $ENV{ANDROID_NDK_HOME})
            set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
        elseif(EXISTS $ENV{ANDROID_NDK_LATEST_HOME})
            set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_LATEST_HOME})
        endif()
    endif()
endif()

if(NOT DEFINED CMAKE_ANDROID_NDK)
    message(Big time error buddy, no NDK)
endif()

string(REPLACE "\\" "/" CMAKE_ANDROID_NDK ${CMAKE_ANDROID_NDK})

message(STATUS "Using NDK ${CMAKE_ANDROID_NDK}")

# check if contains space
if(CMAKE_ANDROID_NDK MATCHES " ")
    message(FATAL_ERROR "CMAKE_ANDROID_NDK contains a space! Please remove it!")
endif()


# Quest is armv8-64
# Uses Android 12-14 now

set(ANDROID_PLATFORM 24)
set(ANDROID_ABI arm64-v8a)
set(ANDROID_STL c++_static)
set(ANDROID_USE_LEGACY_TOOLCHAIN_FILE OFF)

set(_bigscreen_android_toolchain
    "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(DEFINED CMAKE_TOOLCHAIN_FILE AND
   NOT CMAKE_TOOLCHAIN_FILE STREQUAL "" AND
   NOT CMAKE_TOOLCHAIN_FILE STREQUAL _bigscreen_android_toolchain)
    message(FATAL_ERROR
        "Big Screen requires Android NDK toolchain ${_bigscreen_android_toolchain}, but CMAKE_TOOLCHAIN_FILE is ${CMAKE_TOOLCHAIN_FILE}")
endif()
set(CMAKE_TOOLCHAIN_FILE "${_bigscreen_android_toolchain}" CACHE FILEPATH
    "Android NDK toolchain used by Big Screen" FORCE)
unset(_bigscreen_android_toolchain)

# Set triplet for vcpkg
set(VCPKG_TARGET_TRIPLET arm64-android)
