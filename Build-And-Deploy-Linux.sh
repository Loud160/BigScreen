#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Native Linux equivalent of Build-And-Deploy.bat. Build, policy, and Quest
# operations use the same repository-owned Bash/Python implementation and do
# not require PowerShell or a second native build recipe.

set -uo pipefail
repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/linux-host-bootstrap.sh
source "${repository_root}/scripts/linux-host-bootstrap.sh"
# shellcheck source=scripts/linux-adb-common.sh
source "${repository_root}/scripts/linux-adb-common.sh"
# shellcheck source=scripts/linux-build-common.sh
source "${repository_root}/scripts/linux-build-common.sh"

original_arguments=("$@")
clean_build=false
assume_yes=false
show_help=false
while (( $# > 0 )); do
    case "$1" in
        --clean) clean_build=true ;;
        --yes|-y) assume_yes=true ;;
        --help|-h)
            show_help=true
            cat <<'EOF'
Usage: bash ./Build-And-Deploy-Linux.sh [--clean] [--yes]

  --clean       Remove only Big Screen's generated native build directory.
  --yes         Approve disclosed host/container, build, and portable-ADB setup.

The complete validation pass always runs; there is no unchecked build mode.
EOF
            exit 0
            ;;
        *)
            printf 'Unknown Linux deployment option: %s\n' "$1" >&2
            exit 2
            ;;
    esac
    shift
done

if [[ "${show_help}" != true ]]; then
    assume_yes_value=0
    [[ "${assume_yes}" == true ]] && assume_yes_value=1
    bigscreen_prepare_or_reexec_linux_host \
        "${repository_root}/Build-And-Deploy-Linux.sh" \
        "${assume_yes_value}" "${original_arguments[@]}"
fi

cat <<'EOF'
============================================================
Building and deploying Big Screen to the connected Quest...
============================================================

FIRST-RUN NETWORK DOWNLOADS
------------------------------------------------------------
This build downloads only missing or invalid pinned inputs, including QPM
packages, Android NDK r27d, FFmpeg 4.4.8 and 9.0.1 source, the embedded
CPython/yt-dlp runtime, QuickJS-NG, and portable ADB when no ADB is installed.
Direct archives are checked against pinned SHA-256 values. A first build can
download several gigabytes and take a while while FFmpeg compiles.
------------------------------------------------------------
EOF

if [[ "${assume_yes}" != true ]] &&
    ! bigscreen_prompt_yes_no \
        'Continue with dependency restore, build, and deployment? [Y/N] '; then
    printf '\nCancelled. No build or dependency download was started.\n'
    exit 0
fi
if [[ "${assume_yes}" == true ]]; then
    export BIGSCREEN_ADB_MISSING_ACTION=Install
fi

if ! bigscreen_require_x64_linux; then
    exit 1
fi
adb_was_running=0
adb_was_used=0
bigscreen_adb_is_running && adb_was_running=1
printf '\nChecking for ADB.\n'
bigscreen_ensure_adb
result=$?
if (( result != 0 )); then
    exit "${result}"
fi

# Start the selected ADB implementation and prove that an authorized Quest with
# Beat Saber is available before spending time compiling. If the headset is
# missing or still waiting for USB-debugging approval, an interactive launcher
# explains the headset steps and lets the user retry without restarting the
# entire script. The final deploy repeats selection so a disconnect during a
# long build is still caught.
printf '\nChecking the Quest connection before building.\n'
adb_was_used=1
(
    cd "${repository_root}"
    python3 "${repository_root}/scripts/quest_tool.py" check --retry
)
result=$?
if (( result != 0 )); then
    printf '\nDeployment cannot continue until the Quest connection is ready.\n' >&2
    bigscreen_complete_adb_session "${adb_was_running}" || true
    exit "${result}"
fi

build_arguments=()
[[ "${clean_build}" == true ]] && build_arguments+=(--clean)
printf '\nBuilding and validating the complete QMOD.\n'
bash "${repository_root}/scripts/build-linux.sh" "${build_arguments[@]}"
result=$?
if (( result == 0 )); then
    printf '\nDeploying the verified build with Big Screen ownership safeguards.\n'
    adb_was_used=1
    (
        cd "${repository_root}"
        python3 "${repository_root}/scripts/quest_tool.py" deploy
    )
    result=$?
fi

printf '\n'
if (( result == 0 )); then
    printf '%s\n' \
        '============================================================' \
        'SUCCESS - Big Screen was built, installed, and Beat Saber was' \
        'asked to restart on the connected Quest.' \
        '============================================================'
else
    printf '%s\n' \
        '============================================================' \
        "BUILD OR DEPLOY FAILED - error code ${result}" \
        'Review the error output above. Nothing after the failed step' \
        'was reported as successfully installed.' \
        '============================================================' >&2
fi

if (( adb_was_used == 1 )); then
    bigscreen_complete_adb_session "${adb_was_running}" || true
fi
exit "${result}"
