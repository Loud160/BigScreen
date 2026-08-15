# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
$mod = "./mod.json"
$modTemplate = "./mod.template.json"
$qpmShared = "./qpm.shared.json"

if (Test-Path -Path $modTemplate) {
    $update = -not (Test-Path -Path $mod)

    if (-not $update) {
        $update = (Get-Item $modTemplate).LastWriteTime -gt (Get-Item $mod).LastWriteTime
    }

    if (-not $update -and (Test-Path -Path $qpmShared)) {
        $update = (Get-Item $qpmShared).LastWriteTime -gt (Get-Item $mod).LastWriteTime
    }

    if ($update) {
        $qpmCommand = Get-Command qpm -ErrorAction SilentlyContinue
        if ($qpmCommand) {
            & $qpmCommand.Source qmod manifest
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
        }
        elseif (-not (Test-Path -Path $mod)) {
            Write-Output "Error: qpm is required to create the initial mod.json"
            exit 1
        }
        else {
            Write-Output "qpm is not on PATH; using the existing mod.json and synchronizing package fields."
        }
    }
}
elseif (-not (Test-Path -Path $mod)) {
    Write-Output "Error: mod.json and mod.template.json were not present"
    exit 1
}

Write-Output "Creating qmod from mod.json"

# Always perform an offline structural check. Windows PowerShell 5.1 lacks
# Test-Json, but release packaging must still fail on missing critical fields.
try {
    $parsed = Get-Content $mod -Raw | ConvertFrom-Json -ErrorAction Stop
}
catch {
    Write-Output "Error: mod.json is not valid JSON: $($_.Exception.Message)"
    exit 1
}
$requiredText = @("name", "id", "author", "version", "packageId", "packageVersion")
foreach ($property in $requiredText) {
    if (-not $parsed.$property -or -not ($parsed.$property -is [string])) {
        Write-Output "Error: mod.json is missing required text field '$property'"
        exit 1
    }
}
if (-not $parsed.modFiles -or $parsed.modFiles.Count -lt 1) {
    Write-Output "Error: mod.json must declare at least one modFiles entry"
    exit 1
}

$psVersion = $PSVersionTable.PSVersion.Major
if ($psVersion -ge 6) {
    # Pin the schema revision so upstream branch changes cannot silently alter
    # release validation semantics.
    $schemaRevision = "eadb8d8d21caa1f8586b61da3c950a2953ebd399"
    $schemaUrl = "https://raw.githubusercontent.com/Lauriethefish/QuestPatcher.QMod/$schemaRevision/QuestPatcher.QMod/Resources/qmod.schema.json"
    Invoke-WebRequest $schemaUrl -OutFile ./mod.schema.json

    $schema = "./mod.schema.json"
    $modJsonRaw = Get-Content $mod -Raw
    $modSchemaRaw = Get-Content $schema -Raw

    Remove-Item $schema

    Write-Output "Validating mod.json..."
    if (-not ($modJsonRaw | Test-Json -Schema $modSchemaRaw)) {
        Write-Output "Error: mod.json is not valid"
        exit 1
    }
}
else {
    Write-Output "Offline mod.json validation passed (PowerShell 7 CI also performs schema validation)."
}
exit
