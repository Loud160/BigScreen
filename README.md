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

> [!IMPORTANT]
> This branch is an **alpha port for Beat Saber 1.40.8
> (`1.40.8_7379`)** on Quest 2 and Quest 3. Quest mods are game-version
> specific; use the separately maintained 1.37 alpha branch for Beat Saber
> 1.37.x. The 1.40.8 native build, tests, dependency set, and QMOD package have
> been validated, but complete 1.40.8 headset regression testing is still in
> progress. Features described below are implemented; they must not be read as
> a claim that every integration has already passed that new-version test pass.

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
| **Save five layouts** | Keep five independent screen configurations and switch layouts from Big Screen, song selection, or the pause menu. |
| **Respect authored visuals** | Cinema timing metadata and optional Chroma placement can take control when a map was designed around a specific video presentation. |

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
| **Mapper-provided video** | Read `bigscreen.json`, `cinema-video.json`, or `video.json`. A URL-only map receives a Cinema-style download control on song selection. |
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

## Built for standalone Quest use

Big Screen supports H.264/H.265 MP4 and VP8/VP9 WebM sources. Its configurable
output limits cover 480p, 720p, 1080p, and hardware-only 1440p, with 15, 30, and
60 FPS caps. A source below the selected limit is not enlarged or treated as
missing frames.

For comparison and performance tuning, the mod includes:

- Android MediaCodec hardware decoding by default, with safe software fallback
  where the selected format and resolution permit it
- FFmpeg 9.0.1 by default, with an isolated FFmpeg 4.4.8 compatibility runtime
  selectable in-game for A/B testing
- Automatic Performance steps that lower FPS before resolution and restore each
  step in reverse when playback recovers
- A movable live performance panel and results summary
- Decoder latency, source/output resolution, delivered frames, video frame loss,
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

For layouts that extend below Beat Saber's menu floor, turn on **General > Open
Floor Placement**. The menu temporarily replaces the obstructing floor with a
thin four-lane/player-position guide; gameplay and other menus remain unchanged.

### Build and deploy from source on Windows

You do not need prior Beat Saber mod-development experience, but a clean build
does require more than Visual Studio. Install these tools before cloning:

| Tool | Why Big Screen needs it |
|---|---|
| **Git for Windows** | Clones the repository and preserves its versioned build recipes. |
| **Visual Studio with Desktop development with C++** | Supplies the Windows C++ compiler used by host tests. Include **C++ CMake tools for Windows**, or install CMake 3.20+ and Ninja separately. |
| **QPM CLI** | Restores the exact Quest headers/libraries recorded in `qpm.shared.json` and downloads the Windows Android NDK. This port was validated with QPM 1.5.11. |
| **WSL 2 with Ubuntu** | Builds Big Screen's private LGPL FFmpeg libraries in a Linux environment. Inside Ubuntu, install `build-essential`, `curl`, `xz-utils`, and `unzip`. |
| **Android NDK r27d (`27.3.13750724`)** | Two host-specific copies are used: QPM manages the Windows NDK for the mod, while `scripts/install-pinned-ndk.sh` installs the Linux NDK used by FFmpeg inside WSL. Do not substitute a different revision. |
| **Android platform-tools/ADB or SideQuest** | Required only to deploy directly to a connected Quest. It is not required to create the QMOD. |

Windows PowerShell 5.1 is already included with supported Windows versions.
Internet access and several gigabytes of temporary space are needed on the
first clean build because the pinned NDK, FFmpeg, CPython, QuickJS-NG, yt-dlp,
certifi, and Quest dependencies are downloaded and hash-checked.

Python 3 is optional but recommended because it enables the downloader and
repository-invariant host tests. Node.js/pnpm are needed only for the separate
yt-dlp/yt-dlp-ejs source-reproducibility audit, not for a normal mod build.

#### One-time toolchain setup

Install [QPM CLI](https://github.com/QuestPackageManager/QPM.CLI), then clone
and restore the project in PowerShell:

```powershell
git clone https://github.com/Loud160/BigScreen.git
cd BigScreen
qpm restore
qpm ndk resolve --download
qpm doctor
```

If the QPM installer did not add `qpm` to `PATH`, run the same commands through
its standard per-user executable:

```powershell
$qpm = "$env:LOCALAPPDATA\Programs\QPM\qpm.exe"
& $qpm restore
& $qpm ndk resolve --download
& $qpm doctor
```

Install WSL 2 from an Administrator PowerShell window if it is not already
available:

```powershell
wsl --install -d Ubuntu
```

After Windows finishes the WSL installation, open Ubuntu and run:

```bash
sudo apt update
sudo apt install -y build-essential curl xz-utils unzip
cd /mnt/c/path/to/BigScreen
./scripts/install-pinned-ndk.sh
```

Replace `/mnt/c/path/to/BigScreen` with the clone's actual WSL path. The normal
Windows build script will invoke WSL automatically whenever either pinned
FFmpeg runtime has not already been built.

#### Build a QMOD

From PowerShell in the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build.ps1 -clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/createqmod.ps1
```

The resulting `Big Screen.qmod` can be installed with a compatible Quest Beat
Saber mod manager. Building a QMOD does not require a connected headset.

#### Build and deploy directly to a Quest

Enable Quest developer mode, connect the headset over USB, put it on, and
accept the USB debugging authorization prompt. Then double-click
**[Build-And-Deploy.bat](Build-And-Deploy.bat)** in the repository root—or run
it from a terminal—to build and install the complete current mod:

```bat
Build-And-Deploy.bat
```

The launcher builds both FFmpeg runtimes and the embedded downloader, validates
the native libraries and mod manifest, removes stale copies from the opposite
Scotland2 load phase, deploys the complete runtime to the connected Quest, and
asks Beat Saber to restart. Its console remains open and clearly reports success
or the failed step. See [Building and packaging](docs/BUILDING.md) for clean
builds, host tests, QMOD creation, toolchain details, and troubleshooting.

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
- [Complete documentation index](docs/README.md)

## Development status

Big Screen is an unreleased alpha. Its capabilities are implemented and the
1.40.8 branch passes host tests, a clean ARM64 Quest build, dependency/ELF
checks, and QMOD validation. It is not release-verified until the remaining
hands-on Quest 2/Quest 3, map, UI, Chroma/Noodle, Replay, downloader, decoder,
and recovery checks in the [release checklist](docs/RELEASE_CHECKLIST.md) have
been completed on Beat Saber 1.40.8.

## License

Big Screen first-party source is licensed under **GPL-3.0-only** with the
project's additional GPLv3 section 7 terms. See [LICENSE](LICENSE) and
[LICENSE-ADDITIONAL-TERMS.md](LICENSE-ADDITIONAL-TERMS.md) for the complete terms.
Contributor and dependency licensing is documented in [CONTRIBUTING.md](CONTRIBUTING.md)
and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Created by **Loud160 (AKA Whisp)**. Big Screen is an independent community
project and is not affiliated with or endorsed by Beat Games, Meta, Google,
YouTube, BeatSaver, Cinema, or Chroma.
