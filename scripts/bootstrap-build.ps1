# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Compatibility wrapper for old developer/QPM commands. Toolchain and Quest
# dependency preparation now has one implementation: bootstrap-linux.sh.
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "invoke-canonical-linux.ps1") `
    -LinuxScript "scripts/bootstrap-linux.sh"
if (-not $?) { exit 1 }
