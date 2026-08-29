# Building Big Screen on Linux

Big Screen supports a native **x86-64 Linux** source build that produces the
same complete Quest QMOD assembled by the Windows and GitHub Actions paths. It
does not require Visual Studio, Windows, WSL, a connected Quest, or a system
Android SDK.

The supported build-only entrypoint is:

```bash
bash ./Build-QMOD-Linux.sh
```

Its Windows counterpart is `Build-QMOD.bat`; both are guaranteed build-only
entry points and never initialize ADB or access a Quest.

The wrapper restores pinned project dependencies, runs the complete Linux host
tests, builds the private LGPL FFmpeg 4 comparison runtime and GPL-configured
FFmpeg 9 default/transcoder runtime, builds Big Screen for Quest
ARM64 with Android NDK r27d, validates the ELF boundaries and package manifest,
and writes `Big Screen.qmod` in the repository root.

For the same one-command build-and-install workflow provided on Windows, connect
an authorized Quest and run the executable Linux launcher:

```bash
./Build-And-Deploy-Linux.sh
```

If a downloaded ZIP did not preserve executable permission bits, the portable
equivalent is `bash ./Build-And-Deploy-Linux.sh`. The launcher performs the
same complete build, then applies the same dependency, Quest-selection,
ownership-receipt, copy-verification, and Beat Saber restart policy as the
Windows workflow through the native Linux Quest tool.

## Supported hosts

The native supported host family is x86-64 Ubuntu 24.04, Debian 13, and Linux
Mint 22.x. Other x86-64 distributions are accepted natively when the complete
toolchain is already compatible. Immutable or otherwise unsupported hosts are
automatically normalized through a reusable Ubuntu 24.04 Distrobox backed by
Podman. The complete clean-build path was validated with that arrangement on
x86-64 Bazzite.

Linux ARM64 is not a native supported build host. Big Screen targets Quest
ARM64, but Google publishes NDK r27d's Linux host tools for x86-64, and QPM
1.5.11 does not publish a Linux ARM64 executable. Running an amd64 container
through emulation may work but is substantially slower and is not part of the
validated path.

The ordinary build embeds the committed `assets/bigscreen_video_shader`
bundle. Rebuilding that authored bundle after changing `tools/video-shader`
still requires Unity Editor 2022.3.33f1 on a supported Unity host.

## 1. Automatic host preparation

Run either root launcher first. Before building, it determines whether the host
can use the native path or needs the managed Ubuntu Distrobox.

- On Ubuntu, Debian, and Linux Mint it checks the package database, lists only
  missing build packages, notes that `sudo` will be used, and asks before
  installing them with `apt`.
- On Bazzite and other immutable systems it verifies Distrobox and Podman,
  names any missing host tools, and asks before using the detected package
  manager. It creates or reuses `bigscreen-build` from
  `docker.io/library/ubuntu:24.04`, audits that container's package set, and
  installs only missing build packages inside the container.
- On another mutable distribution, a complete compatible native toolchain is
  used as-is. Otherwise the same Distrobox path is selected. Host installation
  supports `apt`, `dnf`, `pacman`, and `zypper`; an OSTree package layer may
  require a reboot before rerunning the launcher.

Nothing is installed before the user sees the exact missing items and approves
the change. `--yes` can provide that approval for automation. Existing valid
containers, host packages, project archives, and toolchain caches are reused.

For manual setup on Ubuntu, Debian, or Linux Mint, install:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential ca-certificates cmake curl ffmpeg \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  ninja-build pkg-config python3 unzip xz-utils
```

The FFmpeg command and development packages above are for host tests. They are
not packaged into the Quest mod. Big Screen separately builds its two pinned,
Android-targeted FFmpeg runtimes from verified upstream source.

ADB is optional for building a QMOD and required only for direct deployment,
source removal, or support-log collection. The Linux launchers use an existing
`adb` from `PATH` or the Android SDK when available. If none is found, they can
download Google's pinned Linux Platform Tools 37.0.0 into the ignored
`BigScreen Tools` directory in this clone after showing the source, terms, and
destination and receiving approval. No system package or persistent `PATH`
entry is created.

Confirm the required host commands:

```bash
uname -m
cmake --version
ninja --version
python3 --version
```

`uname -m` must report `x86_64`, and CMake must be 3.22 or newer. Bash and
Python 3 perform the complete build; PowerShell is neither installed nor
downloaded. Git is useful for cloning, but is not required when building an
extracted GitHub source archive.

## 2. Clone and build

```bash
git clone https://github.com/Loud160/BigScreen.git
cd BigScreen
bash ./Build-QMOD-Linux.sh
```

The first run needs internet access, several gigabytes of free disk space, and
time to compile FFmpeg twice. Every long-running phase prints its purpose before
starting and leaves normal compiler progress visible. Later runs reuse verified
archives, restored QPM inputs, the NDK, and complete FFmpeg installations.

The root launcher accepts these options:

```bash
# Remove only the generated native build directory before rebuilding.
bash ./Build-QMOD-Linux.sh --clean

