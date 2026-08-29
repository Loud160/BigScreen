# Build dependencies and network downloads

This is the authoritative human-readable inventory for building Big Screen
from a fresh clone or downloaded source archive. The versioned scripts and
`qpm.json`/`qpm.shared.json` remain the machine-readable source of truth.

Big Screen's build does not upload source code, credentials, or analytics. It
does download pinned compilers, source archives, runtimes, schemas, and Quest
mod dependencies when they are missing from the local caches described below.
The Windows launcher performs a read-only inventory first, marking every host
prerequisite and pinned project input as `READY` or `MISSING` before asking for
permission. The Linux launcher audits the native or managed-container host,
lists exact missing packages, and asks before installing them. It also discloses
all possible project downloads.

## Host tools and official setup sources

On Windows, `Build-QMOD.bat` provides the build-only counterpart to the Linux
QMOD launcher. `Build-And-Deploy.bat` retains the interactive QMOD/deploy
choice. Both can install missing WSL/Ubuntu host prerequisites after the audit
and explicit consent. If elevation is necessary, the audit names the exact WSL
or Ubuntu component that will trigger UAC. Manual builders and native Linux
users can use these official sources:

Build QMOD is the package path for installation through MBF or SideQuest; it
does not require ADB or touch a connected headset. Build and Deploy creates the
same validated package first, then performs a source-managed ADB installation.

The launcher selects QMOD-only or direct-deployment mode before the audit.
QMOD-only mode excludes ADB and all Quest access. Direct deployment keeps the
existing refusal for a registered QMOD package, mixed ownership, or another
ambiguous Big Screen installation; dependency automation never bypasses that
safety gate.

