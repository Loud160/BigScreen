# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.22)

option(QUEST "Build for quest" ON)
option(BIGSCREEN_UP_DOWN_SHOWCASE
       "Compile the private Up & Down proof-of-concept choreography" ON)

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

string(TOLOWER "${CMAKE_BUILD_TYPE}" BIGSCREEN_BUILD_TYPE_LOWER)
if(BIGSCREEN_BUILD_TYPE_LOWER STREQUAL "release" OR
   BIGSCREEN_BUILD_TYPE_LOWER STREQUAL "relwithdebinfo" OR
   BIGSCREEN_BUILD_TYPE_LOWER STREQUAL "minsizerel")
        # Better optimizations
        add_compile_options(-O3)

        # LTO
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        add_compile_options(-flto)
endif()

if(BIGSCREEN_BUILD_TYPE_LOWER STREQUAL "debug" OR
   BIGSCREEN_BUILD_TYPE_LOWER STREQUAL "relwithdebinfo")
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
        VERSION ${CMAKE_PACKAGE_VERSION})

# Keep the Quest binary reproducible across the Windows and Linux NDK hosts.
# Beat Saber Hook and generated CORDL helpers embed __FILE__ in assertions and
# diagnostics; without prefix maps, host-specific checkout paths enter rodata
# and change ThinLTO layout. Debug paths also affect the linker's SHA-1 build
# ID even though the installed library is stripped afterward. This must follow
# project(), which is where CMake identifies the selected compilers.
if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR
   CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(BIGSCREEN_REPRODUCIBLE_SOURCE_ROOT "/bigscreen/source")
        add_compile_options(
                "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=${BIGSCREEN_REPRODUCIBLE_SOURCE_ROOT}"
                "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=${BIGSCREEN_REPRODUCIBLE_SOURCE_ROOT}"
                "-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=${BIGSCREEN_REPRODUCIBLE_SOURCE_ROOT}"
                "-fdebug-compilation-dir=${BIGSCREEN_REPRODUCIBLE_SOURCE_ROOT}")
        # LLD's SHA-1 build ID includes pre-strip debug/LTO details that differ
        # by host even when every installed ELF section is otherwise identical.
        # Crash symbols are preserved separately in build/debug; omit only the
        # non-reproducible note from installed binaries.
        add_link_options("-Wl,--build-id=none")
endif()

add_compile_definitions(MOD_ID="${CMAKE_PROJECT_NAME}")
add_compile_definitions(VERSION="${PACKAGE_VERSION}")

# Set COMPILE_ID for qpm purposes
set(COMPILE_ID ${CMAKE_PROJECT_NAME})

