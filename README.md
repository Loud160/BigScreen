# Big Screen

Big Screen is a standalone Quest 2/3 Beat Saber mod that plays synchronized
H.264 MP4 video on a configurable world-space screen. It supports OST, DLC,
custom, and WIP songs and does not require a PC after the QMOD is installed.

## Features

- Add a video to any song through an in-headset YouTube search/download,
  mapper-provided metadata, a custom map folder, or the global Video Import
  folder.
- Synchronized previews in song selection and full playback during gameplay,
  Replay playback, and Replay recording.
- Three saved screen layouts with flat/curved display, size up to 2.5x,
  distance, X/Y position, tilt, curvature, and optional curve aspect retention.
- 480p/720p/1080p output and 15/30/60 FPS presentation limits. Source files are
  retained at their downloaded/native resolution and downscaled while playing.
- Timing offset, playback speed, fit-to-song, black/transparent lead-in, menu
  scrubber, and synchronized song-audio preview.
- Optional map-lighting and environment cleanup controls for large screens.
- Automatic performance fallback and optional live/results diagnostics.
- Atomic video-library persistence with two backups and managed-file recovery.
- Background yt-dlp updates with official-release SHA-256 verification, archive
  validation, CPython import testing, and automatic rollback.
- Safe storage maintenance that previews the exact removable files before a
  confirmed cleanup.

## User video import

For a custom or WIP map, place an H.264/AVC MP4 (1080p or lower) directly in
that map's folder. For any song, place the MP4 in:

`/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Video Import`

Open **Big Screen > Video Library**, select the song, and press **SET** beside
the file. Files that cannot be decoded are retained and shown in red with a
HELP explanation. Big Screen never deletes map-folder or Video Import files;
**Remove Video** only unregisters them.

## Downloader runtime

The QMOD includes the official CPython 3.14.6 Android ARM64 runtime, a pinned
yt-dlp baseline, certifi, and the native libraries required by Python. The
runtime executes inside Beat Saber on a background thread—Termux, a system
Python installation, and a connected PC are not used. See
[docs/DOWNLOADER_SECURITY.md](docs/DOWNLOADER_SECURITY.md).

## Building

1. Install QPM-RS and the Android NDK used by the Quest modding toolchain.
2. Run `powershell -ExecutionPolicy Bypass -File scripts/build.ps1`.
3. Run `powershell -ExecutionPolicy Bypass -File scripts/createqmod.ps1` to
   package a QMOD, or `scripts/copy.ps1` to deploy to a connected test headset.

The runtime-fetch script pins and SHA-256 verifies every downloaded build
artifact. A normal build does not silently accept substituted files.

## Documentation

- [User guide](docs/USER_GUIDE.md)
- [Mapper video metadata](docs/MAPPER_FORMAT.md)
- [Troubleshooting and logs](docs/TROUBLESHOOTING.md)
- [Architecture and thread ownership](docs/ARCHITECTURE.md)
- [Downloader security and rollback](docs/DOWNLOADER_SECURITY.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Future work](docs/FUTURE_WORK.md)

Big Screen's original source is available under the [MIT License](LICENSE).
External libraries and generated API headers remain under their respective
licenses.
