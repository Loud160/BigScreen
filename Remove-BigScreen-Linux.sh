#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Native Linux launcher for the same receipt-owned removal policy used on
# Windows. It never recursively deletes Big Screen's user media or log data.

set -uo pipefail
repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/linux-adb-common.sh
source "${repository_root}/scripts/linux-adb-common.sh"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    cat <<'EOF'
Usage: bash ./Remove-BigScreen-Linux.sh

Interactively removes only a source-managed Big Screen installation. Videos,
library data, thumbnails, maps, and logs are preserved by default. The script
asks separately whether settings and Big Screen-managed downloaded videos
should be removed. Map-folder and Video Import videos are never removed.
EOF
    exit 0
fi
if (( $# > 0 )); then
    printf 'Unknown Linux removal option: %s\n' "$1" >&2
    exit 2
fi

printf '%s\n' \
    '============================================================' \
    'Big Screen Source Installation Remover' \
    '============================================================'
if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3 is required by the canonical Big Screen Linux tools.\n' >&2
    exit 1
fi
adb_was_running=0
bigscreen_adb_is_running && adb_was_running=1
printf '\nChecking for ADB.\n'
bigscreen_ensure_adb
result=$?
if (( result != 0 )); then
    exit "${result}"
fi

(
    cd "${repository_root}"
    python3 "${repository_root}/scripts/quest_tool.py" remove
)
result=$?
printf '\n'
if (( result == 0 )); then
    printf 'Removal workflow completed.\n'
else
    printf 'Removal did not complete. Review the safety message above.\n' >&2
fi
bigscreen_complete_adb_session "${adb_was_running}" || true
exit "${result}"
