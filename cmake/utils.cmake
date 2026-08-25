# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
include_guard()

# get files by filter recursively
MACRO(RECURSE_FILES return_list filter)
    FILE(GLOB_RECURSE new_list ${filter})
    SET(file_list "")

    FOREACH(file_path ${new_list})
        SET(file_list ${file_list} ${file_path})
    ENDFOREACH()

    LIST(REMOVE_DUPLICATES file_list)
    # Filesystem enumeration order differs between NTFS and Linux filesystems.
    # A stable ordinal source order keeps ThinLTO input and constructor layout
    # reproducible when the same Quest target is built on either host.
    LIST(SORT file_list)
    SET(${return_list} ${file_list})
ENDMACRO()
