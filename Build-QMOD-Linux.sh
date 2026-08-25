#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Friendly repository-root launcher. Invoke it through `bash` so downloaded
# source archives work even when their ZIP extractor did not preserve Unix
# executable permission bits.

set -euo pipefail
repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/linux-host-bootstrap.sh
source "${repository_root}/scripts/linux-host-bootstrap.sh"

original_arguments=("$@")
assume_yes=0
build_arguments=()
show_help=false
for argument in "$@"; do
    case "${argument}" in
        --yes|-y) assume_yes=1 ;;
        --help|-h) show_help=true; build_arguments+=("${argument}") ;;
        *) build_arguments+=("${argument}") ;;
    esac
done

if [[ "${show_help}" != true ]]; then
    bigscreen_prepare_or_reexec_linux_host \
        "${repository_root}/Build-QMOD-Linux.sh" "${assume_yes}" \
        "${original_arguments[@]}"
fi

exec bash "${repository_root}/scripts/build-linux.sh" \
    "${build_arguments[@]}"
