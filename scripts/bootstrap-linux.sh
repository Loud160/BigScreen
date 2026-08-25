#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Prepare the pinned host-side tools that belong to Big Screen's build. This
# script intentionally does not invoke sudo or install operating-system
# packages. It reports missing prerequisites, downloads only project-specific
# tools, verifies immutable hashes, and reuses valid caches on later builds.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=linux-build-common.sh
source "${script_dir}/linux-build-common.sh"

bigscreen_require_x64_linux

repository_root="$(bigscreen_repository_root)"
toolchain_root="$(bigscreen_toolchain_root)"
qpm_root="$(bigscreen_qpm_root)"
qpm_executable="$(bigscreen_qpm_executable)"
ndk_root="$(bigscreen_ndk_root)"

required_commands=(
    bash cmake curl make ninja nproc python3 sed sha256sum tar unzip xz
)
missing_commands=()
for command_name in "${required_commands[@]}"; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        missing_commands+=("${command_name}")
    fi
done
if (( ${#missing_commands[@]} > 0 )); then
    printf 'Missing required Linux build commands: %s\n' \
        "${missing_commands[*]}" >&2
    bigscreen_print_missing_packages
    exit 1
fi

cmake_version="$(cmake --version | awk 'NR == 1 { print $3 }')"
if [[ -z "${cmake_version}" ]] ||
   ! printf '%s\n%s\n' "3.22.0" "${cmake_version}" | sort -V -C; then
    printf 'CMake 3.22 or newer is required; found %s.\n' \
        "${cmake_version:-unknown}" >&2
    exit 1
fi

mkdir -p "${toolchain_root}"

qpm_archive="${toolchain_root}/qpm-linux-x64-musl-${BIGSCREEN_LINUX_QPM_VERSION}.zip"
qpm_download="${qpm_archive}.download.$$"
qpm_url="https://github.com/QuestPackageManager/QPM.CLI/releases/download/v${BIGSCREEN_LINUX_QPM_VERSION}/qpm-linux-x64-musl.zip"
trap 'rm -f "${qpm_download}"' EXIT INT TERM

qpm_archive_valid() {
    [[ -f "${qpm_archive}" ]] &&
        printf '%s  %s\n' "${BIGSCREEN_LINUX_QPM_ARCHIVE_SHA256}" \
            "${qpm_archive}" | sha256sum --check --status
}

if [[ -f "${qpm_archive}" ]] && ! qpm_archive_valid; then
    printf 'Discarding an invalid cached QPM archive: %s\n' \
        "${qpm_archive}" >&2
    rm -f "${qpm_archive}"
fi
if [[ ! -f "${qpm_archive}" ]]; then
    printf 'Downloading QPM CLI %s from its official GitHub release.\n' \
        "${BIGSCREEN_LINUX_QPM_VERSION}"
    printf 'The archive will be verified against Big Screen\047s pinned SHA-256.\n'
    curl --fail --location --retry 3 --silent --show-error \
        --output "${qpm_download}" "${qpm_url}"
    printf '%s  %s\n' "${BIGSCREEN_LINUX_QPM_ARCHIVE_SHA256}" \
        "${qpm_download}" | sha256sum --check --strict
    mv -f "${qpm_download}" "${qpm_archive}"
else
    printf 'Using cached QPM CLI %s archive.\n' \
        "${BIGSCREEN_LINUX_QPM_VERSION}"
fi

if [[ ! -x "${qpm_executable}" ]]; then
    rm -rf "${qpm_root}"
    mkdir -p "${qpm_root}"
    unzip -q "${qpm_archive}" -d "${qpm_root}"
    chmod 0755 "${qpm_executable}"
fi
"${qpm_executable}" --help >/dev/null

printf 'Preparing Android NDK r27d (%s). A first download and extraction can take several minutes.\n' \
    "${BIGSCREEN_LINUX_NDK_VERSION}"
BIGSCREEN_TOOLCHAIN_ROOT="${toolchain_root}" \
    bash "${script_dir}/install-pinned-ndk.sh"

if [[ ! -f "${ndk_root}/source.properties" ]] ||
   ! grep -q "Pkg.Revision = ${BIGSCREEN_LINUX_NDK_VERSION}" \
       "${ndk_root}/source.properties"; then
    printf 'The pinned Android NDK is incomplete or has the wrong revision: %s\n' \
        "${ndk_root}" >&2
    exit 1
fi

# QPM restore reads the ignored ndkpath.txt when resolving dependency build
# metadata. Replace a stale Windows path before invoking QPM, not afterward,
# so the same checkout can move between Windows and native Linux/WSL without a
# failed first restore. The path points only to the verified r27d installation
# prepared above.
printf '%s\n' "${ndk_root}" > "${repository_root}/ndkpath.txt"

qpm_host_stamp="${repository_root}/.cache/qpm-restore-host.txt"
qpm_lock_stamp="${repository_root}/.cache/qpm-restore.sha256"
restored_host=""
if [[ -f "${qpm_host_stamp}" ]]; then
    restored_host="$(tr -d '\r\n' < "${qpm_host_stamp}")"
fi
qpm_lock_hash="$(sha256sum "${repository_root}/qpm.shared.json" | awk '{ print $1 }')"
qpm_inputs_ready=false
if [[ "${restored_host}" == "linux" ]] &&
   [[ -f "${qpm_lock_stamp}" ]] &&
   [[ "$(tr -d '\r\n' < "${qpm_lock_stamp}")" == "${qpm_lock_hash}" ]] &&
   [[ -f "${repository_root}/extern.cmake" ]] &&
   [[ -f "${repository_root}/qpm_defines.cmake" ]] &&
   [[ -f "${repository_root}/extern/libs/libbeatsaber-hook.so" ]] &&
   [[ -f "${repository_root}/extern/libs/libbsml.so" ]] &&
   [[ -f "${repository_root}/extern/libs/libpaper2_scotland2.so" ]] &&
   [[ -f "${repository_root}/extern/libs/libsl2.so" ]] &&
   [[ -f "${repository_root}/extern/libs/libsongcore.so" ]] &&
   [[ -f "${repository_root}/extern/includes/rapidjson/rapidjson/include/rapidjson/document.h" ]]; then
    qpm_inputs_ready=true
fi

if [[ "${qpm_inputs_ready}" != true && "${restored_host}" != "linux" ]]; then
    # QPM generates host-native links under extern/. A Windows junction or an
    # NTFS reparse point is not a usable Linux dependency link (and vice versa).
    # Reset only QPM's ignored output after proving the exact directory is a
    # direct child of this repository; tracked source is never touched.
    extern_root="$(realpath -m "${repository_root}/extern")"
    case "${extern_root}" in
        "${repository_root}"/extern)
            printf 'Resetting host-specific QPM links for Linux.\n'
            rm -rf -- "${extern_root}"
            rm -f -- "${repository_root}/extern.cmake" \
                "${repository_root}/qpm_defines.cmake"
            ;;
        *)
            printf 'Refusing to reset QPM files outside the repository: %s\n' \
                "${extern_root}" >&2
            exit 1
            ;;
    esac
fi

if [[ "${qpm_inputs_ready}" == true ]]; then
    printf 'Using QPM dependencies already restored for this lockfile and Linux host.\n'
else
    printf 'Restoring the exact Quest headers and libraries from qpm.shared.json.\n'
    printf 'QPM downloads only missing or mismatched locked packages.\n'
    (
        cd "${repository_root}"
        "${qpm_executable}" restore
    )
    mkdir -p "${repository_root}/.cache"
    printf 'linux' > "${qpm_host_stamp}"
    printf '%s\n' "${qpm_lock_hash}" > "${qpm_lock_stamp}"
fi

printf '\nLinux build bootstrap complete.\n'
printf '  QPM: %s\n' "${qpm_executable}"
printf '  NDK:  %s\n' "${ndk_root}"
