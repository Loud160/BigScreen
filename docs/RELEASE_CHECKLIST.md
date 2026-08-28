# Release checklist

## Source and metadata

- [ ] Working tree contains only intended changes.
- [ ] Version matches in `qpm.json`, `qpm.shared.json`, `mod.template.json`, changelog, and tag.
- [ ] `packageVersion` exactly matches the tested Beat Saber APK.
- [ ] Public description, screenshots/video, and compatibility statement are current.
- [ ] Third-party artifact versions/hashes and notices were reviewed.
- [ ] `LICENSE`, `LICENSE-ADDITIONAL-TERMS.md`, `NOTICE`, contribution policy,
      and packaged runtime notices match the release.
- [ ] The canonical public source URL is present in project/release metadata
      and `NOTICE`; the complete matching Big Screen Corresponding Source is
      available from the same public release path used for the QMOD.
- [ ] Both exact FFmpeg source versions remain available from the permanent
      [FFmpeg corresponding-source release](https://github.com/Loud160/BigScreen/releases/tag/ffmpeg-sources-4.4.8-9.0.1),
      and the public QMOD release links directly to it. The packaged build
      configurations and patches match those source archives.
- [ ] Review the tracked large-file refactor in `FUTURE_WORK.md`; either complete
      it after major behavior is stable or explicitly carry it as documented
      technical debt for this release.

## Automated validation

- [ ] Host tests pass from a clean test build directory.
- [ ] The archived
      [code-review resolution](ai-assisted-development/reviews/CODE_REVIEW_RESOLUTION.md)
      still reflects the current implementation;
      deferred items have either been completed or deliberately carried forward.
- [ ] Linux decoder tests generate landscape/portrait H.264 fixtures and pass; RGBA/YUV frame-buffer allocation growth remains bounded.
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
- [ ] Clean Windows and native Linux builds from the same release commit and
      pinned inputs produce byte-identical QMODs with the same SHA-256.
- [ ] `Build-QMOD.bat` and `Build-QMOD-Linux.sh` both complete without starting
      ADB or accessing a Quest; the Windows launcher produces the same QMOD as
      the combined launcher's QMOD-only selection. For unattended parity
      testing, `Build-QMOD.bat --yes` and `Build-QMOD-Linux.sh --yes` produce
      byte-identical packages without enabling deployment.
- [ ] The tracked miniz host utility passes deterministic ZIP tests through the
      one canonical Linux path on Windows/WSL and native Linux without relying
      on a separately installed archive program.
- [ ] Generated QMOD does not declare Paper2 directly, `libbigscreen.so` has no
      Paper2 `DT_NEEDED` entry or exported bridge symbols, and dependencies that
      use Paper2 retain their own unmodified manifests.
- [ ] Windows and Linux source deployment refuse a Big Screen package
      registered through MBF, SideQuest, or another standard QMOD manager, but
      still allow an unmanaged/raw source-style installation.
- [ ] Windows and Linux source removal delete all receipt-owned Big
      Screen-exclusive files even after simulated hash drift, preserve shared
      dependencies, and separately default settings/video deletion to No. A
      video-removal test deletes only `BigScreen/Videos`, never map-folder or
      Video Import files.
- [ ] CI passes on the release commit/tag.

## Headset regression

- [ ] Fresh install and update-over-existing-data both launch.
- [ ] Mods menu, all tabs, dialogs, Reset to Defaults, and master disable work.
- [ ] OST, DLC, custom, and WIP entries appear once per song and remain correctly ordered after search/letter jump/editor return.
- [ ] Menu preview starts/stops with audio and cannot leak into the home/mod menus or resume after Quest focus changes.
- [ ] Let a full Video Library preview reach its natural end repeatedly. Song,
      video, and scrubber restart together; Pause/Play after completion also
      produces a new opening video frame without leaving the song editor.
- [ ] Mapper download, pasted URL download, cancellation, thumbnail, progress, and understandable errors work.
- [ ] Closing song details cancels only its own song-screen download and does
      not cancel an active Video Library download.
- [ ] A forced C++ worker failure cannot be overwritten by stale active status
      JSON; a new download can start without restarting Beat Saber.
- [ ] Center file browser starts in the map/Video Import folder, navigates shared storage, colors compatible/invalid MP4/WebM files green/red, gates Set Video, shows HELP, replaces assignments, and unregisters without deleting user files.
- [ ] A probed YouTube URL shows one Video Library button per available source tier; song selection opens the same tier list in a modal. 1440p warns that software decoding is unavailable.
- [ ] Offset, Fit to Song, speed, lead-in, pause/resume, scrub after end, and download auto-preview work.
- [ ] All five layouts, flat/curved caps, aspect retention, curve arrows, placement, transparency, and Chroma override work.
- [ ] Repeatedly enter/leave song selection, Video Library, gameplay, restart,
      and results screens with both video-material methods; material creation
      must not crash after scene changes or garbage collection.
- [ ] Respect Mapper Settings on/off is independent from Allow Chroma Override and preserves mapper media/timing in both states.
- [ ] With Respect Mapper Settings on, a Cinema-authored screen has identical
      size, shape, position, and rotation in Video Library/menu preview and
      gameplay. A mapper canvas with no `screenCurvature` remains flat even
      when the selected user layout is curved; turning mapper respect off
      restores that selected user layout immediately.
- [ ] PC Cinema color correction, rectangular/elliptical vignette, color blending, opaque screen body, and end fade work on Quest; Refresh reloads an edited map/playlist file without background polling.
- [ ] Rectangular and elliptical vignettes show no black rectangular backing; explicit black lead-in and mapper-authored opaque-body behavior remain intact.
- [ ] Cinema additional screens share the primary video without a second decoder/upload; position, rotation, scale, curvature, and the 32-screen safety limit behave as documented.
- [ ] Cinema exact-name/parent environment edits, active/transform fields, cloned lights, `mergePropGroups`, requested environments, and environment-only `forceEnvironmentModifications` work without affecting ordinary maps.
- [ ] Playlist `customData.cinema` resolves for custom and built-in songs, and mapper YouTube URLs probe/download with useful errors.
- [ ] Advanced video rotation/zoom/pan/tilt/stretch values are layout-scoped, Letterbox Transparency affects only unused canvas, and Video Opacity affects both docked and undocked pictures.
- [ ] Undocked move/rotate/resize saves only after Save Screen; cancel, focus loss, layout change, and menu exit restore the last saved placement and leave no raycastable overlay.
- [ ] Environment/light toggles affect only video maps and restore saved child states.
- [ ] Gameplay pause/resume synchronization, Replay playback, Replay recording, results/failure diagnostics, and map exit work. Big Screen pause-menu controls are intentionally absent on Beat Saber 1.40.8.
- [ ] Native 480p/720p/1080p/1440p files and 15/30/60 FPS limits produce expected diagnostics; Automatic Performance honors its attack/release timers, configured FPS step, and anti-oscillation limit while restoring prior limits without reopening the decoder or changing saved settings.
- [ ] The Misc FFmpeg selector restarts an active library preview at its
      retained position, affects the next gameplay decoder, and the overlay /
      results identify the runtime actually used.
- [ ] Repeated identical-map runs compare the default FFmpeg 9.0.1 runtime with
      FFmpeg 4.4.8 on Quest 2 and Quest 3.
- [ ] Hardware Video Decoding off reports Software and matches prior playback.
- [ ] Hardware Video Decoding on reports Hardware for compatible H.264 on both
      FFmpeg runtimes; colors, crop edges, pause, seek, practice speed, Replay,
      and full-song synchronization remain correct.
- [ ] A MediaCodec failure logs its reason, reports Software after automatic
      fallback, and never interrupts Beat Saber gameplay.
- [ ] GPU Video Conversion defaults on. A pre-migration settings file with the
      old saved `false` value is promoted to on exactly once, unrelated saved
      settings are unchanged, and turning it off afterward survives a restart.
      With it on, YUV420P and MediaCodec
      NV12 preserve color/range, container rotation, vignette transparency,
      mapper correction, flat/curved/additional screens, Showcase deformation,
      crack/shatter snapshots, pause, seek, practice speed, Replay, and loops.
- [ ] A missing conversion shader/RenderTexture and an unsupported decoded
      pixel layout each fall back once to CPU RGBA, log the reason, retain a
      playable map, and do not leave a stale prewarmed YUV frame queued.
- [ ] Quest 2 repeated-map A/B runs compare decoder CPU, presentation time,
      whole-process CPU, video loss, gameplay FPS, heat, and battery with GPU
      Video Conversion off/on. Quest 3 and Quest 3S follow after Quest 2 passes.
- [ ] Controlled software/hardware A/B runs compare preparation CPU average/peak,
      separately logged worker wait average/peak,
      decoder CPU, whole-process CPU, video loss, Quest FPS, and battery use.
- [ ] Library backup restoration/reconstruction and confirmed storage cleanup work.
- [ ] A deliberately missing `library.json` with a valid backup recovers; a
      first run with neither manifest nor backups starts empty without error.
- [ ] Corrupt settings JSON is quarantined and reported before defaults are saved.
- [ ] Stable updater check/install/restart passes; a deliberately incompatible candidate rolls back and is not reoffered.
- [ ] A YouTube URL requiring an EJS challenge reports `Big Screen QuickJS-NG` in the log and downloads without a missing-runtime warning.
- [ ] Misc > Showcase opens a center readiness page without starting network work; missing/inactive Chroma or Noodle Extensions are reported and the missing map/video each have a working adjacent action.
- [ ] Showcase map/video progress remains on the readiness page, Cancel/Close never becomes trapped behind the left panel, and Play stays disabled until all requirements are ready.
- [ ] The first ready launch starts Lawless Expert+ directly without manually opening Solo and uses No Fail only for that launcher-started session; starting the same map normally retains the player's saved modifiers. Replay, Continue, and post-song navigation remain stock; reopening Misc > Showcase afterward shows an idle, reusable Play button rather than stale launch status.
- [ ] Menu Environment defaults to the stock menu and exposes No Environment, Menu Environment, Map Environment, and Map Environment + Lightshow. With Use Big Mirror Override on, verify Big Mirror appears when the menu opens before selecting/playing a song and remains the same resident host while browsing and previewing multiple songs. With the override off, verify that map scenery survives editor-to-browser navigation, same-environment selections do not reload, different environments unload completely before the next host loads, matching lightshow time follows play/pause/seek/loop, and no notes, obstacles, sabers, gameplay audio, or gameplay camera become active. Switch repeatedly between maps with different environments and mapper-driven movement to verify outgoing audio controllers cannot update during the serialized Pop/Push boundary. Show Gameplay HUD defaults off, is enabled only for the two map modes, and hides/shows stock and compatible mod HUDs immediately without pausing the audition, replacing the host, changing the player's normal gameplay HUD preference, freezing input, or crashing. While Map Environment is visible, toggle every Environment-tab lighting, motion, ring, side-bar, side-laser, and spectrogram option both ways and confirm the loaded scene changes immediately and restores correctly; repeat with Respect Mapper Settings and Allow Chroma Override both on to confirm neither screen choice bypasses an environment control. Show Lane Guides remains independent. A failed map scene must visibly fall back to the normal menu environment. Master disable, Close, gameplay launch, Meta-button focus loss/return, and internal-error dismissal must pop the hosted scenes, restore the menu EventSystem/environment, and leave no guide objects behind.
- [ ] A malformed, oversized, path-traversing, wrong-host, or wrong-revision showcase package is rejected without changing user custom-map folders.
- [ ] Two simulated internal errors trigger one disable dialog without popup spam; gameplay failures never interrupt the map.

## Publication

- [ ] Release notes distinguish features, fixes, compatibility, and known limitations.
- [ ] QMOD SHA-256 is recorded in the release notes.
- [ ] Source corresponding to the released binary and third-party notices are published with the release.
- [ ] Release remains draft until a second clean-headset install is confirmed.