# Approve disclosed host/container setup and pinned project downloads.
bash ./Build-QMOD-Linux.sh --yes

```

There is deliberately no unchecked or reduced-dependency build mode. Every
supported build runs the same host and decoder tests, repository policy tests,
dependency preparation, manifest validation, ELF audit, and deterministic
packaging checks. Direct deployment adds live Quest dependency and ownership
validation after that build succeeds.

A successful build ends by printing the QMOD's absolute path, byte size, and
SHA-256. The output is:

```text
Big Screen.qmod
```

Given the same commit and pinned inputs, a clean Windows build and a clean
native Linux build produce the **same QMOD bytes and SHA-256**, not merely the
same list of files. The package writer fixes archive ordering, timestamps, and
attributes; the native build removes checkout/host paths and non-reproducible
linker build IDs. QPM's generated links are also rebuilt automatically when a
single checkout moves between Windows and Linux.

The deterministic QMOD and its nested `python314.zip` use standard ZIP DEFLATE
from the same tracked miniz 3.1.2 source on both hosts. Big Screen's small host
utility fixes entry ordering, timestamps, creator platform, and attributes.
The utility is built with the existing C/CMake toolchain, so no separate ZIP
program is installed or downloaded. The result stays in the normal 19–20 MB
range, remains an ordinary MBF-compatible QMOD ZIP, and is byte-for-byte
reproducible across Windows and Linux.

Install that complete QMOD through MBF or SideQuest. Do not install only
`libbigscreen.so`; the QMOD also carries the decoder backends, private FFmpeg
libraries, downloader runtime, notices, and shader bundle. A compatible QMOD
installer reads its manifest and installs or updates the shared Quest
dependencies listed in the main README.

`Build-QMOD-Linux.sh` intentionally does not access or modify a connected
Quest. `Build-And-Deploy-Linux.sh` is the separate, explicit source-deployment
path described below.

## Direct source deployment to a Quest

Connect the Quest over USB, enable developer mode, put on the headset, and
accept the USB-debugging authorization prompt. Make sure Big Screen is not
currently registered as a QMOD-managed package, then run:

```bash
./Build-And-Deploy-Linux.sh
```

Before compilation begins, the deployment launcher starts ADB and waits up to
30 seconds for an authorized Quest with Beat Saber. If the headset is missing
or has not accepted USB debugging, the terminal explains how to authorize the
computer and offers **Retry** or **Cancel**. Retrying does not repeat dependency
downloads or restart the launcher.

The launcher discloses its possible first-run downloads before starting. It
accepts the same clean-build control as the QMOD-only launcher:

```bash
./Build-And-Deploy-Linux.sh --clean
```

`--yes` can approve the disclosed dependency and portable-ADB downloads for a
noninteractive build, but it does not bypass Quest selection, shared-mod
dependency validation, installation-ownership checks, or other safety gates.

The source deployer verifies that the selected Android device identifies as a
Meta/Oculus Quest with Beat Saber installed. Attached phones and tablets are
ignored. If multiple eligible Quests are connected, their models and serials
are listed for an explicit numbered choice. It also verifies every shared QMOD
dependency before changing the headset. Missing or incompatible dependencies
must be installed through MBF first.

Source installs are tracked through partial and complete ownership receipts.
The deployer refuses to overwrite a registered QMOD package or ambiguous mixed
install. It copies the complete embedded runtime—not only
`libbigscreen.so`—and restarts Beat Saber after all copies verify.

When the launcher had to start ADB, it stops the server afterward. When ADB
was already active, it asks whether to stop it and defaults to No after five
minutes. Advanced automation can set `BIGSCREEN_EXISTING_ADB_ACTION=Stop` or
`Leave`; the default is `Ask`.

## Removing a source deployment

To leave source-development mode, run:

```bash
./Remove-BigScreen-Linux.sh
```

The remover uses the same ownership receipt as deployment to distinguish exact
Big Screen-exclusive paths from shared dependencies. After confirmation it
removes those private files even if their hashes changed after deployment;
hash drift cannot block uninstalling the mod. Library data, thumbnails, maps,
choreography, and logs are preserved. The remover asks separately whether to
delete settings and whether to delete videos downloaded and managed by Big
Screen; both default to No. Map-folder and Video Import videos are always
  preserved. A registered QMOD installation must be removed through the QMOD
  manager that installed it instead.

## Collecting support logs

Run the Linux support collector after a crash or other problem:

```bash
./Collect-BigScreen-Logs-Linux.sh
```

It asks how many minutes ago the incident occurred and writes a freshness-
labelled `BigScreen-Support-<date>-<time>.zip` beneath
`BigScreen Support Logs` in the clone. A time can be supplied directly:

```bash
./Collect-BigScreen-Logs-Linux.sh -SinceMinutes 120
```

The collector is read-only on the Quest and uses the same phone-safe and
multiple-Quest selection policy as deployment. See
[Troubleshooting](TROUBLESHOOTING.md#collecting-a-support-bundle) for archive
contents and privacy guidance.

## Bazzite and other immutable systems

Run the same root launcher directly from the host; do not manually layer the
compiler toolchain into Bazzite:

```bash
bash ./Build-QMOD-Linux.sh
# or, with an authorized Quest connected:
bash ./Build-And-Deploy-Linux.sh
```

The launcher detects the immutable host and performs the Distrobox/Podman and
Ubuntu-package checks described above. Bazzite normally ships Distrobox and
Podman, so its usual first run creates `bigscreen-build`, downloads the Ubuntu
24.04 image, installs the container-only build packages, and continues without
requiring the user to enter the container manually. The repository and output
remain visible on the host through Distrobox's shared-home integration.

Distrobox exposes the host's removable and USB devices to the integrated
container. The direct deployer still independently requires `adb devices -l`
to see an authorized Quest and fails before changing it if that check does not
pass. If an immutable image unusually lacks Distrobox or Podman, the launcher
offers the host package operation; OSTree may require one reboot before those
new commands are available.

## What the scripts download

The scripts do not upload source or send telemetry. Before invoking `sudo` or a
host package manager, they list the exact missing host packages and require
approval. On immutable or unsupported hosts they may also download the declared
Ubuntu 24.04 container image and install the disclosed packages inside that
container. Project-specific downloads retrieve only missing or invalid pinned
inputs:

- QPM CLI 1.5.11 for Linux x86-64 from its official GitHub release;
- Android NDK r27d (`27.3.13750724`) from Google's Android repository;
- Google Android SDK Platform Tools 37.0.0 for Linux, only when an ADB-enabled
  launcher is used and no existing ADB can be found;
- QPM dependencies locked by `qpm.shared.json`;
- FFmpeg 4.4.8 and 9.0.1 source from ffmpeg.org;
- pinned x264 source for FFmpeg 9's last-resort software H.264 encoder;
- the pinned Android CPython, yt-dlp, certifi, QuickJS-NG, and validation
  artifacts documented in [DEPENDENCIES.md](DEPENDENCIES.md).

Direct archives are SHA-256 verified before use. Project-owned toolchains are
cached under `~/.cache/bigscreen-toolchains`, FFmpeg sources/build trees under
`~/.cache/bigscreen-ffmpeg`, and portable runtime inputs under the repository's
ignored `.cache/dependencies` directory. Set `BIGSCREEN_TOOLCHAIN_ROOT` or
`BIGSCREEN_FFMPEG_CACHE` before launching the build to select different native
Linux cache locations.

## Lower-level commands

The friendly launcher delegates to these independently callable stages:

```bash
# Restore pinned QPM inputs and install/verify the Linux NDK.
bash ./scripts/bootstrap-linux.sh

