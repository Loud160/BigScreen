# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
include_guard()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets/android-ndk.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qpm.cmake)

add_compile_definitions(QUEST)
add_compile_definitions(UNITY_2021)
add_compile_definitions(NEED_UNSAFE_CSHARP)