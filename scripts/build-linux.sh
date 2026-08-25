#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Supported native x86-64 Linux entrypoint. This deliberately shares Big
# Screen's existing pinned fetch/build/package scripts with Windows and CI;
# there is no second implementation whose dependency versions can drift.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=linux-build-common.sh
source "${script_dir}/linux-build-common.sh"

clean_build=false
while (( $# > 0 )); do
    case "$1" in
        --clean)
            clean_build=true
            ;;
        --help|-h)
            cat <<'EOF'
Usage: bash ./Build-QMOD-Linux.sh [--clean] [--yes]

  --clean  Remove only Big Screen's generated native build directory.
  --yes    Approve disclosed host/container and pinned dependency downloads.

Every supported build runs the full host, decoder, repository-policy, QMOD,
dependency, and ownership validation pass. There is no unchecked build mode.
EOF
            exit 0
            ;;
        *)
            printf 'Unknown Linux build option: %s\n' "$1" >&2
            exit 2
            ;;
    esac
    shift
done

bigscreen_require_x64_linux
repository_root="$(bigscreen_repository_root)"
toolchain_root="$(bigscreen_toolchain_root)"
qpm_executable="$(bigscreen_qpm_executable)"
ndk_root="$(bigscreen_ndk_root)"

bash "${script_dir}/bootstrap-linux.sh"
export BIGSCREEN_TOOLCHAIN_ROOT="${toolchain_root}"
export ANDROID_NDK_ROOT="${ndk_root}"
export BIGSCREEN_QPM="${qpm_executable}"
export PATH="$(dirname "${qpm_executable}"):${PATH}"

# test-linux.sh owns bootstrap and manifest generation so direct validation and
# complete builds exercise exactly the same checks in exactly the same order.
bash "${script_dir}/test-linux.sh"

printf '\nBuilding Big Screen\047s isolated FFmpeg 4.4.8 runtime.\n'
printf 'A clean first build can take several minutes; normal compiler progress will remain visible.\n'
BIGSCREEN_FFMPEG_VERSION=4.4.8 \
    bash "${script_dir}/build-ffmpeg-lgpl.sh"

printf '\nBuilding Big Screen\047s isolated FFmpeg 9.0.1 runtime.\n'
printf 'A clean first build can take several minutes; normal compiler progress will remain visible.\n'
BIGSCREEN_FFMPEG_VERSION=9.0.1 \
    bash "${script_dir}/build-ffmpeg-lgpl.sh"

printf '\nBuilding the ARM64 Quest mod with Android NDK r27d.\n'
build_arguments=("${script_dir}/build_pipeline.py" build-native)
if [[ "${clean_build}" == true ]]; then
    build_arguments+=(--clean)
fi
(
    cd "${repository_root}"
    python3 "${build_arguments[@]}"
)

printf '\nCreating and validating the complete QMOD package.\n'
(
    cd "${repository_root}"
    python3 "${script_dir}/build_pipeline.py" package
)

qmod_path="${repository_root}/Big Screen.qmod"
if [[ ! -s "${qmod_path}" ]]; then
    printf 'The Linux build did not produce a non-empty QMOD: %s\n' \
        "${qmod_path}" >&2
    exit 1
fi

printf '\nLinux build completed successfully.\n'
printf 'QMOD: %s\n' "${qmod_path}"
printf 'Size: %s bytes\n' "$(stat -c '%s' "${qmod_path}")"
printf 'SHA-256: '
sha256sum "${qmod_path}" | awk '{ print $1 }'
