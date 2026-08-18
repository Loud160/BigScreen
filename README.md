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

> **Current development warning (August 18, 2026):** this repository is a
> preservation checkpoint containing substantial recent shader, screen, and
> Cinema-compatibility work. Its current on-device behavior has not been
> independently established by this documentation audit, and the complete mod
> must be retested on Quest before it is treated as a release candidate. See
> [Current development checkpoint](docs/KNOWN_ISSUES.md).

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
| **Mapper-provided video** | Read `bigscreen.json`, `cinema-video.json`, `video.json`, or playlist `customData.cinema`. Media/timing fields and a substantial subset of PC Cinema presentation fields are implemented, including the mapper `bloom` glow intensity. The new presentation path remains under Quest testing. A URL-only map receives a Cinema-style download control on song selection. |
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

Beat Saber must already be patched for mods, and the Big Screen release must
match the Beat Saber version installed on the headset.

### Create a crash support bundle

Windows users can double-click **[Collect-BigScreen-Logs.bat](Collect-BigScreen-Logs.bat)**
after a problem. It automatically finds ADB, asks when the problem occurred,
and creates one ZIP containing freshness-labelled Big Screen, Beat Saber, and
Quest OS diagnostics. No ADB commands or manual file hunting are required.
See [Troubleshooting](docs/TROUBLESHOOTING.md#collecting-a-support-bundle) for
what is collected, stale-log protection, and privacy guidance.
ADB started by the collector is stopped automatically. An ADB server that was
already running receives a five-minute **Stop ADB?** prompt that defaults to
leaving the existing session alone.

If ADB is not installed, the BAT offers to download Google's pinned Windows
Platform Tools package after showing its source, size, destination, and SDK
terms. The archive and extracted `adb.exe` are verified before use. The
approximately 17 MB portable copy remains under `BigScreen Tools` beside the
BAT; deleting that folder removes it without an uninstaller or `PATH` changes.
The console reports transferred megabytes and percentage during the download,
then announces archive verification, extraction, signature checking, and final
installation so a slow first run does not look frozen.

## Built for standalone Quest use

Big Screen supports H.264/H.265 MP4 and VP8/VP9 WebM sources. YouTube downloads
offer the compatible 480p, 720p, 1080p, and hardware-only 1440p files that the
source actually provides. Playback preserves the selected file's native
resolution, with configurable 15, 30, and 60 FPS presentation ceilings.

For comparison and performance tuning, the mod includes:

- Android MediaCodec hardware decoding by default, with safe software fallback
  where the selected format and resolution permit it
- FFmpeg 9.0.1 by default, with an isolated FFmpeg 4.4.8 compatibility runtime
  selectable in-game for A/B testing
- Automatic Performance with separate attack/release timing, configurable FPS
  steps, and anti-oscillation protection, without reopening the decoder
- A movable live performance panel and results summary
- Decoder latency, actual video resolution, delivered frames, video frame loss,
  and Beat Saber gameplay FPS diagnostics
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

### Build and deploy from source on Windows

There are two supported Windows workflows. The standalone batch launcher is
the simplest way to build and install Big Screen. The manual workflow below it
exposes every preparation, build, packaging, test, and deployment command.

#### Method 1 — Standalone `Build-And-Deploy.bat`

Install [QPM CLI](https://github.com/QuestPackageManager/QPM.CLI), Visual Studio
with **Desktop development with C++** and **C++ CMake tools for Windows**, and
WSL 2 with Ubuntu. Enable Quest developer mode, connect the headset over USB,
put it on, and accept the USB debugging authorization prompt.

The first time Ubuntu opens, install the small Linux toolchain used to compile
FFmpeg. The batch launcher handles the pinned Linux NDK itself:

```bash
sudo apt update
sudo apt install -y build-essential curl xz-utils unzip
```

Clone or download this repository, then double-click
**[Build-And-Deploy.bat](Build-And-Deploy.bat)** in its root folder. It can also
be launched from Command Prompt or PowerShell:

```bat
Build-And-Deploy.bat
```

The launcher first lists the dependencies it may download and waits for
permission. It checks what is already installed or cached and downloads only
missing inputs. It then restores QPM packages, resolves both pinned Android NDK
installations, builds the two private FFmpeg runtimes and embedded downloader,
builds and validates Big Screen, creates the QMOD, removes stale copies from the
opposite Scotland2 load phase, installs the complete runtime, and asks Beat
Saber to restart. The console remains open and reports either success or the
exact failed step.

The deploy step also keeps the experimental embedded video shader bundle
(`assets/bigscreen_video_shader`) current: when any source under
`tools/video-shader/` has changed, it rebuilds the bundle automatically with
Unity **2022.3.33f1** (the exact engine Beat Saber 1.40.8 ships) and refuses to
deploy a stale bundle. Unity is only required when those shader sources have
changed or appear newer than the bundle; `build.ps1`/`createqmod.ps1` use the
committed bundle and do not launch Unity. The
complete Unity project inputs—including `Packages`, `ProjectSettings`, and XR
assets—are versioned so a clean clone can reproduce the bundle. After the game
restarts, the console prints the selected shader tier and archives that boot's
Big Screen log lines to `diagnostics/last-deploy-bigscreen.log`. That tier is a
diagnostic only; it does not prove that the screen rendered correctly on-device.

After deployment, ADB is stopped automatically when the launcher started it.
If ADB was already active, the launcher asks whether to stop it and defaults to
**No** after five minutes so it cannot silently disrupt an existing session.
When ADB is missing, this launcher uses the same disclosed, verified portable
Platform Tools download as the crash-log collector.

The first clean run needs internet access, several gigabytes of temporary
space, and time to compile FFmpeg. Later runs reuse hash-verified caches. The
launcher does not upload source or telemetry.

#### Method 2 — Manual build from source without the batch launcher

You do not need previous Beat Saber mod-development experience, but the manual
workflow requires the following tools:

| Tool | Why Big Screen needs it |
|---|---|
| **Git for Windows** | Clones the repository and preserves its versioned build recipes. |
| **Visual Studio with Desktop development with C++** | Supplies the Windows C++ compiler used by host tests. Include **C++ CMake tools for Windows**, or install CMake 3.22+ and Ninja separately. |
| **QPM CLI** | Restores the exact Quest headers/libraries recorded in `qpm.shared.json` and downloads the Windows Android NDK. This port was validated with QPM 1.5.11. |
| **WSL 2 with Ubuntu** | Builds Big Screen's private LGPL FFmpeg libraries in a Linux environment. |
| **Android NDK r27d (`27.3.13750724`)** | QPM manages the Windows copy used for the mod; `scripts/install-pinned-ndk.sh` installs the matching Linux copy used by FFmpeg inside WSL. Do not substitute another revision. |
| **Android platform-tools/ADB or SideQuest** | Required only for direct deployment to a connected Quest; it is not required to build the QMOD. |
| **Unity Editor 2022.3.33f1** | Required only after changing the experimental shader project; ordinary clean builds use the committed bundle. |

Windows PowerShell 5.1 is already included with supported Windows versions.
Python 3 is optional but recommended because it enables the downloader and
repository-invariant host tests. Node.js/pnpm are needed only for the separate
yt-dlp/yt-dlp-ejs source-reproducibility audit, not for a normal mod build.

Clone the repository, restore its exact Quest dependencies, and resolve the
pinned Windows Android NDK from PowerShell:

```powershell
git clone https://github.com/Loud160/BigScreen.git
cd BigScreen
qpm restore
qpm ndk resolve --download
qpm doctor
```

If QPM was installed in its normal per-user directory but was not added to
`PATH`, use its full environment-relative path:

```powershell
$qpm = "$env:LOCALAPPDATA\Programs\QPM\qpm.exe"
& $qpm restore
& $qpm ndk resolve --download
& $qpm doctor
```

Install WSL 2 from an Administrator PowerShell window if necessary:

```powershell
wsl --install -d Ubuntu
```

After Windows completes the WSL installation, open Ubuntu and prepare the Linux
FFmpeg toolchain:

```bash
sudo apt update
sudo apt install -y build-essential curl xz-utils unzip
cd /mnt/c/path/to/BigScreen
./scripts/install-pinned-ndk.sh
```

Replace `/mnt/c/path/to/BigScreen` with the clone's actual WSL path. The Windows
build script invokes WSL automatically if either pinned FFmpeg runtime has not
already been staged.

Run the host tests, build the Quest libraries, and create the QMOD from
PowerShell in the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build.ps1 -clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/createqmod.ps1
```

The resulting `Big Screen.qmod` can be installed with a compatible Quest Beat
Saber mod manager. Building it does not require a connected headset.

To build and deploy manually without using the batch launcher, connect and
authorize the Quest, make sure `adb` is available, and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/copy.ps1
```

See **[Build dependencies and network downloads](docs/DEPENDENCIES.md)** for
the complete versioned inventory, official sources, cache locations, integrity
checks, packaged/build-only distinctions, and lower-level manual commands. See
[Building and packaging](docs/BUILDING.md) for clean builds, host tests, QMOD
creation, toolchain details, and troubleshooting.

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
decoder facade. Software or MediaCodec decoding produces CPU-readable frames,
which pass through the same crop, color-range, reusable RGBA conversion, and
Unity texture-upload path. This preserves curved screens, free placement,
transparency, deformations, and showcase effects regardless of the selected
decoder backend. Playback workers own codec state; Unity and Beat Saber objects
remain on the game thread.

The FFmpeg builds are decoder-only, use unique SONAMEs and symbol namespaces,
and are validated to exclude GPL, version-3, and nonfree components. Both build
configurations and source transformations are recorded for reproducibility.

</details>

<details>
<summary><strong>Video shader methods and Beat Saber's Bloom</strong></summary>

The current tree contains two experimental video-material paths: Unity's
`UI/Default` shader with RGB-only color writes, and an embedded
`BigScreen/Video` AssetBundle shader. The bundle project is pinned to Unity
2022.3.33f1 and configures Oculus Android Multiview so required stereo variants
can be compiled.

Off selects Unity's `UI/Default` shader with RGB-only color writes. On selects
the embedded `BigScreen/Video` shader, which provides explicit alpha blending,
depth writes, and Cinema soft-additive blending. If the requested shader cannot
be loaded, the implementation follows a documented fallback ladder and records
the selected tier in the log. These source-level checks establish how the two
paths are wired; their complete current behavior with Bloom on and off still
requires the on-device matrix in
[Current development checkpoint](docs/KNOWN_ISSUES.md).

Why the two paths treat Bloom differently: Beat Saber's bloom composite reads
the framebuffer's **alpha channel as a per-pixel emission weight**. A shader
that writes opaque alpha over the picture makes the game bloom the video into
a solid white rectangle. The `UI/Default` path avoids this by being unable to
write alpha at all (`_ColorMask = RGB`) — but that also means it can only
*preserve* whatever emission the map's lighting already wrote behind the
screen, so bloom-heavy maps (for example YY.exe) can still wash it out. The
embedded shader's separate alpha blend equation instead actively **clears**
the emission weight where video covers the screen — Cinema parity: opaque
screens force it to zero (alpha blend Zero/Zero), transparent and
soft-additive screens attenuate it by coverage (Zero/OneMinusSrcAlpha) —
which is why the embedded method is the correct selection for bloom-heavy
content. Preserving destination alpha (Zero/One) instead of clearing it was
the cause of the 2026-08-18 white-screen-on-bloom-maps regression; the
invariant tests pin the clearing blend factors.

Cinema's soft-additive picture blending (`colorBlending`) is applied only
when the map file explicitly sets it to `true`. It is never inferred from
other mapper presentation fields: that inference gave every screen-placing
map — including the bundled showcase — see-through additive screens with no
solid body, overriding the player's opacity settings. An absent field means
the player's own presentation settings win.

</details>

<details>
<summary><strong>Standalone downloader runtime</strong></summary>

The QMOD includes CPython 3.14.7 for Android ARM64, a pinned yt-dlp baseline,
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
yt-dlp version. Big Screen checks the latest public stable mod release once per
Beat Saber session and notifies the player only when a newer version exists.
The manual check always reports its result. Mod updates remain notification-only
and are installed through ModsBeforeFriday or the GitHub release page.

</details>

<details>
<summary><strong>Project layout</strong></summary>

```text
include/BigScreen/      Public declarations and testable core logic
src/                    Native mod, UI, playback, storage, and downloader code
tests/                  Host-side C++ and embedded-Python tests
scripts/                Reproducible dependency, build, deploy, and QMOD scripts
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
- [External code-review resolution](docs/CODE_REVIEW_RESOLUTION.md)
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
