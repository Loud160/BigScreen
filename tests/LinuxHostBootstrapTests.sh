#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen. Distributed under GPL-3.0-only with additional terms
# under GPLv3 section 7(b)/(c) and an interoperability permission under
# section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

set -euo pipefail
repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../scripts/linux-host-bootstrap.sh
source "${repository_root}/scripts/linux-host-bootstrap.sh"

strategy="$(bigscreen_linux_host_strategy)"
[[ "${strategy}" == "native" ]] || {
    printf 'Expected the supported Linux test host to build natively; found %s.\n' \
        "${strategy}" >&2
    exit 1
}

# A readable existing file is sufficient to simulate the OSTree boot marker;
# strategy selection must not need a live immutable test host. Clear the
# managed-container marker inside this isolated probe because the full test
# suite also runs after a public launcher has re-entered its Distrobox.
strategy="$(
    unset BIGSCREEN_MANAGED_DISTROBOX
    BIGSCREEN_OSTREE_MARKER="${BIGSCREEN_OS_RELEASE_FILE}"
    bigscreen_linux_host_strategy
)"
[[ "${strategy}" == "distrobox" ]] || {
    printf 'Expected an immutable host to select Distrobox; found %s.\n' \
        "${strategy}" >&2
    exit 1
}

# The environment marker prevents recursive container creation after the
# public launcher re-enters itself inside the managed Ubuntu Distrobox.
strategy="$(BIGSCREEN_MANAGED_DISTROBOX=1 bigscreen_linux_host_strategy)"
[[ "${strategy}" == "native" ]] || {
    printf 'Expected the managed Distrobox to continue natively; found %s.\n' \
        "${strategy}" >&2
    exit 1
}

printf 'Linux host and Distrobox strategy tests passed.\n'
