# Big Screen

<p align="center">
  <strong>Quest-native synchronized video screens for Beat Saber</strong><br>
  Add a video to OST, DLC, custom, or WIP songs without requiring a PC during normal use.
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Meta%20Quest%202%20%7C%203-00b2ff">
  <img alt="Beat Saber" src="https://img.shields.io/badge/Beat%20Saber-1.37.0-orange">
  <img alt="Language" src="https://img.shields.io/badge/language-C%2B%2B20-blue">
  <img alt="Status" src="https://img.shields.io/badge/status-beta-yellow">
  <img alt="License" src="https://img.shields.io/badge/source%20license-GPL--3.0--only-blue">
</p>

Big Screen plays H.264/H.265 MP4 and VP8/VP9 WebM video on a configurable world-space screen, synchronized to Beat Saber's own song clock. It includes an entirely standalone, in-headset YouTube workflow, local-file support, Cinema/Chroma compatibility, Replay compatibility, performance controls, recovery, and storage maintenance. It contains no advertising, telemetry, account system, or subscription.

> [!IMPORTANT]
> The current package targets **Beat Saber 1.37.0 (`1.37.0_9064817954`)** on Quest 2 and Quest 3. Quest mods are game-version specific; do not install this build on another Beat Saber version unless a compatible build is explicitly provided.

## Highlights

| | Capability | What it does |
|---|---|---|
| 🎬 | Any song can have video | Assign video to OST, DLC, custom, and WIP songs without modifying Beat Saber audio or beatmaps. |
| 🔎 | In-headset YouTube workflow | Search YouTube in the Quest browser, paste a normal or share URL, preview the thumbnail, and choose each available 480p/720p/1080p/1440p source tier with visible progress and readable errors. |
| 📁 | Local video browser | Browse Quest shared storage from the center screen and assign compatible MP4 or WebM video to any song. User-owned local files are never deleted by Remove Video. |
| 🖥️ | Five screen layouts | Save five independent flat/curved layouts and switch them from the mod, song-selection header, or pause menu. |
| 🎛️ | Detailed screen control | Adjust size, distance, X/Y position, tilt, rotation, curvature, curve aspect behavior, picture opacity, and letterbox transparency. Advanced controls can freely position and reshape an undocked screen. |
| 🔍 | Independent video framing | Rotate, zoom, pan, perspective-tilt, letterbox, or stretch the picture inside the screen without changing the decoded resolution. |
| ⏱️ | Audio-clock synchronization | Configure offset, playback speed, automatic fit-to-song timing, and transparent or black lead-in. |
| 💡 | Video-friendly environments | Keep or suppress map lights, force Big Mirror, stop background motion, and hide geometry or light groups that obstruct large screens. |
| 🎞️ | Preview and Replay support | Preview videos while browsing songs and keep them present during Replay playback and recording. |
| 📊 | Quest performance controls | Cap output at 480p/720p/1080p/1440p and 15/30/60 FPS, compare software with experimental MediaCodec hardware decoding, view diagnostics, let Automatic Performance adjust quality, or record repeatable CPU/battery benchmark CSVs. |
| 🛟 | Recovery and containment | Atomic library writes, two backups, managed-file reconstruction, updater rollback, and deferred gameplay errors reduce the chance of lost data or interrupted maps. |
| 🚀 | Built-in showcase launcher | From Misc, verify Chroma/Noodle Extensions, download the exact BeatSaver map and video when needed, and open the motion-heavy Lawless Expert+ demonstration directly. |

## Documentation

Start with the [documentation index](docs/README.md), or jump directly to:

- [Installation and first run](docs/INSTALLATION.md)
- [Every setting and interaction](docs/SETTINGS.md)
- [Video Library and adding videos](docs/USER_GUIDE.md)
- [Mapper metadata format](docs/MAPPER_FORMAT.md)
- [Troubleshooting, logs, and recovery](docs/TROUBLESHOOTING.md)
- [Downloader security and rollback](docs/DOWNLOADER_SECURITY.md)
- [Architecture and thread ownership](docs/ARCHITECTURE.md)
- [Building and packaging](docs/BUILDING.md)
- [Privacy and network access](docs/PRIVACY.md)
- [Known limitations and future work](docs/FUTURE_WORK.md)

## Quick start

1. Install the compatible QMOD using a Quest Beat Saber mod manager.
2. Start Beat Saber, open **Mods**, then open **Big Screen**.
3. Leave **Big Screen Enabled**, **Video In Map**, and **Preview Video** on.
4. Open **Video Library**, select a song, and either use **Show File Browser** to choose a compatible local MP4/WebM or paste a YouTube URL and choose an available download resolution.
5. After assignment/download, use the synchronized preview to adjust **Video Playback Offset**, **Playback Speed**, and **Fit to Song**.
6. Open **Screen** to position the blank preview surface and save up to five layouts. Enable **Advanced Screen Controls** on only the layouts that need independent video framing or free placement.
7. Select the song normally. Big Screen's header controls can globally enable previews/gameplay video and change layouts without reopening the mod.

To see the proof-of-concept choreography, open **Misc > Showcase** and choose
**Play Big Screen Showcase**. Chroma and Noodle Extensions must both be loaded.
The center-screen readiness page lists both mods, the pinned Up & Down map, its
video, and the downloader runtime. Missing map and video files have separate
buttons and are never downloaded merely because the page opened. Once every
requirement is ready, **Play Showcase** shows the motion-sickness warning and
starts Lawless Expert+ without a difficulty-selection step. Results and
post-song navigation remain Beat Saber's standard controls; reopen Big Screen
from Mods whenever you want to run it again. The map is
obtained from BeatSaver on demand; it is not bundled in the QMOD.

