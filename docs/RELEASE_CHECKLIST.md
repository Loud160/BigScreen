# Release checklist

## Source and metadata

- [ ] Working tree contains only intended changes.
- [ ] Version matches in `qpm.json`, `qpm.shared.json`, `mod.template.json`, changelog, and tag.
- [ ] `packageVersion` exactly matches the tested Beat Saber APK.
- [ ] Public description, screenshots/video, and compatibility statement are current.
- [ ] Third-party artifact versions/hashes and notices were reviewed.
- [ ] Review the tracked large-file refactor in `FUTURE_WORK.md`; either complete
      it after major behavior is stable or explicitly carry it as documented
      technical debt for this release.

## Automated validation

- [ ] Host tests pass from a clean test build directory.
- [ ] `CODE_REVIEW_RESOLUTION.md` still reflects the current implementation;
      deferred items have either been completed or deliberately carried forward.
- [ ] Linux decoder tests generate landscape/portrait H.264 fixtures and pass; RGBA allocation growth remains bounded.
- [ ] QuickJS output, exception, timeout, recursion, source-limit, and output-limit tests pass in a Release build; the pinned source hash was reviewed.
- [ ] `build-downloader-from-source.ps1` rebuilds yt-dlp/yt-dlp-ejs and its full payload matches the pinned official release.
- [ ] Clean ARM64 Quest build passes with no unresolved symbols.
- [ ] `libbigscreen.so` directly needs `libpython3.14.so` but does not directly
      need CPython's SSL, crypto, or SQLite runtime libraries.
- [ ] A clean ARM64 build includes both isolated decoder backends, all eight
      version-suffixed FFmpeg libraries, and both packaged build records.
- [ ] ELF inspection proves the 4.4 backend needs only `BIGSCREEN44_LIB*`
      symbols and the 9 backend needs only `BIGSCREEN9_LIB*` symbols.
- [ ] QMOD creation passes from clean staged dependencies and replaces no prior artifact until fresh-archive validation succeeds.
- [ ] QMOD archive exactly contains the unique manifest inputs: native libraries, full downloader runtime, manifest, and license/notices.
- [ ] CI passes on the release commit/tag.

## Headset regression

- [ ] Fresh install and update-over-existing-data both launch.
- [ ] Mods menu, all tabs, dialogs, Reset to Defaults, and master disable work.
- [ ] OST, DLC, custom, and WIP entries appear once per song and remain correctly ordered after search/letter jump/editor return.
- [ ] Menu preview starts/stops with audio and cannot leak into the home/mod menus or resume after Quest focus changes.
- [ ] Mapper download, pasted URL download, cancellation, thumbnail, progress, and understandable errors work.
- [ ] Closing song details cancels only its own song-screen download and does
      not cancel an active Video Library download.
- [ ] A forced C++ worker failure cannot be overwritten by stale active status
      JSON; a new download can start without restarting Beat Saber.
- [ ] Center file browser starts in the map/Video Import folder, navigates shared storage, colors compatible/invalid MP4/WebM files green/red, gates Set Video, shows HELP, replaces assignments, and unregisters without deleting user files.
- [ ] A probed YouTube URL shows one Video Library button per available source tier; song selection opens the same tier list in a modal. 1440p warns that software decoding is unavailable.
- [ ] Offset, Fit to Song, speed, lead-in, pause/resume, scrub after end, and download auto-preview work.
- [ ] All five layouts, flat/curved caps, aspect retention, curve arrows, placement, transparency, and Chroma override work.
- [ ] Advanced video rotation/zoom/pan/tilt/stretch values are layout-scoped, Letterbox Transparency affects only unused canvas, and Video Opacity affects both docked and undocked pictures.
- [ ] Undocked move/rotate/resize saves only after Save Screen; cancel, focus loss, layout change, and menu exit restore the last saved placement and leave no raycastable overlay.
- [ ] Environment/light toggles affect only video maps and restore saved child states.
- [ ] Gameplay pause controls, Replay playback, Replay recording, results/failure diagnostics, and map exit work.
- [ ] 480p/720p/1080p and 15/30/60 FPS produce expected diagnostics; Automatic Performance steps down without changing saved settings.
- [ ] The Misc FFmpeg selector restarts an active library preview at its
      retained position, affects the next gameplay decoder, and the overlay /
      results identify the runtime actually used.
- [ ] Repeated identical-map runs compare FFmpeg 4.4.8 and 9.0.1 on Quest 2 and Quest 3 before changing the release default.
- [ ] Hardware Video Decoding off reports Software and matches prior playback.
- [ ] Hardware Video Decoding on reports Hardware for compatible H.264 on both
      FFmpeg runtimes; colors, crop edges, pause, seek, practice speed, Replay,
      and full-song synchronization remain correct.
- [ ] A MediaCodec failure logs its reason, reports Software after automatic
      fallback, and never interrupts Beat Saber gameplay.
- [ ] Controlled software/hardware A/B runs compare decode average/peak,
      decoder CPU, whole-process CPU, video loss, Quest FPS, and battery use.
- [ ] Library backup restoration/reconstruction and confirmed storage cleanup work.
- [ ] A deliberately missing `library.json` with a valid backup recovers; a
      first run with neither manifest nor backups starts empty without error.
- [ ] Corrupt settings JSON is quarantined and reported before defaults are saved.
- [ ] Stable updater check/install/restart passes; a deliberately incompatible candidate rolls back and is not reoffered.
- [ ] A YouTube URL requiring an EJS challenge reports `Big Screen QuickJS-NG` in the log and downloads without a missing-runtime warning.
- [ ] Misc > Showcase opens a center readiness page without starting network work; missing/inactive Chroma or Noodle Extensions are reported and the missing map/video each have a working adjacent action.
- [ ] Showcase map/video progress remains on the readiness page, Cancel/Close never becomes trapped behind the left panel, and Play stays disabled until all requirements are ready.
- [ ] The first ready launch starts Lawless Expert+ directly without manually opening Solo; Replay, Continue, and post-song navigation remain stock; reopening Misc > Showcase afterward shows an idle, reusable Play button rather than stale launch status.
- [ ] A malformed, oversized, path-traversing, wrong-host, or wrong-revision showcase package is rejected without changing user custom-map folders.
- [ ] Two simulated internal errors trigger one disable dialog without popup spam; gameplay failures never interrupt the map.

## Publication

- [ ] Release notes distinguish features, fixes, compatibility, and known limitations.
- [ ] QMOD SHA-256 is recorded in the release notes.
- [ ] Source corresponding to the released binary and third-party notices are published with the release.
- [ ] Release remains draft until a second clean-headset install is confirmed.
