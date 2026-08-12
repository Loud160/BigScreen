# Changelog

## 0.6.6 — unreleased beta

- Quest-native H.264 video playback for OST, DLC, custom, and WIP songs.
- Video Library with mapper, YouTube, map-folder, and Video Import sources.
- Five flat/curved layouts, pause/song-selection controls, environment cleanup, Cinema/Chroma and Replay compatibility.
- Advanced layout-scoped screen rotation, video rotation/zoom/pan/perspective tilt, stretch or transparency-aware letterboxing, and controller-based undocked screen placement.
- Embedded CPython/yt-dlp plus an in-process QuickJS-NG challenge engine, with verified updates, provider compatibility testing, rejection, and rollback.
- Reproducible, hash-pinned source builds for QuickJS-NG, yt-dlp, and yt-dlp-ejs, including byte-for-byte release-payload comparison.
- Automatic library recovery, storage maintenance, performance diagnostics/reduction, error containment, host tests, and CI.
- Release hardening adds HTTPS YouTube mapper URL restrictions, metadata/list bounds, decoder-worker exception containment, pinned CI actions, and packaged third-party notices.
- Downloader hardening adds bounded QuickJS input/output/stack use, exception-safe Python ownership, real EJS startup validation, transactional update activation, stale native-extension pruning, fresh QMOD verification, and source-rebuild CI.
- Playback hardening adds source-aware/VFR timing, reusable RGBA buffers, full request-cost diagnostics, gameplay decoder prewarming, and material-state updates only when invariants change.
- Menu and storage work is rate-limited and cached; downloader progress avoids per-block flash synchronization, network waits are bounded, and error dialogs back off safely.
- The runtime now uses CPython 3.14.7 and one pinned Android NDK r27d toolchain. Reproducible LGPL FFmpeg 4.4.8 and 9.0.1 builds remain side by side for controlled on-headset comparison.
- Expanded host/CI coverage validates downloader policies, repository/toolchain/license invariants, real H.264 decoding, portrait media, worker shutdown, and RGBA buffer reuse.
