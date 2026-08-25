# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

cmake_minimum_required(VERSION 3.16)
project(bigscreen_deterministic_zip LANGUAGES C CXX)

add_executable(bigscreen-deterministic-zip
    main.cpp
    vendor/miniz-3.1.2/miniz.c
    vendor/miniz-3.1.2/miniz_tdef.c
    vendor/miniz-3.1.2/miniz_tinfl.c)

target_compile_features(bigscreen-deterministic-zip PRIVATE cxx_std_17)
target_include_directories(bigscreen-deterministic-zip PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniz-3.1.2")

# Keep the executable in one predictable directory for single- and
# multi-configuration CMake generators on every supported host.
set_target_properties(bigscreen-deterministic-zip PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(bigscreen-deterministic-zip PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(MSVC)
    target_compile_options(bigscreen-deterministic-zip PRIVATE /W3)
else()
    target_compile_options(bigscreen-deterministic-zip PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
