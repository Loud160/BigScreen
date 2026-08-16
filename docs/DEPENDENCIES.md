# Build dependencies and network downloads

This is the authoritative human-readable inventory for building Big Screen
from a fresh clone or downloaded source archive. The versioned scripts and
`qpm.json`/`qpm.shared.json` remain the machine-readable source of truth.

Big Screen's build does not upload source code, credentials, or analytics. It
does download pinned compilers, source archives, runtimes, schemas, and Quest
mod dependencies when they are missing from the local caches described below.
`Build-And-Deploy.bat` displays a summary and asks for permission before it
starts any dependency restore or build step.

## Tools you install yourself

The bootstrap script restores project dependencies, but it does not silently
install system applications or elevate itself. Install these tools first:

| Tool | Required version or capability | Used for |
|---|---|---|
| Windows PowerShell | 5.1 or newer | Runs the portable build, fetch, package, and deployment scripts. |
| Visual Studio C++ tools | Desktop development with C++, CMake 3.22+, and Ninja; equivalent standalone CMake/Ninja is also supported | Configures and compiles the Windows host tests and Android/Quest mod. |
| [QPM CLI](https://github.com/QuestPackageManager/QPM.CLI) | Validated with 1.5.11 | Restores Quest headers/libraries and manages the Windows Android NDK. |
| WSL 2 with Ubuntu or compatible Linux | `build-essential curl xz-utils unzip` | Builds the two private LGPL FFmpeg runtimes with Linux-host Android tools. |
| Android platform-tools/ADB or SideQuest | Any version compatible with the connected Quest | Required only for direct deployment, restart, logs, and device verification. Not required to compile a QMOD. |
| Git for Windows | Current supported release | Recommended for cloning and updating the repository. A downloaded source archive can also build. |

Python 3 is optional but recommended for the Python host/invariant tests. Node
24 and pnpm 11.16.0 are required only for the independent yt-dlp/yt-dlp-ejs
source-reproducibility audit; neither is used by a normal Quest build.

Inside Ubuntu/WSL, install the required Linux commands once:

```bash
sudo apt update
sudo apt install -y build-essential curl xz-utils unzip
```

## Automatically restored Quest packages

`qpm restore` reads `qpm.shared.json`, checks the existing QPM/restored cache,
and retrieves only missing or mismatched packages. The current lock contains:

| Package | Resolved version |
|---|---:|
| beatsaber-hook | 6.4.2 |
| bs-cordl | 4008.0.0 |
| BSML | 0.4.55 |
| custom-types | 0.18.4 |
| fmt | 11.0.2 |
| libil2cpp | 0.4.0 |
| paper2_scotland2 | 4.8.0 |
| rapidjson | 0.1.20250205132808+24b5e7a8 |
| Scotland2 | 0.1.7 |
| SongCore | 1.1.26 |
| tinyxml2 | 10.0.0 |

QPM resolves the package URLs and integrity metadata recorded in the lockfile.
Restored build inputs are written beneath the ignored `extern/` directory.
Runtime mod dependencies remain declared in the generated QMOD manifest.

## Automatically downloaded toolchains and runtime inputs

Each row is checked before download. A valid cached copy is reused. Direct
archives are rejected if their committed checksum does not match.

| Component | Pinned version | Official source | Local cache/output | Integrity and use |
|---|---:|---|---|---|
| Windows Android NDK | r27d (`27.3.13750724`) | QPM/Google Android repository | QPM's per-user NDK cache; selected path written to ignored `ndkpath.txt` | QPM resolves/downloads it; bootstrap verifies `source.properties` and the CMake toolchain. Build-only. |
| Linux Android NDK | r27d (`27.3.13750724`) | `https://dl.google.com/android/repository/android-ndk-r27d-linux.zip` | `~/.cache/bigscreen-toolchains/android-ndk-r27d` inside WSL | Pinned SHA-256 in `install-pinned-ndk.sh`. Build-only. |
| FFmpeg | 4.4.8 | `https://ffmpeg.org/releases/ffmpeg-4.4.8.tar.xz` | WSL cache plus `extern/ffmpeg-lgpl` outputs | Pinned SHA-256; configured as private LGPL-only decoding/scaling libraries and packaged in the QMOD. |
| FFmpeg | 9.0.1 | `https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz` | WSL cache plus `extern/ffmpeg-lgpl-9.0.1` outputs | Pinned SHA-256; configured as the comparison LGPL-only runtime and packaged in the QMOD. |
| CPython Android runtime | 3.14.7 ARM64 | `https://www.python.org/ftp/python/3.14.7/python-3.14.7-aarch64-linux-android.tar.gz` | `extern/downloader` and `build/downloader` | Pinned SHA-256 and required-file validation; runtime libraries and standard library are packaged. |
| QuickJS-NG amalgamation | 0.16.1 | `https://github.com/quickjs-ng/quickjs/releases/download/v0.16.1/quickjs-amalgam.zip` | `extern/quickjs-ng` | Pinned SHA-256; compiled into Big Screen for yt-dlp's JavaScript challenge solver. |
| yt-dlp | 2026.07.04 | `https://github.com/yt-dlp/yt-dlp/releases/download/2026.07.04/yt-dlp` | `extern/downloader` and `build/downloader` | Pinned SHA-256 plus archive-content validation; packaged as the shipped downloader baseline. |
| yt-dlp-ejs | 0.8.0 | Bundled inside the verified yt-dlp release above | Inside the yt-dlp package | Version and both solver payloads are required before packaging. No separate normal-build download. |
| certifi | 2026.7.22 | Python Package Index (`files.pythonhosted.org`) | `extern/downloader` and `build/downloader/certifi` | Pinned SHA-256; CA bundle packaged for HTTPS certificate validation. |
| QMOD JSON schema | Commit `eadb8d8d21caa1f8586b61da3c950a2953ebd399` | QuestPatcher.QMod on GitHub | `.cache/qmod-schema-<revision>.json` | Revision-pinned and SHA-256-verified schema reused by PowerShell 7 validation. Build-only. |

The exact SHA-256 values live beside the URLs in:

- `scripts/install-pinned-ndk.sh`
- `scripts/build-ffmpeg-lgpl.sh`
- `scripts/fetch-downloader-runtime.ps1`
- `scripts/fetch-quickjs-ng.ps1`

Do not update a version or checksum independently. Review the upstream release,
license, ABI, expected contents, and build configuration together.

## Building without the BAT launcher

The BAT is a convenience wrapper, not a private build path. These commands
perform the same preparation manually from the repository root.

Restore QPM dependencies and the Windows NDK:

```powershell
qpm restore
qpm ndk resolve --download
qpm doctor
```

If QPM is installed in its standard per-user directory but is not on `PATH`:

```powershell
$qpm = "$env:LOCALAPPDATA\Programs\QPM\qpm.exe"
& $qpm restore
& $qpm ndk resolve --download
& $qpm doctor
```

Install or verify the separate Linux NDK from WSL:

```bash
cd /mnt/c/path/to/BigScreen
./scripts/install-pinned-ndk.sh
```

Build and package a QMOD without a connected Quest:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build.ps1 -clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/createqmod.ps1
```

Or build and deploy after connecting and authorizing the Quest through ADB:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/copy.ps1 -clean
```

`scripts/bootstrap-build.ps1` performs only the QPM/NDK preparation used by
the BAT. It can also be run directly before either manual build command.

## Cache behavior and clean builds

A normal rerun verifies and reuses downloaded inputs. `-clean` removes the
repository's CMake build output, not the versioned download caches. `-Force`
on an individual fetch script deliberately re-downloads that script's direct
artifacts and is intended for a reviewed dependency update or cache repair.

After a successful QPM restore, the bootstrap records the SHA-256 of
`qpm.shared.json` in ignored `.cache/qpm-restore.sha256`. It skips QPM entirely
while that stamp matches and every required generated header/library remains
present. Changing the lockfile, deleting the stamp, or removing a required QPM
input causes the next approved run to restore the package set again.

Deleting ignored `extern/`, `build*`, QPM's NDK cache, or the WSL toolchain
cache makes the corresponding dependencies missing and causes the next
approved build to retrieve or rebuild them again.

## Expected upstream build warnings

A clean build currently emits several warnings from pinned upstream code. They
do not mean that Big Screen is calling a deprecated Beat Saber API:

- Android NDK r27d's own `android.toolchain.cmake` and `flags.cmake` files warn
  that their compatibility declarations for CMake versions older than 3.10
  will be removed by a future CMake release. Big Screen itself requires CMake
  3.22 or newer. The NDK remains pinned because changing the Android compiler
  is a separate compatibility and release-validation decision.
- FFmpeg 4.4.8 warns about its internal `child_class_next` field while that
  legacy comparison runtime is compiled. FFmpeg 9.0.1 is Big Screen's default;
  4.4.8 is intentionally retained only for the experimental in-game backend
  comparison and can be removed after on-device testing is complete.
- QPM-restored BSML, beatsaber-hook, custom-types, SongCore, and generated CORDL
  headers can emit unused-variable or inline-function warnings under the pinned
  NDK's Clang version. Those files are third-party/generated inputs and are not
  rewritten locally merely to suppress diagnostics.

These warnings are still visible rather than filtered out. A warning from a
future dependency update therefore cannot be silently mistaken for one of the
known messages above, and any build error still stops packaging and deployment.