# recursively get all src files
RECURSE_FILES(cpp_file_list ${SOURCE_DIR}/*.cpp)
RECURSE_FILES(c_file_list ${SOURCE_DIR}/*.c)

# FrameDecoder.cpp is compiled twice below against ABI-matching headers. The
# ordinary source glob must not also compile it as a third, ambiguous backend.
list(FILTER cpp_file_list EXCLUDE REGEX "[/\\\\]FrameDecoder\\.cpp$")

set(BIGSCREEN_FFMPEG44_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/.cache/dependencies/ffmpeg-lgpl)
set(BIGSCREEN_FFMPEG9_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/.cache/dependencies/ffmpeg-lgpl-9.0.1)

# FFmpeg 4.4 and 9 expose incompatible public structure layouts. Compile the
# same proven worker twice with matching headers and isolate each copy in a
# small shared backend. This separation is essential: an ordinary unversioned
# object reference inside libbigscreen.so could bind both decoder copies to the
# first FFmpeg found. Linking each backend separately records requirements for
# that runtime's private ELF symbol namespace instead. Only the ABI-neutral
# FrameDecoderBackend interface crosses into libbigscreen.so, and VideoFrame's
# reusable vector moves across that boundary without an additional RGBA copy.
add_library(bigscreen_ffmpeg44_backend SHARED ${SOURCE_DIR}/FrameDecoder.cpp)
target_compile_definitions(bigscreen_ffmpeg44_backend PRIVATE
        BIGSCREEN_NATIVE_FFMPEG_IMPLEMENTATION=1
        FrameDecoder=FrameDecoder44Backend
        BIGSCREEN_FFMPEG_BACKEND_FACTORY=CreateFrameDecoder44Backend)
target_include_directories(bigscreen_ffmpeg44_backend PRIVATE ${INCLUDE_DIR})
target_include_directories(bigscreen_ffmpeg44_backend SYSTEM PRIVATE
        ${BIGSCREEN_FFMPEG44_DIR}/include)
target_link_libraries(bigscreen_ffmpeg44_backend PRIVATE
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libavformat-bigscreen44.so
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libavcodec-bigscreen44.so
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libavutil-bigscreen44.so
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libswscale-bigscreen44.so)
set_target_properties(bigscreen_ffmpeg44_backend PROPERTIES
        OUTPUT_NAME "bigscreen-ffmpeg44-backend"
        POSITION_INDEPENDENT_CODE ON)

add_library(bigscreen_ffmpeg9_backend SHARED ${SOURCE_DIR}/FrameDecoder.cpp)
target_compile_definitions(bigscreen_ffmpeg9_backend PRIVATE
        BIGSCREEN_NATIVE_FFMPEG_IMPLEMENTATION=1
        FrameDecoder=FrameDecoder9Backend
        BIGSCREEN_FFMPEG_BACKEND_FACTORY=CreateFrameDecoder9Backend)
target_include_directories(bigscreen_ffmpeg9_backend PRIVATE ${INCLUDE_DIR})
target_include_directories(bigscreen_ffmpeg9_backend SYSTEM PRIVATE
        ${BIGSCREEN_FFMPEG9_DIR}/include)
target_link_libraries(bigscreen_ffmpeg9_backend PRIVATE
        ${BIGSCREEN_FFMPEG9_DIR}/lib/libavformat-bigscreen9.so
        ${BIGSCREEN_FFMPEG9_DIR}/lib/libavcodec-bigscreen9.so
        ${BIGSCREEN_FFMPEG9_DIR}/lib/libavutil-bigscreen9.so
        ${BIGSCREEN_FFMPEG9_DIR}/lib/libswscale-bigscreen9.so)
set_target_properties(bigscreen_ffmpeg9_backend PROPERTIES
        OUTPUT_NAME "bigscreen-ffmpeg9-backend"
        POSITION_INDEPENDENT_CODE ON)

# Preserve backend symbols for crash diagnosis while keeping the installed
# comparison payload small. The template's deferred strip helper covers only
# libbigscreen.so, so these two additional shared targets need the equivalent
# treatment explicitly.
if(CMAKE_STRIP)
        foreach(BIGSCREEN_BACKEND_TARGET IN ITEMS
                bigscreen_ffmpeg44_backend
                bigscreen_ffmpeg9_backend)
                add_custom_command(TARGET ${BIGSCREEN_BACKEND_TARGET} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory
                                ${CMAKE_CURRENT_BINARY_DIR}/debug
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                $<TARGET_FILE:${BIGSCREEN_BACKEND_TARGET}>
                                ${CMAKE_CURRENT_BINARY_DIR}/debug/$<TARGET_FILE_NAME:${BIGSCREEN_BACKEND_TARGET}>
                        COMMAND ${CMAKE_STRIP} --strip-all
                                $<TARGET_FILE:${BIGSCREEN_BACKEND_TARGET}>
                        COMMENT "Preserve symbols and strip ${BIGSCREEN_BACKEND_TARGET}")
        endforeach()
endif()

add_library(
        ${CMAKE_PROJECT_NAME}
        SHARED
        ${cpp_file_list}
        ${c_file_list}
        ${CMAKE_CURRENT_SOURCE_DIR}/.cache/dependencies/quickjs-ng/source/quickjs-amalgam.c
)

# Preserve the incomplete Cinema bloom/color-blending implementation without
# allowing it to enter release builds accidentally. Developers can opt into a
# dedicated investigation build explicitly; the shipped default remains off.
option(BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
        "Compile the unfinished Cinema bloom experiment" OFF)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM=$<BOOL:${BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM}>)

# The ordinary Unity unlit shader writes opaque video alpha into Beat Saber's
# main render target. Beat Saber's post-processing interprets that alpha as an
# emission weight, which turns the complete screen white whenever the game's
# bloom is enabled. Embed Big Screen's Android shader bundle (the visible video
# material plus the experimental YUV conversion pass) into libbigscreen.so so
# every installation has both shaders without a second runtime file that can be
# omitted or left stale.
set(BIGSCREEN_VIDEO_SHADER_ASSET
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/bigscreen_video_shader)
if(NOT EXISTS ${BIGSCREEN_VIDEO_SHADER_ASSET})
        message(FATAL_ERROR
                "Missing embedded video shader asset: ${BIGSCREEN_VIDEO_SHADER_ASSET}")
endif()
set(BIGSCREEN_VIDEO_SHADER_OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/bigscreen_video_shader.o)
add_custom_command(
        OUTPUT ${BIGSCREEN_VIDEO_SHADER_OBJECT}
        COMMAND ${CMAKE_OBJCOPY}
                bigscreen_video_shader
                ${BIGSCREEN_VIDEO_SHADER_OBJECT}
                --input-target binary
                --output-target elf64-aarch64
                --set-section-flags binary=alloc,load,readonly,data,contents
        DEPENDS ${BIGSCREEN_VIDEO_SHADER_ASSET}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets
        COMMENT "Embed Big Screen's Android video shader bundle")
set_source_files_properties(${BIGSCREEN_VIDEO_SHADER_OBJECT} PROPERTIES
        GENERATED TRUE
        EXTERNAL_OBJECT TRUE)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${BIGSCREEN_VIDEO_SHADER_OBJECT})

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
target_compile_options(bigscreen_ffmpeg44_backend PRIVATE
        ${BIGSCREEN_WARNING_OPTIONS})
target_compile_options(bigscreen_ffmpeg9_backend PRIVATE
        ${BIGSCREEN_WARNING_OPTIONS})

# for inline hook
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE -llog)

# QPM exposes link libraries through host-specific generated links. Copy the
# only dependency intentionally carried inside Big Screen's QMOD into build/
# while that link is valid. Packaging then reads a regular file on both hosts
# and cannot serialize a Linux symlink or a stale Windows reparse point.
set(BIGSCREEN_PACKAGED_HOOK_LIBRARY
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/libs/libbeatsaber-hook.so)
if(NOT EXISTS ${BIGSCREEN_PACKAGED_HOOK_LIBRARY})
        message(FATAL_ERROR
                "Missing restored beatsaber-hook library: ${BIGSCREEN_PACKAGED_HOOK_LIBRARY}")
endif()
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BIGSCREEN_PACKAGED_HOOK_LIBRARY}
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/libbeatsaber-hook.so
        COMMENT "Stage the packaged beatsaber-hook runtime as a regular file")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${INCLUDE_DIR})
if(BIGSCREEN_UP_DOWN_SHOWCASE)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
                BIGSCREEN_UP_DOWN_SHOWCASE=1)
endif()
# QPM's generated extern.cmake adds its umbrella dependency directories with
# ordinary `-I`, which makes Clang report warnings from generated Beat Saber,
# BSML, SongCore, and hook headers whenever strict first-party warnings are
# enabled. Register the same roots as system includes before the deferred QPM
# setup. CMake then emits `-isystem` for dependency code while preserving
# -Wall/-Wextra/-Wpedantic on Big Screen's own include/src trees.
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${SHARED_DIR}
        ${EXTERN_DIR}/includes
        ${EXTERN_DIR}/includes/bs-cordl/include)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${EXTERN_DIR}/includes/rapidjson/rapidjson/include)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/.cache/dependencies/quickjs-ng/source)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/.cache/dependencies/downloader/python-3.14.7/prefix)
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

# Big Screen ships both private, minimal LGPL FFmpeg runtimes so the user can
# select a decoder in Misc and compare the same map without reinstalling a
# different QMOD. VideoLibrary's compatibility probe intentionally uses the
# conservative 4.4 headers/runtime; actual playback uses the selected backend.
message(STATUS "Using switchable private FFmpeg 4.4.8 and 9.0.1 runtimes")
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${BIGSCREEN_FFMPEG44_DIR}/include)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        bigscreen_ffmpeg44_backend
        bigscreen_ffmpeg9_backend
        # VideoLibrary's file-compatibility probe remains deliberately bound
        # to the conservative 4.4 runtime and never shares FFmpeg structs with
        # either playback backend.
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libavformat-bigscreen44.so
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libavcodec-bigscreen44.so
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libavutil-bigscreen44.so
        ${BIGSCREEN_FFMPEG44_DIR}/lib/libswscale-bigscreen44.so
)

# Package both complete sets. Distinct suffixes and symbol namespaces are what
# let Android load them simultaneously without satisfying one ABI from another.
foreach(BIGSCREEN_FFMPEG_LIBRARY IN ITEMS
        libavformat-bigscreen44.so
        libavcodec-bigscreen44.so
        libavutil-bigscreen44.so
        libswscale-bigscreen44.so)
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${BIGSCREEN_FFMPEG44_DIR}/lib/${BIGSCREEN_FFMPEG_LIBRARY}
                        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${BIGSCREEN_FFMPEG_LIBRARY}
                COMMENT "Stage ${BIGSCREEN_FFMPEG_LIBRARY}")
endforeach()
foreach(BIGSCREEN_FFMPEG_LIBRARY IN ITEMS
        libavformat-bigscreen9.so
        libavcodec-bigscreen9.so
        libavutil-bigscreen9.so
        libswscale-bigscreen9.so)
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${BIGSCREEN_FFMPEG9_DIR}/lib/${BIGSCREEN_FFMPEG_LIBRARY}
                        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${BIGSCREEN_FFMPEG_LIBRARY}
                COMMENT "Stage ${BIGSCREEN_FFMPEG_LIBRARY}")
endforeach()

# beatsaber hook inline hook

RECURSE_FILES(src_inline_hook_beatsaber_hook_local_extra_c ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.c)
RECURSE_FILES(src_inline_hook_beatsaber_hook_local_extra_cpp ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.cpp)

target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_beatsaber_hook_local_extra_c})
target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_beatsaber_hook_local_extra_cpp})
