# Big Screen

<p align="center">
  <strong>Turn Beat Saber maps into synchronized video stages on Meta Quest.</strong><br>
  Go beyond a video on the back wall: build enormous flat or curved screens,
  position them anywhere, reshape the picture, and keep everything locked to the song.
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Meta%20Quest%202%20%7C%203-00b2ff">
  <img alt="Beat Saber" src="https://img.shields.io/badge/Beat%20Saber-1.40.8-orange">
  <img alt="Language" src="https://img.shields.io/badge/language-C%2B%2B20-blue">
  <img alt="Status" src="https://img.shields.io/badge/status-alpha-yellow">
  <img alt="License" src="https://img.shields.io/badge/source%20license-GPL--3.0--only-blue">
</p>

<p align="center">
  <img
    src="docs/assets/cracking-glass-effect.gif"
    alt="Big Screen playing video across a screen that cracks and shatters during Beat Saber gameplay"
    width="800">
</p>

Big Screen is a Quest-native video system for Beat Saber. It can add synchronized
video to **OST, DLC, custom, and WIP songs**, whether the mapper included one or
the player chose their own. Normal use—including YouTube downloads, local-file
selection, timing, screen setup, and playback—happens entirely inside the headset.

## The screen is part of the experience

Big Screen is not limited to placing a fixed rectangle behind the notes. Its
screen canvas and video picture can be controlled independently, making the
video part of the map's visual presentation instead of a passive background.

| Screen capability | What you can do |
|---|---|
| **Go big** | Scale flat or curved screens up to 8x for wall-sized displays and immersive wraparound layouts. |
| **Place it precisely** | Adjust distance, horizontal and vertical position, tilt, rotation, size, and curvature with sliders and fine-adjustment arrows. |
| **Undock it** | Grab, move, angle, and freely resize an advanced screen in 3D space, then save its placement. |
| **Frame the picture** | Rotate, zoom, pan, perspective-tilt, stretch, letterbox, or crop the video without changing the screen canvas. |
| **Blend it into the map** | Control video opacity and letterbox transparency, retain map lighting, or hide environmental objects and light groups that obstruct large screens. |
| **Save five layouts** | Keep five independent screen configurations and switch layouts from Big Screen or song selection. Beat Saber 1.40.8 does not currently expose Big Screen pause-menu controls. |
| **Respect authored visuals** | The current development tree can parse Cinema-authored placement, curvature, additional screens, color correction, vignette, environment selection, and object changes—or retain only mapper media/timing. This new compatibility path still requires complete on-device regression testing. |
| **Preview the map around it** | Choose no scenery, the stock menu, the selected map's full environment, or that environment driven by the preview song's lightshow while you configure the video. |

### A proof of concept for animated video choreography

The built-in **Up & Down** showcase demonstrates what the screen renderer can do
when its surfaces are animated in real time:

- Split one presentation into coordinated multi-screen formations
- Send screens toward and past the player while keeping the note lanes clear
- Build corkscrews, vortexes, tunnels, and almost any other screen formation
  you can imagine
- Bend the video with distortion and flag-wave deformation
- Crack and shatter a playing screen into hundreds of video-bearing fragments
- Synchronize screen movement, environment visibility, and transitions to the song

These showcase effects are currently a bundled proof of concept rather than a
released mapper scripting format. They demonstrate the rendering system's
potential without claiming that arbitrary maps can author the choreography yet.

To run it, open **Big Screen > Misc > Showcase**. The readiness screen checks
for Chroma, Noodle Extensions, the pinned BeatSaver map, its video, and the
downloader runtime. Missing map and video assets have separate download buttons;
the copyrighted song is not bundled in the QMOD.

## Add a video to any map

| Source | Workflow |
|---|---|
| **Mapper-provided video** | Read `bigscreen.json`, `cinema-video.json`, `video.json`, or playlist `customData.cinema`. Media/timing, geometry, color correction, vignette, and other supported presentation fields are read; map-driven bloom and soft-additive blending are currently ignored for stability. A URL-only map receives a Cinema-style download control on song selection. |
| **YouTube** | Search by song and artist in the Quest browser, paste a normal or share URL, verify its thumbnail, and choose an available 480p, 720p, 1080p, or 1440p source. |
| **Local Quest storage** | Browse readable shared-storage folders and assign a compatible MP4 or WebM without renaming or copying it. Custom and WIP maps begin in their map folder; built-in songs begin in Big Screen's Video Import folder. |

