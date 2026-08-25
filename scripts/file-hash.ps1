# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen. Distributed under GPL-3.0-only with additional terms
# under GPLv3 section 7(b)/(c) and an interoperability permission under
# section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Windows PowerShell normally autoloads Get-FileHash from
# Microsoft.PowerShell.Utility. A launcher started from another terminal can
# inherit a custom PSModulePath that prevents that autoload, so security and
# ownership checks use the framework SHA-256 implementation directly.
function Get-BigScreenFileSha256 {
    param([Parameter(Mandatory=$true)][string] $Path)

    $stream = [IO.File]::OpenRead($Path)
    try {
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            return ([BitConverter]::ToString(
                $sha256.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}
