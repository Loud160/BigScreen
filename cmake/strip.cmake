# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Run at end to link with project
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _setup_linux_strip_project())

function(_setup_linux_strip_project)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        message("Enabling Stripping")

        # Ninja combines the final link, ThinLTO optimization, symbol handling,
        # and all POST_BUILD staging commands into one progress item. On a clean
        # build that item can remain visible for a while with no changing
        # percentage. Print a separate line from the rule immediately before
        # the linker starts so an end user does not mistake normal work for a
        # frozen build script.
        add_custom_command(TARGET ${COMPILE_ID} PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E echo
                "Final Big Screen link and optimization is running. This can take a few minutes on some computers; please wait."
        )

        # Strip debug symbols
        add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
            COMMAND ${CMAKE_STRIP} -d --strip-all
            "lib${COMPILE_ID}.so" -o "stripped_lib${COMPILE_ID}.so"
            COMMENT "Strip debug symbols done on final binary.")

        add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory debug
            COMMENT "Make directory for debug symbols"
        )
        add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rename lib${COMPILE_ID}.so debug/lib${COMPILE_ID}.so
            COMMENT "Rename the lib to debug_ since it has debug symbols"
        )

        # strip debug symbols from the .so and all dependencies
        add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rename stripped_lib${COMPILE_ID}.so lib${COMPILE_ID}.so
            COMMENT "Rename the stripped lib to regular"
        )
    endif()
endfunction(_setup_linux_strip_project)
