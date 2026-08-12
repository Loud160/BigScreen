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
- [ ] Linux decoder tests generate landscape/portrait H.264 fixtures and pass; RGBA allocation growth remains bounded.
- [ ] QuickJS output, exception, timeout, recursion, source-limit, and output-limit tests pass in a Release build; the pinned source hash was reviewed.
- [ ] `build-downloader-from-source.ps1` rebuilds yt-dlp/yt-dlp-ejs and its full payload matches the pinned official release.
- [ ] Clean ARM64 Quest build passes with no unresolved symbols.
- [ ] Clean ARM64 builds pass against both FFmpeg 4.4.8 and 9.0.1; the packaged build record matches the selected runtime.
- [ ] QMOD creation passes from clean staged dependencies and replaces no prior artifact until fresh-archive validation succeeds.
- [ ] QMOD archive exactly contains the unique manifest inputs: native libraries, full downloader runtime, manifest, and license/notices.
- [ ] CI passes on the release commit/tag.

## Headset regression

- [ ] Fresh install and update-over-existing-data both launch.
- [ ] Mods menu, all tabs, dialogs, Reset to Defaults, and master disable work.
- [ ] OST, DLC, custom, and WIP entries appear once per song and remain correctly ordered after search/letter jump/editor return.
- [ ] Menu preview starts/stops with audio and cannot leak into the home/mod menus or resume after Quest focus changes.
- [ ] Mapper download, pasted URL download, cancellation, thumbnail, progress, and understandable errors work.
- [ ] Map-folder local file, Video Import file, invalid-file HELP, assignment replacement, and unregister behavior work.
- [ ] Offset, Fit to Song, speed, lead-in, pause/resume, scrub after end, and download auto-preview work.
- [ ] All five layouts, flat/curved caps, aspect retention, curve arrows, placement, transparency, and Chroma override work.
- [ ] Advanced video rotation/zoom/pan/tilt/stretch values are layout-scoped and black/transparent letterboxing follows Video Transparency.
- [ ] Undocked move/rotate/resize saves only after Save Screen; cancel, focus loss, layout change, and menu exit restore the last saved placement and leave no raycastable overlay.
- [ ] Environment/light toggles affect only video maps and restore saved child states.
- [ ] Gameplay pause controls, Replay playback, Replay recording, results/failure diagnostics, and map exit work.
- [ ] 480p/720p/1080p and 15/30/60 FPS produce expected diagnostics; Automatic Performance steps down without changing saved settings.
- [ ] Repeated identical-map runs compare FFmpeg 4.4.8 and 9.0.1 on Quest 2 and Quest 3 before changing the release default.
- [ ] Library backup restoration/reconstruction and confirmed storage cleanup work.
- [ ] Stable updater check/install/restart passes; a deliberately incompatible candidate rolls back and is not reoffered.
- [ ] A YouTube URL requiring an EJS challenge reports `Big Screen QuickJS-NG` in the log and downloads without a missing-runtime warning.
- [ ] Two simulated internal errors trigger one disable dialog without popup spam; gameplay failures never interrupt the map.

## Publication

- [ ] Release notes distinguish features, fixes, compatibility, and known limitations.
- [ ] QMOD SHA-256 is recorded in the release notes.
- [ ] Source corresponding to the released binary and third-party notices are published with the release.
- [ ] Release remains draft until a second clean-headset install is confirmed.
