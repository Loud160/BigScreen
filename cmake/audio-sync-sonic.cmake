# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
# Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Linux, WSL and host tests share one pinned/private source cache. Python is
# already required; no Git, system Sonic or new installed build tool is needed.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(BIGSCREEN_SONIC_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${BIGSCREEN_SONIC_ROOT}/scripts/build_pipeline.py" prepare-sonic
    RESULT_VARIABLE BIGSCREEN_SONIC_RESULT)
if(NOT BIGSCREEN_SONIC_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not prepare verified private Sonic audio source.")
endif()
add_library(bigscreen_sync_pitch STATIC "${BIGSCREEN_SONIC_ROOT}/.cache/dependencies/sonic/source/sonic.c")
target_include_directories(bigscreen_sync_pitch PUBLIC "${BIGSCREEN_SONIC_ROOT}/.cache/dependencies/sonic/source")
set_target_properties(bigscreen_sync_pitch PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
