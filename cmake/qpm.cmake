# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
include_guard()

# Necessary for extern.cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils.cmake)


# read in information about the mod from qpm.json
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/qpm.json PACKAGE_JSON)

string(JSON PACKAGE_INFO GET ${PACKAGE_JSON} info)

string(JSON PACKAGE_NAME GET ${PACKAGE_INFO} name)
string(JSON PACKAGE_ID GET ${PACKAGE_INFO} id)
string(JSON PACKAGE_VERSION GET ${PACKAGE_INFO} version)

# CMake's project(VERSION) grammar accepts only numeric major/minor/patch
# components, while QMOD and QPM versions may carry SemVer prerelease labels.
# Preserve the complete package version for the mod and expose only its numeric
# core to CMake's project metadata.
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" CMAKE_PACKAGE_VERSION
       "${PACKAGE_VERSION}")
if(NOT CMAKE_PACKAGE_VERSION)
    message(FATAL_ERROR "Package version '${PACKAGE_VERSION}' has no numeric SemVer core")
endif()

message(STATUS "PACKAGE NAME: ${PACKAGE_NAME}")
message(STATUS "PACKAGE VERSION: ${PACKAGE_VERSION}")

string(JSON EXTERN_DIR_NAME GET ${PACKAGE_JSON} dependenciesDir)
string(JSON SHARED_DIR_NAME GET ${PACKAGE_JSON} sharedDir)

set(EXTERN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${EXTERN_DIR_NAME})
set(SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${SHARED_DIR_NAME})

# QPM's generated extern file must be included after project() establishes the
# final target identity. Defer only that generated dependency setup; the main
# CMakeLists assigns COMPILE_ID immediately after project().
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _setup_qpm_project())
function(_setup_qpm_project)
    include(${CMAKE_CURRENT_SOURCE_DIR}/extern.cmake)

    # QPM's generated extern.cmake links every restored shared library,
    # including Paper2 restored transitively for beatsaber-hook, BSML,
    # SongCore, and Custom Types. Big Screen's own logger is independent, so
    # remove only libbigscreen.so's Paper link. The library remains available
    # to those other dependencies and their own QMOD manifests remain intact.
    get_target_property(BIGSCREEN_QPM_LINK_LIBRARIES
                        ${COMPILE_ID} LINK_LIBRARIES)
    if(BIGSCREEN_QPM_LINK_LIBRARIES)
        list(FILTER BIGSCREEN_QPM_LINK_LIBRARIES EXCLUDE REGEX
             "(^|[/\\\\])libpaper2_scotland2\\.so$")
        set_property(TARGET ${COMPILE_ID} PROPERTY LINK_LIBRARIES
                     "${BIGSCREEN_QPM_LINK_LIBRARIES}")
    endif()
endfunction(_setup_qpm_project)
