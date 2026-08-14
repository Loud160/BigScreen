cmake_minimum_required(VERSION 3.22)

option(QUEST "Build for quest" ON)
option(BIGSCREEN_UP_DOWN_SHOWCASE
       "Compile the private Up & Down proof-of-concept choreography" OFF)

# Globals
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_compile_options(-frtti -fexceptions -fvisibility=hidden -fPIE -fPIC)

# Include. Include order matters!
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qpm.cmake)

if(${CMAKE_BUILD_TYPE} STREQUAL "RELEASE" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo" OR ${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
        # Better optimizations
        add_compile_options(-O3)

        # LTO
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        add_compile_options(-flto)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "DEBUG" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
        add_compile_options(-g)
endif()

# Targets
if(QUEST)
        include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets/quest.cmake)
endif()

# Post build
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/strip.cmake)

# Hidden visibility above keeps private implementation symbols out of the mod's
# public ELF surface. Required mod-loader entry points carry explicit export
# attributes, so a broad linker exclude rule is both unnecessary and unsafe.
project( bigscreen
        VERSION ${PACKAGE_VERSION})

add_compile_definitions(MOD_ID="${CMAKE_PROJECT_NAME}")
add_compile_definitions(VERSION="${CMAKE_PROJECT_VERSION}")

# Set COMPILE_ID for qpm purposes
set(COMPILE_ID ${CMAKE_PROJECT_NAME})

# recursively get all src files
RECURSE_FILES(cpp_file_list ${SOURCE_DIR}/*.cpp)
RECURSE_FILES(c_file_list ${SOURCE_DIR}/*.c)

add_library(
        ${CMAKE_PROJECT_NAME}
        SHARED
        ${cpp_file_list}
        ${c_file_list}
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/quickjs-ng/source/quickjs-amalgam.c
)

# Keep warnings actionable by applying them only to Big Screen's C++ sources.
# The target also compiles upstream inline-hook code; enabling project policy
# on that source produced hundreds of diagnostics that this repository cannot
# fix and obscured warnings in the mod itself.
set(BIGSCREEN_WARNING_OPTIONS
        -Wall -Wextra -Wpedantic
        # Beat Saber Hook intentionally exposes GNU statement-expression
        # macros in public headers. They are required by the supported Quest
        # toolchain and cannot be replaced from this repository; suppress only
        # that upstream-extension diagnostic so -Werror remains useful for Big
        # Screen's own warnings.
        -Wno-gnu-statement-expression-from-macro-expansion
        -Wno-gnu-zero-variadic-macro-arguments
        -Wno-zero-length-array
        -Wno-gnu-anonymous-struct
        -Wno-nested-anon-types)
set_source_files_properties(${cpp_file_list} PROPERTIES
        COMPILE_OPTIONS "${BIGSCREEN_WARNING_OPTIONS}")

# for inline hook
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE -llog)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${INCLUDE_DIR})
if(BIGSCREEN_UP_DOWN_SHOWCASE)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
                BIGSCREEN_UP_DOWN_SHOWCASE=1)
endif()
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${SHARED_DIR})
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${EXTERN_DIR}/includes/rapidjson/rapidjson/include)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/quickjs-ng/source)

# QuickJS-NG is compiled into libbigscreen.so instead of shipped as a qjs
# executable. Android forbids execve from both /sdcard and Beat Saber's writable
# private directories, while an in-process engine also avoids carrying Node or
# Deno. Do not enable QJS_BUILD_LIBC: YouTube solver scripts receive only the
# JavaScript language runtime and Big Screen's captured console, with no direct
# filesystem, process, or network APIs.

# Big Screen embeds the official CPython Android runtime for its optional
# downloader. Link against the exact staged ABI and package the library beside
# the mod; pure Python files are installed into Big Screen's ModData folder.
set(BIGSCREEN_PYTHON_PREFIX
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/downloader/python-3.14.7/prefix)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${BIGSCREEN_PYTHON_PREFIX}/include/python3.14)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        ${BIGSCREEN_PYTHON_PREFIX}/lib/libpython3.14.so
        -ldl
        -lz)

# Stage the entire native CPython set beside the built mod. Development copy
# previously looked only in build/ but fetch-downloader-runtime.ps1 placed these
# files in extern/libs, allowing a new extension set to be deployed against an
# older libpython already on the Quest. These are one inseparable ABI unit.
foreach(BIGSCREEN_PYTHON_LIBRARY
        libpython3.14.so
        libssl_python.so
        libcrypto_python.so
        libsqlite3_python.so)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${BIGSCREEN_PYTHON_PREFIX}/lib/${BIGSCREEN_PYTHON_LIBRARY}
                    $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${BIGSCREEN_PYTHON_LIBRARY}
            COMMENT "Stage ${BIGSCREEN_PYTHON_LIBRARY}")
endforeach()

# Big Screen ships a private, minimal FFmpeg runtime built exclusively
# from LGPL components.  Its custom SONAMEs and symbol-version namespaces keep
# Android from resolving these calls to Hollywood's unrelated GPL FFmpeg build.
# scripts/build-ffmpeg-lgpl.sh is the single reproducible source of these files.
set(BIGSCREEN_FFMPEG_VERSION "4.4.8" CACHE STRING
    "Private FFmpeg runtime used by Big Screen (4.4.8 or 9.0.1)")
set_property(CACHE BIGSCREEN_FFMPEG_VERSION PROPERTY STRINGS 4.4.8 9.0.1)
if(BIGSCREEN_FFMPEG_VERSION STREQUAL "4.4.8")
    set(BIGSCREEN_FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg-lgpl)
elseif(BIGSCREEN_FFMPEG_VERSION STREQUAL "9.0.1")
    set(BIGSCREEN_FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg-lgpl-9.0.1)
else()
    message(FATAL_ERROR "Unsupported BIGSCREEN_FFMPEG_VERSION=${BIGSCREEN_FFMPEG_VERSION}")
endif()
message(STATUS "Using private FFmpeg ${BIGSCREEN_FFMPEG_VERSION}")
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        BIGSCREEN_FFMPEG_VERSION="${BIGSCREEN_FFMPEG_VERSION}")
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${BIGSCREEN_FFMPEG_DIR}/include)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        ${BIGSCREEN_FFMPEG_DIR}/lib/libavformat-bigscreen.so
        ${BIGSCREEN_FFMPEG_DIR}/lib/libavcodec-bigscreen.so
        ${BIGSCREEN_FFMPEG_DIR}/lib/libavutil-bigscreen.so
        ${BIGSCREEN_FFMPEG_DIR}/lib/libswscale-bigscreen.so
)

# Package the entire matching set. Mixing one library from a different FFmpeg
# build would defeat both ABI safety and the LGPL/GPL separation.
foreach(BIGSCREEN_FFMPEG_LIBRARY
        libavformat-bigscreen.so
        libavcodec-bigscreen.so
        libavutil-bigscreen.so
        libswscale-bigscreen.so)
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${BIGSCREEN_FFMPEG_DIR}/lib/${BIGSCREEN_FFMPEG_LIBRARY}
                        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${BIGSCREEN_FFMPEG_LIBRARY}
                COMMENT "Stage ${BIGSCREEN_FFMPEG_LIBRARY}")
endforeach()

# beatsaber hook inline hook

RECURSE_FILES(src_inline_hook_beatsaber_hook_local_extra_c ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.c)
RECURSE_FILES(src_inline_hook_beatsaber_hook_local_extra_cpp ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.cpp)

target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_beatsaber_hook_local_extra_c})
target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_beatsaber_hook_local_extra_cpp})
