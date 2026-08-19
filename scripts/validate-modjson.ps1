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
        $qpmExecutable = if ($qpmCommand) { $qpmCommand.Source } else { $null }
        if (-not $qpmExecutable -and $env:LOCALAPPDATA) {
            # QPM's Windows installer does not always add its install folder to
            # PATH. Resolve the standard per-user location without embedding a
            # developer-specific absolute path in the repository.
            $installedQpm = Join-Path $env:LOCALAPPDATA "Programs/QPM/qpm.exe"
            if (Test-Path -LiteralPath $installedQpm) {
                $qpmExecutable = $installedQpm
            }
        }
        if ($qpmExecutable) {
            & $qpmExecutable qmod manifest
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
        }
        elseif (-not (Test-Path -Path $mod)) {
            Write-Output "Error: qpm is required to create the initial mod.json"
            exit 1
        }
        else {
            Write-Output "qpm is unavailable; using the existing mod.json only after checking it against the tracked package metadata."
        }
    }
}
elseif (-not (Test-Path -Path $mod)) {
    Write-Output "Error: mod.json and mod.template.json were not present"
    exit 1
}

Write-Output "Creating qmod from mod.json"

# JSON permits Unicode text, but Mods Before Friday's manifest importer expects
# the first byte to be `{` and rejects a UTF-8 BOM as an invalid value at line 1
# column 1. Validate the actual bytes rather than relying on PowerShell's
# BOM-tolerant ConvertFrom-Json parser.
$resolvedMod = (Resolve-Path $mod).Path
$modBytes = [System.IO.File]::ReadAllBytes($resolvedMod)
if ($modBytes.Length -ge 3 -and
    $modBytes[0] -eq 0xEF -and
    $modBytes[1] -eq 0xBB -and
    $modBytes[2] -eq 0xBF) {
    Write-Output "Error: mod.json must be UTF-8 without a byte-order mark for Mods Before Friday compatibility."
    exit 1
}

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

# A stale mod.json can still be structurally valid while targeting a different
# Beat Saber APK and dependency ABI. This previously allowed a 1.40 native
# build to be packaged as 1.37. Require every identity/version field owned by
# the template to match before any QMOD archive is created.
$template = Get-Content $modTemplate -Raw | ConvertFrom-Json -ErrorAction Stop
$shared = if (Test-Path -LiteralPath $qpmShared) {
    Get-Content $qpmShared -Raw | ConvertFrom-Json -ErrorAction Stop
} else {
    $null
}
foreach ($property in @("name", "id", "author", "version", "packageId", "packageVersion")) {
    $expected = [string]$template.$property
    if ($expected -eq '${mod_name}' -and $shared) {
        $expected = [string]$shared.config.info.name
    }
    elseif ($expected -eq '${mod_id}' -and $shared) {
        $expected = [string]$shared.config.info.id
    }
    if ([string]$parsed.$property -ne $expected) {
        Write-Output "Error: mod.json field '$property' is stale (expected '$expected', found '$($parsed.$property)'). Run 'qpm qmod manifest'."
        exit 1
    }
}

# QPM derives QMOD dependencies from restored packages that publish a modLink,
# except dependencies explicitly marked includeQmod=false. Validate the set so
# an old Paper/BSML/SongCore manifest cannot accompany newer game bindings.
if (Test-Path -LiteralPath $qpmShared) {
    $expectedDependencyIds = @()
    foreach ($configured in $shared.config.dependencies) {
        $includeQmod = $true
        if ($configured.additionalData -and
            $configured.additionalData.PSObject.Properties["includeQmod"]) {
            $includeQmod = [bool]$configured.additionalData.includeQmod
        }
        if (-not $includeQmod) {
            continue
        }
        $restored = $shared.restoredDependencies | Where-Object {
            $_.dependency.id -eq $configured.id
        } | Select-Object -First 1
        # modLink is optional QPM metadata. Access it through the property bag
        # so validation remains correct under StrictMode as well as in the
        # ordinary standalone invocation.
        $additionalData = if ($restored -and $restored.dependency) {
            $restored.dependency.PSObject.Properties["additionalData"]
        } else { $null }
        $modLink = if ($additionalData -and $additionalData.Value) {
            $additionalData.Value.PSObject.Properties["modLink"]
        } else { $null }
        if ($modLink -and
            -not [string]::IsNullOrWhiteSpace([string]$modLink.Value)) {
            $expectedDependencyIds += [string]$configured.id
        }
    }
    $actualDependencyIds = @($parsed.dependencies | ForEach-Object { [string]$_.id })
    $dependencyDifference = Compare-Object `
        ($expectedDependencyIds | Sort-Object -Unique) `
        ($actualDependencyIds | Sort-Object -Unique)
    if ($dependencyDifference) {
        Write-Output "Error: mod.json dependencies do not match qpm.shared.json. Run 'qpm qmod manifest'."
        Write-Output ($dependencyDifference | Format-Table -AutoSize | Out-String)
        exit 1
    }
}

$psVersion = $PSVersionTable.PSVersion.Major
if ($psVersion -ge 6) {
    # Pin the schema revision so upstream branch changes cannot silently alter
    # release validation semantics.
    $schemaRevision = "eadb8d8d21caa1f8586b61da3c950a2953ebd399"
    $schemaSha256 = "2de429724eae87554700b9eee31380fdd38a27afe135db0c2a124d5268e4c2ec"
    $schemaUrl = "https://raw.githubusercontent.com/Lauriethefish/QuestPatcher.QMod/$schemaRevision/QuestPatcher.QMod/Resources/qmod.schema.json"
    $schemaCache = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) ".cache"
    $schema = Join-Path $schemaCache "qmod-schema-$schemaRevision.json"
    if (-not (Test-Path -LiteralPath $schema)) {
        New-Item -ItemType Directory -Force -Path $schemaCache | Out-Null
        Write-Output "Downloading the pinned QMOD validation schema from GitHub."
        Write-Output "Source: $schemaUrl"
        Invoke-WebRequest $schemaUrl -OutFile $schema
    }
    else {
        Write-Output "Using cached QMOD validation schema at revision $schemaRevision."
    }
    $actualSchemaSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $schema).Hash.ToLowerInvariant()
    if ($actualSchemaSha256 -ne $schemaSha256) {
        throw "SHA-256 mismatch for cached QMOD schema. Expected $schemaSha256, received $actualSchemaSha256."
    }

    $modJsonRaw = Get-Content $mod -Raw
    $modSchemaRaw = Get-Content $schema -Raw

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
