#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Native Linux launcher for Big Screen's freshness-aware Python collector.

set -uo pipefail
repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/linux-adb-common.sh
source "${repository_root}/scripts/linux-adb-common.sh"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    cat <<'EOF'
Usage: bash ./Collect-BigScreen-Logs-Linux.sh [-SinceMinutes N] [-OutputRoot PATH]

Examples:
  bash ./Collect-BigScreen-Logs-Linux.sh
  bash ./Collect-BigScreen-Logs-Linux.sh -SinceMinutes 120
  bash ./Collect-BigScreen-Logs-Linux.sh -OutputRoot "$HOME/Desktop/BigScreen Logs"

The ZIP is written to "BigScreen Support Logs" in the repository unless
-OutputRoot is supplied.
EOF
    exit 0
fi

printf '%s\n' \
    'Big Screen Support Log Collector' \
    '=================================' \
    'This collects recent Big Screen, Beat Saber, and Quest crash information.' \
    'It does not change anything on the headset.'
adb_was_running=0
bigscreen_adb_is_running && adb_was_running=1
if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3 is required by the canonical Big Screen Linux tools.\n' >&2
    exit 1
fi
printf '\nChecking for ADB.\n'
bigscreen_ensure_adb
result=$?
if (( result != 0 )); then
    exit "${result}"
fi

python_arguments=("${repository_root}/scripts/quest_tool.py" collect-logs)
while (( $# > 0 )); do
    case "$1" in
        -SinceMinutes|--since-minutes)
            [[ $# -ge 2 ]] || { printf 'Missing value for %s\n' "$1" >&2; exit 2; }
            python_arguments+=(--since-minutes "$2")
            shift 2
            ;;
        -OutputRoot|--output-root)
            [[ $# -ge 2 ]] || { printf 'Missing value for %s\n' "$1" >&2; exit 2; }
            python_arguments+=(--output-root "$2")
            shift 2
            ;;
        *)
            printf 'Unknown log collector option: %s\n' "$1" >&2
            exit 2
            ;;
    esac
done
(
    cd "${repository_root}"
    python3 "${python_arguments[@]}"
)
result=$?
printf '\n'
if (( result == 0 )); then
    printf 'Log collection completed.\n'
else
    printf 'Log collection did not complete. Read the message above for the cause.\n' >&2
fi
bigscreen_complete_adb_session "${adb_was_running}" || true
exit "${result}"