| Tool | Required version or capability | Used for |
|---|---|---|
| Windows PowerShell | 5.1+ is included with supported Windows releases | Runs the audit and Windows-side deployment scripts. |
| [WSL 2 with Ubuntu](https://learn.microsoft.com/windows/wsl/install) | x86-64 Ubuntu 22.04 or 24.04 | Hosts the supported Visual-Studio-free Windows build. Manual command: `wsl --install -d Ubuntu-24.04`. |
| Ubuntu host packages | Install with the command below | Supplies the compiler, CMake, Ninja, FFmpeg test libraries, Python, and archive/network tools. |
| [Android Platform Tools/ADB](https://developer.android.com/tools/releases/platform-tools) or SideQuest | Any version compatible with the connected Quest | Required only for direct deployment, restart, logs, and device verification. The launcher can instead install its pinned portable copy. |
| [Git for Windows](https://git-scm.com/download/win) | Current supported release | Recommended for cloning and updating. A downloaded GitHub source archive does not require Git. |
| [Unity Editor](https://unity.com/releases/editor/archive) | 2022.3.33f1 exactly | Required only after intentionally changing the shader project. The committed bundle is sufficient for an ordinary clean QMOD build. |

Visual Studio, Git, PowerShell 7, Windows QPM, a Windows Android NDK, Docker,
and 7-Zip are not required by the root Windows BAT. There is no separate
Windows-native compile/package recipe: Windows enters the same Linux build
used by native Linux and CI.

Native x86-64 Ubuntu, Debian, and Linux Mint builds use `build-essential`,
CMake 3.22+, Ninja, curl, XZ/unzip support, Python 3, pkg-config, and the host
FFmpeg command/development packages. Missing Debian-family packages can be
installed by the root launcher after explicit approval. Immutable or otherwise
unsupported x86-64 hosts use a reusable Ubuntu 24.04 Distrobox; the launcher
checks and can offer installation of Distrobox/Podman through `apt`, `dnf`,
`pacman`, or `zypper`, then installs only missing packages inside the container.
An OSTree host package change may require one reboot. Complete details and
manual commands are documented in [Building on Linux](BUILDING-LINUX.md).
It downloads a private, hash-verified QPM 1.5.11 binary rather than requiring a
system-wide QPM install. Linux ARM64 is not a native supported host because the
pinned QPM and Android NDK Linux tools are x86-64.

When Linux ADB is not already present, the ADB-enabled launchers can download
Google Platform Tools 37.0.0 for Linux to the repository's ignored
`BigScreen Tools/platform-tools` directory. The 8.7 MB official archive is
checked against the pinned SHA-256 before extraction and reused from its local
cache; no system package or persistent `PATH` change is made.

Python 3 is required by the canonical build and repository tests. Node
24 and pnpm 11.16.0 are required only for the independent yt-dlp/yt-dlp-ejs
source-reproducibility audit; neither is used by a normal Quest build.

The complete small Unity shader project is versioned under
`tools/video-shader`, including its `Packages`, `ProjectSettings`, and XR asset
configuration. `Build`, `Library`, `Logs`, `Temp`, IDE files, and `UserSettings`
are generated locally and intentionally ignored.

Reproducible packaging compiles a small host utility from the tracked miniz
3.1.2 source under `tools/deterministic-zip`. The same deterministic raw
DEFLATE implementation, ordinal entry ordering, and fixed ZIP32 metadata are
used on Windows and Linux. The executable is cached under
`.cache/build-tools/deterministic-zip`, is never packaged into the QMOD, and
does not change MBF's normal ZIP handling. It uses the C/CMake toolchain already
required for source builds; there is no extra archive program to install or
download.

Inside Ubuntu/WSL, install the required Linux packages once:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential ca-certificates cmake curl ffmpeg \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  ninja-build pkg-config python3 unzip xz-utils
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
Paper2 4.8.0 remains in the resolved build lock only because beatsaber-hook,
BSML, SongCore, and Custom Types consume Paper headers or runtime logging under
their own dependency contracts. It is not a direct `qpm.json` dependency and
does not appear in Big Screen's generated QMOD manifest. The final link removes
Paper2 from `libbigscreen.so` while a hidden link-time bridge redirects only
beatsaber-hook abort-helper references originating inside Big Screen to the
first-party logger. Other shared objects continue using the real Paper2
library selected by their manifests.

Direct source deployment cannot ask a QMOD manager to resolve packages. After
the local build has passed, the Windows `scripts/copy.ps1` path and Linux
`scripts/quest_tool.py deploy` path read the generated QMOD requirements and
inspect the selected Quest's package registrations and payload files. Missing,
outdated, unregistered, or incomplete shared dependencies stop deployment
before any Big Screen file changes. Install or update the reported dependency
through MBF, then rerun the launcher.

At runtime, Big Screen performs one dependency audit on the first stable menu
update after Scotland2 finishes its late-mod phase. Registered mods use their
live Scotland2 identity; anonymous library-phase dependencies use the QMOD
manifest installed for this Beat Saber version. This is startup-only work with
no polling or ongoing frame-path cost. A dependency that is present but below
the QMOD's minimum is recorded in plain language and queued as an in-game
dialog. The audit reads installed dependency manifests, not Big Screen's QMOD
archive, so direct `.bat`/`.sh` source deployments use the same runtime path.
An unavailable manifest is diagnostic evidence only: it never blocks startup
or creates a false downgrade dialog. The direct deployment scripts separately
require compatible package registrations before copying any Big Screen file.
If dependency resolution or Android's dynamic linker prevents Big
Screen from loading at all, no code inside the mod can create that dialog.
Both support-log launchers therefore generate a top-level
`DEPENDENCY-DIAGNOSIS.txt` independently from the Quest package registrations
and payload files.

## Automatically downloaded toolchains and runtime inputs

Each row is checked before download. A valid cached copy is reused. Direct
archives are rejected if their committed checksum does not match.

| Component | Pinned version | Official source | Local cache/output | Integrity and use |
|---|---:|---|---|---|
| Linux Android NDK | r27d (`27.3.13750724`) | `https://dl.google.com/android/repository/android-ndk-r27d-linux.zip` | `~/.cache/bigscreen-toolchains/android-ndk-r27d` inside WSL | Pinned SHA-256 in `install-pinned-ndk.sh`. Build-only. |
| FFmpeg comparison runtime | 4.4.8 | `https://ffmpeg.org/releases/ffmpeg-4.4.8.tar.xz` | WSL source cache plus `.cache/dependencies/ffmpeg-lgpl` outputs | Pinned SHA-256; configured as isolated LGPL-only decoding/scaling libraries and packaged for the experimental runtime comparison toggle. |
| FFmpeg default runtime | 9.0.1 | `https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz` | WSL source cache plus `.cache/dependencies/ffmpeg-lgpl-9.0.1` outputs | Pinned SHA-256; configured as the default isolated GPL runtime with MediaCodec decoding/encoding and the pinned x264 software-encode fallback. Packaged in the QMOD. |
| x264 software H.264 encoder | commit `b35605ace3ddf7c1a5d67a2eb553f034aef41d55` | `https://github.com/mirror/x264/archive/b35605ace3ddf7c1a5d67a2eb553f034aef41d55.tar.gz` | Linux/WSL source cache; statically linked into the FFmpeg 9 `libavcodec` runtime | Pinned SHA-256; GPL-2.0-or-later; 8-bit 4:2:0 library build with CLI/OpenCL disabled. Used only after the player approves last-resort conversion and Android's hardware path cannot complete it. |
| CPython Android runtime | 3.14.7 ARM64 | `https://www.python.org/ftp/python/3.14.7/python-3.14.7-aarch64-linux-android.tar.gz` | `.cache/dependencies/downloader` and `build/downloader` | Pinned SHA-256 and required-file validation; runtime libraries and standard library are packaged. |
| QuickJS-NG amalgamation | 0.16.1 | `https://github.com/quickjs-ng/quickjs/releases/download/v0.16.1/quickjs-amalgam.zip` | `.cache/dependencies/quickjs-ng` | Pinned SHA-256; compiled into Big Screen for yt-dlp's JavaScript challenge solver. |
| miniz deterministic compressor source | 3.1.2 | Tracked snapshot from `https://github.com/richgel999/miniz/releases/tag/3.1.2` | `tools/deterministic-zip/vendor/miniz-3.1.2`; compiled utility cached under `.cache/build-tools/deterministic-zip` | MIT-licensed source and notice are included in the repository. Build-only; produces standard ZIP/DEFLATE streams for `python314.zip` and the QMOD, and is not packaged. No separate download or installation. |
| yt-dlp | stable 2026.08.19 | `https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp` | `.cache/dependencies/downloader` and `build/downloader` | Pinned SHA-256 plus archive-content validation. Stable 2026.08.19 contains the YouTube recovery that temporarily required nightly 2026.08.18.122307. |
| yt-dlp-ejs | 0.8.0 | Bundled inside the verified yt-dlp release above | Inside the yt-dlp package | Version and both solver payloads are required before packaging. No separate normal-build download. |
| certifi | 2026.7.22 | Python Package Index (`files.pythonhosted.org`) | `.cache/dependencies/downloader` and `build/downloader/certifi` | Pinned SHA-256; CA bundle packaged for HTTPS certificate validation. |
| QMOD JSON schema | Commit `eadb8d8d21caa1f8586b61da3c950a2953ebd399` | QuestPatcher.QMod on GitHub | `.cache/qmod-schema-<revision>.json` | Revision-pinned and SHA-256-verified; the canonical Python validator enforces every schema rule used by Big Screen's manifest shape without adding a package-manager dependency. Build-only. |

The exact SHA-256 values live beside the URLs in:

- `scripts/install-pinned-ndk.sh`
- `scripts/build-ffmpeg-lgpl.sh`
- `scripts/build_pipeline.py`

Do not update a version or checksum independently. Review the upstream release,
license, ABI, expected contents, and build configuration together.

## Building without the BAT launcher

The BAT is a convenience wrapper, not a private build path. These commands
perform the same preparation manually from the repository root.

Install WSL and Ubuntu manually from Administrator PowerShell if needed:

```powershell
wsl --install -d Ubuntu-24.04
```

Use the Ubuntu package command in the first section of this document. Then
enter the repository through Ubuntu/WSL and run:

```bash
cd /mnt/c/path/to/BigScreen
bash ./Build-QMOD-Linux.sh --clean
```

That wrapper downloads the pinned QPM binary from its
[official releases](https://github.com/QuestPackageManager/QPM.CLI/releases),
the pinned NDK from [Google's Android repository](https://developer.android.com/ndk/downloads),
and the remaining exact artifacts from the official URLs in the table above.
Each cache is checked first; the scripts should perform these downloads so the
committed versions and SHA-256 values cannot be accidentally bypassed.

To deploy the verified build directly from Windows, connect and authorize the
Quest, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/ensure-adb.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/copy.ps1 -UseExistingVerifiedBuild
```

The supported native Linux equivalent is:

```bash
./Build-And-Deploy-Linux.sh --clean
```

`scripts/bootstrap-linux.sh` performs the pinned Linux QPM/NDK preparation used
by the Windows/WSL and native Linux launchers. It can also be run directly by
advanced users after the operating-system prerequisites are present.

## Cache behavior and clean builds

A normal rerun verifies and reuses downloaded inputs. Big Screen's portable
dependency cache lives under `.cache/dependencies` inside the repository so it
moves with a complete working folder but remains separate from QPM's generated
`extern/` directory. QPM may replace `extern/` during a restore; it cannot
delete the private FFmpeg, QuickJS, CPython, yt-dlp, or certifi caches.

`--clean` removes the repository's CMake build output, not the versioned
download caches. `-Force`
on an individual fetch script deliberately re-downloads that script's direct
artifacts and is intended for a reviewed dependency update or cache repair.

After a successful QPM restore, the bootstrap records the SHA-256 of
`qpm.shared.json` in ignored `.cache/qpm-restore.sha256`. It skips QPM entirely
while that stamp matches and every required generated header/library remains
present. Changing the lockfile, deleting the stamp, or removing a required QPM
input causes the next approved run to restore the package set again.

Deleting ignored `.cache/dependencies`, QPM's NDK cache, or the WSL toolchain
cache makes the corresponding dependencies missing and causes the next
approved build to retrieve or rebuild them again. Deleting `extern/` alone
causes a QPM restore but no longer discards Big Screen's private downloads.

## Expected upstream diagnostics

Pinned upstream code may contain diagnostics that do not mean Big Screen is
calling a deprecated Beat Saber API:

- Android NDK r27d's own `android.toolchain.cmake` and `flags.cmake` files warn
  that their compatibility declarations for CMake versions older than 3.10
  will be removed by a future CMake release. Big Screen itself requires CMake
  3.22 or newer. The NDK remains pinned because changing the Android compiler
  is a separate compatibility and release-validation decision.
- FFmpeg 4.4.8 contains legacy C that produces extensive warnings under NDK
  r27d. Its third-party compile uses `-w` so those known upstream warnings do
  not obscure Big Screen's own diagnostics. FFmpeg configure failures,
  missing-feature checks, compiler errors, link failures, integrity checks,
  and ELF validation remain active.
- QPM-restored BSML, beatsaber-hook, custom-types, SongCore, and generated CORDL
  headers can emit unused-variable or inline-function warnings under the pinned
  NDK's Clang version. Those third-party/generated inputs are not rewritten
  locally merely to silence a warning.

Big Screen's first-party code still builds with `-Wall`, `-Wextra`, and
`-Wpedantic`; its warnings remain visible. Any build error still stops
packaging and deployment.

## Checkout path compatibility

The repository may be cloned or extracted beneath a Windows path containing
spaces and parentheses, including GitHub's common `BigScreen-main (1)` download
name. FFmpeg is configured and installed first under a path-safe native WSL
cache because FFmpeg 4 writes its installation prefix into generated shell
fragments without consistently quoting it. Only the completed runtime is then
copied into the checkout's ignored `extern/` staging directory.