# Run the complete native Linux host test pass.
bash ./scripts/test-linux.sh

# Run bootstrap, tests, FFmpeg builds, the Quest build, and QMOD packaging.
bash ./scripts/build-linux.sh
```

These wrappers call the canonical `scripts/build_pipeline.py`, the FFmpeg
builder, and the pinned QPM/NDK bootstrap. Windows/WSL invokes these exact same
files, so Linux is not a second recipe with different validation or packaging.

## Common failures

- **Missing command:** run a root one-click launcher so it can audit and offer
  the supported package installation. Lower-level scripts report missing
  commands but never change host packages.
- **Wrong architecture:** use an x86-64 Linux host or GitHub Actions. ARM64 host
  emulation is outside the supported path.
- **QPM restore failure:** confirm network access and rerun. A verified QPM
  archive and complete restored packages are reused. When one checkout is
  shared between Windows and Linux, rerun the normal bootstrap rather than
  editing `extern/` or `ndkpath.txt`; it detects the host change and safely
  replaces only QPM's ignored host-specific links.
- **NDK checksum or revision failure:** do not change the pinned checksum to
  bypass it. Remove the reported cached archive and let the script retrieve the
  official r27d package again.
- **FFmpeg build interruption:** rerun the same command. Only a complete,
  validated installation receives the ready stamp used by later builds.
- **Shader source is newer than the committed bundle:** Linux can build the
  existing bundle into the QMOD but cannot reproduce a newly edited Unity
  AssetBundle without the exact Unity editor workflow.