## Ways to add a video

- **Mapper-provided:** Big Screen reads `bigscreen.json`, `cinema-video.json`, or `video.json`. If a mapper supplied only a URL, the song page offers a Cinema-style Download Video control.
- **YouTube:** Search from the selected song, paste an HTTPS `youtube.com` or `youtu.be` URL, verify its thumbnail, then choose one available source tier. 480p/720p/1080p downloads use H.264 MP4; 1440p uses VP9 WebM and requires hardware decoding.
- **Quest file browser:** Select **Show File Browser**. Custom/WIP songs begin in their map folder; OST/DLC songs begin in `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Video Import`. Use **Back One Folder** or the clickable path breadcrumbs to navigate elsewhere in Quest shared storage, select a green compatible MP4/WebM, then choose **Set Video**.

Downloaded files are mod-managed. Every file selected through the browser remains user-owned in its original folder. **Remove Video** unregisters a user-owned file and never deletes it.

## Downloader runtime

The QMOD includes CPython 3.14.7 for Android ARM64, a pinned yt-dlp baseline with yt-dlp-ejs 0.8.0, certifi, and QuickJS-NG 0.16.1 compiled directly into Big Screen. QuickJS supplies the JavaScript challenge engine required for full modern YouTube extraction without trying to launch an Android executable from `ModData`. Everything runs inside Beat Saber on downloader workers: Termux, a system Python/JavaScript installation, a YouTube login, and a connected PC are not used. Updates are offered rather than silently installed, verified against the official release SHA-256 list, tested against `YoutubeDL`, yt-dlp-ejs, and Big Screen's QuickJS provider after restart, and rolled back if incompatible.

Reproducible source-build recipes are tracked with the project. QuickJS-NG is compiled from its pinned source amalgamation during every native build. `scripts/build-downloader-from-source.ps1` retrieves hash-pinned yt-dlp and yt-dlp-ejs source archives, rebuilds the EJS JavaScript payload through its upstream lockfile, assembles the zipimport runtime, and verifies every generated file against the official release Big Screen ships. Third-party source archives and generated dependencies stay in ignored build directories rather than being duplicated in Git.

## Project layout

```text
include/BigScreen/      Public declarations and testable core logic
src/                    Native mod, UI, playback, storage, and downloader code
tests/                  Host-side C++ and embedded-Python tests
scripts/                Reproducible dependency, build, deploy, and QMOD scripts
docs/                   User, mapper, architecture, security, and build manuals
licenses/               Redistributable third-party license texts
.github/workflows/      Host tests and Quest/NDK package build
```

## Development status

Big Screen is feature-complete for its initial beta but still needs broad public headset/map coverage. The current release gate requires host tests, a clean Quest build, QMOD validation, and hands-on Quest regression testing. See [Building](docs/BUILDING.md) and the [release checklist](docs/RELEASE_CHECKLIST.md).

## Legal and licenses

Big Screen first-party source is distributed under
[GPL-3.0-only](LICENSE), with reasonable attribution and origin/modification
terms permitted by GPLv3 sections 7(b) and 7(c). A narrow additional permission
allows the GPL-covered mod to interoperate with Beat Saber, Unity, the Quest
platform, and separately distributed Quest modding interfaces without claiming
that those components are GPL-licensed. Read the complete
[section 7 terms and interoperability permission](LICENSE-ADDITIONAL-TERMS.md).

In plain English, covered Big Screen code may be used, modified, and shared
under GPLv3, but copied Big Screen-derived material must retain reasonable
credit to **Loud160 (AKA Whisp)** and must not be falsely represented as having
originated solely elsewhere. Modified versions must be identified as modified.
These requirements concern covered code and material, not ideas, algorithms,
techniques, interoperability facts, or independently developed implementations.

Contributions use an explicit **inbound MIT / outbound GPL-3.0-only plus
section 7 terms** model. An intentional contribution grants the maintainer a
separate MIT license to that contribution; the DCO 1.1 sign-off separately
certifies the contributor's right to submit it. See
[CONTRIBUTING.md](CONTRIBUTING.md),
[INBOUND_LICENSE.md](INBOUND_LICENSE.md), and [DCO.txt](DCO.txt).

The QMOD includes independent third-party software under separate terms. Its
private FFmpeg 4.4.8 and 9.0.1 runtimes are dynamically linked under
LGPL-2.1-or-later and are machine-checked to exclude GPL, version-3, and
nonfree components. QuickJS-NG remains MIT-licensed; CPython, yt-dlp, certifi,
OpenSSL, SQLite, and the Quest dependencies retain their respective terms.
The package installs Big Screen's license, section 7 terms, attribution notice,
and the applicable runtime notices. Read
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistribution.

> [!IMPORTANT]
> The canonical repository is <https://github.com/Loud160/BigScreen> and may
> remain private during development. Before any public QMOD release—including
> listing through ModsBeforeFriday/BSQMods—the complete matching Corresponding
> Source must be publicly accessible from that repository or an equivalent
> release location. The QMOD format and MBF installation flow do not conflict
> with this license, but a binary-only upload without accessible matching source
> would not satisfy GPLv3.

Big Screen is an independent community project. It is not affiliated with or endorsed by Beat Games, Meta, Google, YouTube, BeatSaver, the Cinema mod, or the Chroma mod. Users are responsible for complying with the rights and terms applicable to maps and videos they download or import.
