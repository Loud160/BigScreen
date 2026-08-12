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
  <img alt="License" src="https://img.shields.io/badge/source%20license-MIT-green">
</p>

Big Screen plays H.264 MP4 video on a configurable world-space screen, synchronized to Beat Saber's own song clock. It includes an entirely standalone, in-headset YouTube workflow, local-file support, Cinema/Chroma compatibility, Replay compatibility, performance controls, recovery, and storage maintenance. It contains no advertising, telemetry, account system, or subscription.

> [!IMPORTANT]
> The current package targets **Beat Saber 1.37.0 (`1.37.0_9064817954`)** on Quest 2 and Quest 3. Quest mods are game-version specific; do not install this build on another Beat Saber version unless a compatible build is explicitly provided.

## Highlights

| | Capability | What it does |
|---|---|---|
| 🎬 | Any song can have video | Assign video to OST, DLC, custom, and WIP songs without modifying Beat Saber audio or beatmaps. |
| 🔎 | In-headset YouTube workflow | Search YouTube in the Quest browser, paste a normal or share URL, preview the thumbnail, and download with visible progress and readable errors. |
| 📁 | Local video support | Register compatible MP4s from custom-map folders or the global Video Import folder; user-owned local files are never deleted by Remove Video. |
| 🖥️ | Five screen layouts | Save five independent flat/curved layouts and switch them from the mod, song-selection header, or pause menu. |
| 🎛️ | Detailed screen control | Adjust size, distance, X/Y position, tilt, rotation, curvature, curve aspect behavior, and transparency. Advanced controls can freely position and reshape an undocked screen. |
| 🔍 | Independent video framing | Rotate, zoom, pan, perspective-tilt, letterbox, or stretch the picture inside the screen without changing the decoded resolution. |
| ⏱️ | Audio-clock synchronization | Configure offset, playback speed, automatic fit-to-song timing, and transparent or black lead-in. |
| 💡 | Video-friendly environments | Keep or suppress map lights, force Big Mirror, stop background motion, and hide geometry or light groups that obstruct large screens. |
| 🎞️ | Preview and Replay support | Preview videos while browsing songs and keep them present during Replay playback and recording. |
| 📊 | Quest performance controls | Cap output at 480p/720p/1080p and 15/30/60 FPS, view diagnostics, or let Automatic Performance step quality down temporarily. |
| 🛟 | Recovery and containment | Atomic library writes, two backups, managed-file reconstruction, updater rollback, and deferred gameplay errors reduce the chance of lost data or interrupted maps. |

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
4. Open **Video Library**, select a song, and either choose a compatible local MP4 or paste a YouTube URL.
5. After assignment/download, use the synchronized preview to adjust **Video Playback Offset**, **Playback Speed**, and **Fit to Song**.
6. Open **Screen** to position the blank preview surface and save up to five layouts. Enable **Advanced Options** only if you need independent video framing or a freely positioned screen.
7. Select the song normally. Big Screen's header controls can globally enable previews/gameplay video and change layouts without reopening the mod.

## Ways to add a video

- **Mapper-provided:** Big Screen reads `bigscreen.json`, `cinema-video.json`, or `video.json`. If a mapper supplied only a URL, the song page offers a Cinema-style Download Video control.
- **YouTube:** Search from the selected song, paste an HTTPS `youtube.com` or `youtu.be` URL, verify its thumbnail, then download.
- **Custom/WIP map folder:** Put an H.264/AVC MP4 at 1080p or lower in the map folder. Select **SET** beside the filename.
- **Video Import:** Put an MP4 in `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Video Import`, then assign it to any song from Video Library.

Downloaded files are mod-managed. Map-folder and Video Import files remain user-owned. **Remove Video** unregisters a user-owned file and never deletes it.

## Downloader runtime

The QMOD includes CPython 3.14.6 for Android ARM64, a pinned yt-dlp baseline, certifi, and required native libraries. It runs inside Beat Saber on a background thread: Termux, a system Python installation, a YouTube login, and a connected PC are not used. Updates are offered rather than silently installed, verified against the official release SHA-256 list, import-tested after restart, and rolled back if incompatible.

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

Big Screen's original source is available under the [MIT License](LICENSE). The QMOD includes independent third-party software under separate terms. In particular, the packaged FFmpeg build identifies itself as GPL version 3 or later; its applicable text and the CPython, certifi, and yt-dlp terms are installed with the runtime. Read [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistribution.

Big Screen is an independent community project. It is not affiliated with or endorsed by Beat Games, Meta, Google, YouTube, the Cinema mod, or the Chroma mod. Users are responsible for complying with the rights and terms applicable to videos they download or import.