Only one downloaded source is assigned to a song at a time. Replacing it keeps
the old assignment intact until the new download commits successfully. Files
chosen through the browser remain user-owned unless the player explicitly
chooses **Delete File**. **Remove Video** opens a confirmation where **Unlink**
removes only the map assignment and leaves the original file untouched.

## Synchronized where it matters

- Beat Saber's song clock remains the playback authority.
- Preview the map audio and video together before playing.
- Adjust playback offset and speed, or use **Fit to Song** to account for the
  map duration and lead-in automatically.
- Choose a solid black or transparent lead-in and transparent or opaque
  letterboxing.
- Pause, seek, resume after the end, practice, and Replay playback retain the
  same synchronization model.
- Video previews and in-map playback have separate global switches.
- Replay playback and recording retain the video screen.

## Install a testing release with ModsBeforeFriday

Until Big Screen is listed in the ModsBeforeFriday catalog, you can install a
release manually:

1. Download the `.qmod` file from the [latest Big Screen release](https://github.com/Loud160/BigScreen/releases/latest).
2. Connect your Quest to your computer with USB and open [ModsBeforeFriday](https://mbf.bsquest.xyz/) in a compatible browser such as Chrome or Edge.
3. Connect ModsBeforeFriday to the headset and select **Upload Files**.
4. Choose the downloaded Big Screen `.qmod` file and let ModsBeforeFriday install it.
5. Start or restart Beat Saber. Big Screen will appear in the game's **Mods** menu.

`Big.Screen.qmod` is the complete install package and the only release asset
that should be uploaded to a Quest. A standalone `libbigscreen.so` is not
published because replacing it outside the QMOD can leave the installed
runtimes or manifest out of sync; developers who need that binary should build
it from source. The exact FFmpeg source archives are kept in the
[permanent FFmpeg corresponding-source release](https://github.com/Loud160/BigScreen/releases/tag/ffmpeg-sources-4.4.8-9.0.1)
for rebuilding and license compliance, not installation.

Beat Saber must already be patched for mods, and the Big Screen release must
match the Beat Saber version installed on the headset.

### Create a crash support bundle

Windows users can double-click **[Collect-BigScreen-Logs.bat](Collect-BigScreen-Logs.bat)**;
Linux users can run `./Collect-BigScreen-Logs-Linux.sh`. Both launch the same
collector after a problem. It automatically finds ADB, asks when the problem
occurred, and creates one ZIP containing freshness-labelled Big Screen, Beat
Saber, and Quest OS diagnostics. No ADB commands or manual file hunting are
required.
When **Misc > Detailed Diagnostic Logging** is enabled (the default), the ZIP
also includes the ten newest Menu sessions and ten newest Download sessions as
structured JSONL. Temporary yt-dlp media URLs, cookies, authorization values,
and tokens are omitted or redacted.
See [Troubleshooting](docs/TROUBLESHOOTING.md#collecting-a-support-bundle) for
what is collected, stale-log protection, and privacy guidance.
ADB started by the collector is stopped automatically. An ADB server that was
already running receives a five-minute **Stop ADB?** prompt that defaults to
leaving the existing session alone.

If ADB is not installed, the Windows and Linux launchers offer to download the
matching pinned Google Platform Tools package after showing its source, size,
destination, and SDK terms. The archive is SHA-256 verified before use; Windows
also verifies the extracted `adb.exe` Google signature. The portable copy
remains under `BigScreen Tools` beside the launchers; deleting that folder
removes it without an uninstaller or persistent `PATH` changes.
The console reports transferred megabytes and percentage during the download,
then announces archive verification, extraction, signature checking, and final
installation so a slow first run does not look frozen.

## Built for standalone Quest use

Big Screen supports H.264/H.265 MP4 and VP8/VP9 WebM sources. YouTube downloads
offer the compatible 480p, 720p, 1080p, and hardware-only 1440p files that the
source actually provides. Playback preserves the selected file's native
resolution, with configurable 15, 30, and 60 FPS presentation ceilings. Direct
H.264 MP4 is preferred; compatible HLS downloads are prepared as seek-safe MP4
files in the background without re-encoding or reducing quality.

For comparison and performance tuning, the mod includes:

- Android MediaCodec hardware decoding by default, with safe software fallback
  where the selected format and resolution permit it
- FFmpeg 9.0.1 by default, with an isolated FFmpeg 4.4.8 compatibility runtime
  selectable in-game for A/B testing
- Automatic Performance with separate attack/release timing, configurable FPS
  steps, and anti-oscillation protection, without reopening the decoder
- A default-on GPU conversion path that uploads reusable 8-bit
  Y/U/V planes, performs conversion and mapper picture effects in one pass,
  keeps a configurable 32–256 MiB decoded-frame reserve, and offers a separate
  experimental one-upload packed mode with visible 3-plane/CPU fallbacks
- A movable live performance panel and results summary
- Decoder latency, actual video resolution, delivered frames, video frame loss,
  Beat Saber gameplay FPS, and GPU queue-health diagnostics
- Repeatable CPU and battery benchmark CSV logging
- Safe software fallback where the codec and resolution permit it

H.265/HEVC and content above 1080p require hardware decoding. HDR and 10-bit
video are rejected with a readable explanation rather than entering an unsafe
playback path. A video failure never needs to stop the map.

## Quick start

1. Install the compatible QMOD with a Quest Beat Saber mod manager.
2. Start Beat Saber and open **Mods > Big Screen**.
3. Leave **Big Screen Enabled**, **Video In Map**, and **Preview Video** on.
4. Open **Video Library** and select any OST, DLC, custom, or WIP song.
5. Choose a mapper video, paste a YouTube URL, or select a local MP4/WebM.
6. Preview the song and adjust **Video Playback Offset**, **Playback Speed**, or
   **Fit to Song** if needed.
7. Open **Screen** to customize and save up to five layouts, then select the song
   normally.

For layouts that extend below Beat Saber's menu floor, turn off **General > Show
Menu Environment**. This hides the menu scenery, lighting, and floor while Big
Screen is open so the complete placement remains visible. **Show Lane Guides**
can independently add a thin four-lane/player-position reference; neither
setting changes gameplay environments.

### Build and deploy from source

Most players should install the published QMOD. The source tools below are for
developers and testers who intentionally want to reproduce or directly deploy
the current build.

Big Screen has one canonical x86-64 Linux build implemented in Bash and Python.
Native Linux calls it directly; Windows runs the same files inside WSL. Given
the same commit and pinned inputs, both platforms produce the same QMOD bytes
and SHA-256.

| Host and goal | Run | Quest accessed? |
|---|---|---:|
| Windows: build a QMOD for MBF or SideQuest | Double-click [`Build-QMOD.bat`](Build-QMOD.bat) | No |
| Windows: choose QMOD-only or build-and-deploy | Double-click [`Build-And-Deploy.bat`](Build-And-Deploy.bat) | Only when **Deploy** is selected |
| Linux: build a QMOD for MBF or SideQuest | `bash ./Build-QMOD-Linux.sh` | No |
| Linux: build, validate, and deploy | `bash ./Build-And-Deploy-Linux.sh` | Yes |

**Build QMOD** creates the complete `Big Screen.qmod` package for installing
Big Screen through MBF or SideQuest. It does not require ADB, inspect a
connected headset, or install the mod by itself; after the build finishes, load
the generated QMOD with MBF or SideQuest.

**Build and Deploy** creates and validates that same QMOD first, then uses ADB
to copy the verified mod and private runtime directly to a connected Quest and
restart Beat Saber. This is a source-managed development installation, not a
QMOD-manager installation. Do not use it over a QMOD already registered by MBF,
SideQuest, or another installer that writes standard package metadata.

Every build runs the host tests, builds both private FFmpeg runtimes, builds the
Quest ARM64 mod with Android NDK r27d, validates the native-library boundaries
and manifest, and creates `Big Screen.qmod` in the repository root. The
Windows and Linux build-only launchers never look for ADB or communicate with
a headset.

#### Windows one-click workflow

Clone or download the repository. For a build-only workflow matching Linux's
`Build-QMOD-Linux.sh`, double-click:

```bat
Build-QMOD.bat
```

This skips the deployment choice, excludes ADB from the audit, and cannot
access a Quest. To choose interactively between QMOD-only and direct deployment,
run:

```bat
Build-And-Deploy.bat
```

For unattended build verification, `Build-QMOD.bat --yes` approves only the
missing prerequisites disclosed by its read-only audit. It remains QMOD-only:
the option cannot enable deployment, start ADB, or access a Quest.

The launcher first asks whether to create the QMOD only or also deploy it. It
then performs a read-only audit that labels every prerequisite and cached input
as **READY** or **MISSING** before asking permission to change anything. Only
missing items are installed or downloaded. If enabling WSL or installing
Ubuntu requires elevation, the audit names that exact component before the UAC
prompt. A newly enabled WSL installation may require one Windows restart.

Compilation, tests, validation, and packaging all run inside x86-64 Ubuntu
22.04 or 24.04 under WSL. Visual Studio, Git, PowerShell 7, Windows QPM, a
Windows NDK, Docker, and 7-Zip are not required. Windows PowerShell 5.1 is used
only by the small prerequisite and optional ADB-deployment wrapper.

QMOD-only mode does not check for ADB and does not access the Quest. Deploy mode
can use an existing ADB or install a verified portable copy in `BigScreen
Tools`. It validates the Quest, Beat Saber, dependencies, and install ownership
before copying anything.

#### Linux one-click workflow

On x86-64 Ubuntu, Debian, or Linux Mint, the launcher checks the native package
set before building. If anything is missing, it lists the exact packages,
explains that `sudo` may be requested, and offers to install only those missing
packages with `apt`. The equivalent manual package command is:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential ca-certificates cmake curl ffmpeg \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  ninja-build pkg-config python3 unzip xz-utils
```

Build the QMOD without accessing a Quest:

```bash
git clone https://github.com/Loud160/BigScreen.git
cd BigScreen
bash ./Build-QMOD-Linux.sh
```

Or connect and authorize a Quest, then build and deploy in one command:

```bash
bash ./Build-And-Deploy-Linux.sh
```

The Windows and Linux deployment launchers verify the Quest connection before
the long build. If USB-debugging authorization is still pending, they explain
the headset steps and let the user retry without restarting the launcher.

Use `--clean` with either Linux build launcher to discard generated native
build output before rebuilding. Both also accept `--yes` for disclosed host,
container, and pinned dependency downloads; it never bypasses device selection,
Quest dependency checks, or ownership protection.

Immutable systems such as Bazzite and unsupported mutable distributions are
handled automatically through a reusable Ubuntu 24.04 Distrobox. The launcher
checks Distrobox and Podman, offers to install missing host tools through a
recognized package manager, creates the `bigscreen-build` container, lists and
installs only its missing Ubuntu build packages, and then continues the same
operation inside it. Bazzite normally already includes the container tools.
An immutable host that must layer a missing package may require one reboot
before the launcher can continue. Linux ARM64 remains unsupported because the
pinned QPM and Android NDK host tools are x86-64.

#### Manual Windows/WSL setup

The Windows BAT normally performs this setup. To prepare it manually, install
WSL from Administrator PowerShell:

```powershell
wsl --install -d Ubuntu-24.04
```

Install the Linux package list above inside Ubuntu. Then open the repository
from WSL and run the same Linux build-only command:

```bash
cd /mnt/c/path/to/BigScreen
bash ./Build-QMOD-Linux.sh
```

Git is convenient for cloning and updates but is not required for a downloaded
GitHub source archive. The build downloads and verifies only missing pinned
project inputs, including QPM 1.5.11, Android NDK r27d, FFmpeg, QuickJS-NG, and
the embedded downloader runtime. Valid caches are reused on later runs. The
first clean build needs internet access, several gigabytes of free space, and
time to compile both FFmpeg versions.

#### Quest dependencies and deployment protection

The QMOD declares these compatible runtime ranges. A compatible installer such
as MBF or SideQuest normally installs or updates them when it processes the
package; direct source deployment verifies that compatible registrations and
files already exist.

| Dependency | Compatible runtime version | Upstream project |
|---|---:|---|
| beatsaber-hook | `^6.4.2` | [QuestPackageManager/beatsaber-hook](https://github.com/QuestPackageManager/beatsaber-hook) |
| SongCore | `^1.1.23` | [raineaeternal/Quest-SongCore](https://github.com/raineaeternal/Quest-SongCore) |
| BSML | `^0.4.54` | [bsq-ports/Quest-BSML](https://github.com/bsq-ports/Quest-BSML) |
| custom-types | `^0.18.3` | [QuestPackageManager/Il2CppQuestTypePatching](https://github.com/QuestPackageManager/Il2CppQuestTypePatching) |

Those projects may install and use Paper2 through their own dependency
manifests. Big Screen uses its private first-party logger and does not declare,
link, initialize, replace, or globally intercept Paper2.

Deployment ignores attached phones and tablets, verifies that the selected
device is a Meta/Oculus Quest with Beat Saber installed, and asks which headset
to use if more than one eligible Quest is connected. A Big Screen QMOD already
registered by MBF—or another installer that writes the standard `Packages`
metadata—is detected by `id: bigscreen` and blocks source deployment. A raw
`.so` copied manually has no package-manager identity and is treated like an
unmanaged/source installation.

Source deployment records partial and complete ownership receipts and verifies
every copied file. Shared mod dependencies are never claimed as Big Screen
files. If the launcher started ADB, it stops it afterward; if ADB was already
running, it asks whether to stop it and defaults to No after five minutes.

#### Removing a source-deployed copy

Use [`Remove-BigScreen.bat`](Remove-BigScreen.bat) on Windows or
`bash ./Remove-BigScreen-Linux.sh` on Linux. After one confirmation, exact Big
Screen-exclusive files are removed even if their hashes changed after
deployment; hash drift cannot trap an installed source build. The remover then
asks separately whether to delete the settings file and whether to delete
videos downloaded and managed by Big Screen. Both choices default to No.

Shared dependencies, `library.json`, thumbnails, logs, maps, choreography,
map-folder videos, and files in `Video Import` are preserved. A registered QMOD
installation must still be removed through the installer that owns it.

The ordinary build embeds the committed `assets/bigscreen_video_shader` bundle.
Unity 2022.3.33f1 is required only when intentionally changing and rebuilding
the tracked shader project.

For full commands, downloads, cache locations, Bazzite/Distrobox instructions,
support tools, and failure guidance, see [Building and packaging](docs/BUILDING.md),
[Building on Linux](docs/BUILDING-LINUX.md), and
[Build dependencies](docs/DEPENDENCIES.md).

## Recovery, storage, and privacy

Atomic library writes, two rotating backups, managed-file reconstruction,
downloader rollback, task cancellation, and deferred gameplay errors protect
the user's library and keep mod failures away from active gameplay. Storage
maintenance previews every removable mod-owned file and allows individual
items to be unchecked before cleanup.

Big Screen contains no advertising, telemetry, account system, or subscription.
It contacts network services only for actions such as YouTube metadata/downloads,
thumbnail retrieval, update checks, or the user-requested showcase assets. See
[Privacy and network access](docs/PRIVACY.md) for the exact behavior.

## How it works

<details>
<summary><strong>Video and rendering path</strong></summary>

Big Screen dynamically loads two private LGPL FFmpeg runtimes behind one stable
decoder facade. Software or MediaCodec decoding produces CPU-readable frames.
The default presentation path transports supported 8-bit YUV420P/NV12 planes
and performs YUV conversion, container rotation, color correction, and vignette
in one GPU pass; the reusable CPU RGBA conversion and Unity texture-upload path
remains the automatic fallback and an in-game comparison option. Both paths
produce one ordinary shared presentation texture, preserving curved screens,
free placement, transparency,
deformations, and showcase effects regardless of decoder backend. Unsupported
layouts or failed GPU resources fall back for the rest of that playback session.
Playback workers own codec state; Unity and Beat Saber objects remain on the
game thread. Thumbnail generation remains on its existing CPU RGBA path.

The FFmpeg builds are decoder-only, use unique SONAMEs and symbol namespaces,
and are validated to exclude GPL, version-3, and nonfree components. Both build
configurations and source transformations are recorded for reproducibility.

</details>

<details>
<summary><strong>Video shader methods and Beat Saber's Bloom</strong></summary>

The current tree contains two experimental visible video-material paths:
Unity's `UI/Default` shader with RGB-only picture writes, and an embedded
`BigScreen/Video` AssetBundle shader. The bundle project is pinned to Unity
2022.3.33f1 and configures Oculus Android Multiview so required stereo variants
can be compiled.

Off selects Unity's `UI/Default` shader with RGB-only picture writes. On selects
the embedded `BigScreen/Video` shader, which provides explicit alpha blending
and depth writes. If the requested shader cannot
be loaded, the implementation follows a documented fallback ladder and records
the selected tier in the log. These source-level checks establish how the two
paths are wired; their complete current behavior with Bloom on and off still
requires the on-device matrix in
[Current development checkpoint](docs/KNOWN_ISSUES.md).

Beat Saber's bloom composite reads
the framebuffer's **alpha channel as a per-pixel emission weight**. A shader
that writes opaque alpha over the picture makes the game bloom the video into
a solid white rectangle. The embedded shader can clear that emission weight
through its separate alpha blend equation. `UI/Default` cannot, so its visible
RGB pass is followed by an invisible alpha-only guard using the embedded
shader; the guard changes no picture pixels and uses the picture's actual
opacity/vignette coverage. Big Screen's experimental Cinema bloom pre-pass and
its two diagnostic sliders remain preserved behind the default-off
`BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM` build gate.
Mapper `bloom` and `colorBlending` values therefore do not affect the active
screen. The Embedded Video Shader toggle remains available for comparing the
two visible-material paths.

</details>

<details>
<summary><strong>Standalone downloader runtime</strong></summary>

The QMOD includes CPython 3.14.7 for Android ARM64, pinned stable yt-dlp
2026.08.19,
yt-dlp-ejs 0.8.0, certifi, and QuickJS-NG 0.16.1 compiled into Big Screen.
QuickJS supplies the JavaScript challenge engine needed by modern YouTube
extraction without launching an Android executable from ModData. Termux, a
system Python or JavaScript installation, a YouTube login, and a connected PC
are not required.

Downloader updates are offered rather than installed silently. Packages are
checked against official release hashes, compatibility-tested after restart,
and rolled back when rejected. The tracked source-build recipe rebuilds yt-dlp
and yt-dlp-ejs, assembles the zip-import runtime, and compares the generated
payload with the official release shipped by Big Screen.

The Update tab separately displays the installed Big Screen version and active
yt-dlp version and channel. The nightly switch follows the package actually
loaded and changes only after a staged replacement passes startup validation;
it cannot silently disagree with the runtime. Big Screen checks both the latest public stable mod
release and the appropriate yt-dlp channel once per Beat Saber session on
background workers, without delaying the menu. Nightly yt-dlp users are told
whether stable has caught up. An older stable release remains selectable for
users who deliberately want to return, but the confirmation clearly identifies
that switch as a downgrade; stable users are never automatically moved to
nightly. Manual checks always report their result. After
three consecutive YouTube download failures, Big Screen also checks yt-dlp and
explains whether an available update may address a recent YouTube change. Mod
updates remain notification-only and are installed through ModsBeforeFriday or
the GitHub release page.

</details>

<details>
<summary><strong>Project layout</strong></summary>

```text
include/BigScreen/      Public declarations and testable core logic
src/                    Native mod, UI, playback, storage, and downloader code
tests/                  Host-side C++ and embedded-Python tests
scripts/                Reproducible dependency, build, deploy, and QMOD scripts
tools/deterministic-zip Tracked host-side deterministic ZIP/DEFLATE builder
docs/                   User, mapper, architecture, security, and build manuals
licenses/               Redistributable third-party license texts
.github/workflows/      Host tests and Quest/NDK package build
```

</details>

## Documentation

### Players

- [Installation and first run](docs/INSTALLATION.md)
- [Every setting and interaction](docs/SETTINGS.md)
- [Video Library and adding videos](docs/USER_GUIDE.md)
- [Troubleshooting, logs, and recovery](docs/TROUBLESHOOTING.md)
- [Privacy and network access](docs/PRIVACY.md)

### Mappers

- [Video metadata format and Cinema-compatible fields](docs/MAPPER_FORMAT.md)

### Developers and distributors

- [Architecture and thread ownership](docs/ARCHITECTURE.md)
- [Downloader security and rollback](docs/DOWNLOADER_SECURITY.md)
- [Building and packaging](docs/BUILDING.md)
- [Release checklist](docs/RELEASE_CHECKLIST.md)
- [Known limitations and future work](docs/FUTURE_WORK.md)
- [Current development checkpoint and required retesting](docs/KNOWN_ISSUES.md)
- [Complete documentation index](docs/README.md)

## Development status

Big Screen is an unreleased alpha. This checkpoint contains substantial recent
work and is **not** release-ready even when host tests, the ARM64 Quest build,
dependency/ELF checks, and QMOD validation pass. Everything requires a fresh
on-device regression pass; see
[current development checkpoint](docs/KNOWN_ISSUES.md) and the
[release checklist](docs/RELEASE_CHECKLIST.md).

## License

Big Screen first-party source is licensed under **GPL-3.0-only** with the
project's additional GPLv3 section 7 terms. See [LICENSE](LICENSE) and
[LICENSE-ADDITIONAL-TERMS.md](LICENSE-ADDITIONAL-TERMS.md) for the complete terms.
Contributor and dependency licensing is documented in [CONTRIBUTING.md](CONTRIBUTING.md)
and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Created by **Loud160 (AKA Whisp)**. Big Screen is an independent community
project and is not affiliated with or endorsed by Beat Games, Meta, Google,
YouTube, BeatSaver, Cinema, or Chroma.
