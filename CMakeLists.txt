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

# Big Screen embeds the official CPython Android runtime for its optional
# downloader. Link against the exact staged ABI and package the library beside
# the mod; pure Python files are installed into Big Screen's ModData folder.
set(BIGSCREEN_PYTHON_PREFIX
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/downloader/python-3.14.6/prefix)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
        ${BIGSCREEN_PYTHON_PREFIX}/include/python3.14)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        ${BIGSCREEN_PYTHON_PREFIX}/lib/libpython3.14.so
        -ldl
        -lz)

# Big Screen ships a private, minimal FFmpeg 4.4.8 runtime built exclusively
# from LGPL components.  Its custom SONAMEs and symbol-version namespaces keep
# Android from resolving these calls to Hollywood's unrelated GPL FFmpeg build.
# scripts/build-ffmpeg-lgpl.sh is the single reproducible source of these files.
set(BIGSCREEN_FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg-lgpl)
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
