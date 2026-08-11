cmake_minimum_required(VERSION 3.22)

option(QUEST "Build for quest" ON)

# Globals
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
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

# stop symbols leaking
# TODO: Fix
# add_link_options(-Wl, --exclude-libs, ALL)
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
)

# for inline hook
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE -llog)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${INCLUDE_DIR})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${SHARED_DIR})
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${EXTERN_DIR}/includes/rapidjson/rapidjson/include)

# FFmpeg is a dynamic runtime dependency supplied by Hollywood. These pinned
# headers and Android libraries are downloaded by scripts/fetch-ffmpeg.ps1 so
# Big Screen compiles against the exact ABI present on Beat Saber 1.37 Quests.
set(BIGSCREEN_FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${BIGSCREEN_FFMPEG_DIR}/include)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        ${BIGSCREEN_FFMPEG_DIR}/lib/libavformat.so
        ${BIGSCREEN_FFMPEG_DIR}/lib/libavcodec.so
        ${BIGSCREEN_FFMPEG_DIR}/lib/libavutil.so
        ${BIGSCREEN_FFMPEG_DIR}/lib/libswscale.so
)

# Hollywood installs the matching avformat/avcodec/avutil runtime libraries,
# but its public package does not include swscale. Copy the exact pinned
# companion library beside our build so the qmod can install that one missing
# runtime dependency without duplicating Hollywood's other FFmpeg libraries.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BIGSCREEN_FFMPEG_DIR}/lib/libswscale.so
                $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/libswscale.so
        COMMENT "Stage Big Screen's pinned libswscale runtime")

# beatsaber hook inline hook

RECURSE_FILES(src_inline_hook_beatsaber_hook_local_extra_c ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.c)
RECURSE_FILES(src_inline_hook_beatsaber_hook_local_extra_cpp ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.cpp)

target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_beatsaber_hook_local_extra_c})
target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_beatsaber_hook_local_extra_cpp})
