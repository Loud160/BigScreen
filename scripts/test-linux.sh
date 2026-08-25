#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Run the complete host-side validation available on a native Linux machine.
# Unlike the ordinary Windows test path, Linux must have FFmpeg development
# packages so the real decoder worker and generated video fixtures are tested.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=linux-build-common.sh
source "${script_dir}/linux-build-common.sh"

bigscreen_require_x64_linux
repository_root="$(bigscreen_repository_root)"
toolchain_root="$(bigscreen_toolchain_root)"
qpm_executable="$(bigscreen_qpm_executable)"

missing_commands=()
for command_name in cmake ctest ffmpeg pkg-config python3; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        missing_commands+=("${command_name}")
    fi
done
if (( ${#missing_commands[@]} > 0 )); then
    printf 'Missing required Linux test commands: %s\n' \
        "${missing_commands[*]}" >&2
    bigscreen_print_missing_packages
    exit 1
fi

if ! pkg-config --exists libavformat libavcodec libavutil libswscale; then
    printf 'Linux FFmpeg development packages are missing.\n' >&2
    bigscreen_print_missing_packages
    exit 1
fi

printf 'Running Big Screen host, decoder, script, and repository tests.\n'

bash "${repository_root}/tests/LinuxHostBootstrapTests.sh"

# Keep the validation entry point self-contained. A fresh source archive has
# neither QPM-restored headers nor generated mod.json, and repository invariant
# tests intentionally validate both. Bootstrap is cache-aware, so a complete
# build can call this same script without downloading or restoring twice.
bash "${script_dir}/bootstrap-linux.sh"
export BIGSCREEN_TOOLCHAIN_ROOT="${toolchain_root}"
export BIGSCREEN_QPM="${qpm_executable}"
export PATH="$(dirname "${qpm_executable}"):${PATH}"
(
    cd "${repository_root}"
    python3 "${script_dir}/build_pipeline.py" generate-manifest
)

(
    cd "${repository_root}"
    python3 "${script_dir}/build_pipeline.py" prepare-quickjs
    cmake -S "${repository_root}/tests" \
        -B "${repository_root}/build-host-tests-linux"
    cmake --build "${repository_root}/build-host-tests-linux" --config Release
    ctest --test-dir "${repository_root}/build-host-tests-linux" \
        -C Release --output-on-failure
    python3 "${repository_root}/tests/BuildPipelineTests.py" "${repository_root}"
)
