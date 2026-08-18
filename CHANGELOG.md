# Changelog

## Unreleased

- Restored the downloader's original YouTube-only security boundary. Mapper
  metadata, pasted addresses, URL probes, UI text, tests, and documentation now
  accept only HTTPS YouTube/youtu.be addresses.
- Removed active-map Cinema JSON polling. Playlist metadata is indexed once per
  game session, and the selected-song editor now provides an explicit Refresh
  button that rebuilds that index and reloads the selected map's metadata only
  when the user requests it.
- Added an experimental `BigScreen/Video` AssetBundle shader project pinned to
  Unity 2022.3.33f1, with Oculus Android Multiview configuration and a tracked
  build script. The complete authored Unity project inputs are kept in source
  control so a clean clone can reproduce the bundle.
- Added a development-only Embedded Video Shader selector, a `UI/Default`
  RGB-only alternative, an explicit fallback ladder, and shader-tier
  diagnostics. The source and bundle checks establish the intended material
  configuration but do not replace the full Bloom-on/Bloom-off Quest retest in
  `docs/KNOWN_ISSUES.md`.
- Added a Cinema-compatibility implementation for mapper screen geometry,
  additional screens, color correction, vignette, and explicit environment
  instructions, plus an explicit Video Library Refresh action. This work has
  host coverage but has not completed its Quest regression pass. PC Cinema's
  `bloom` field is not parsed or applied.
- This is a preservation checkpoint with serious outstanding quality concerns;
  every feature must be retested on Quest before the tree is treated as
  releasable.

## 0.7.0-alpha.7 — Reliable preview completion and runtime hardening

- Fixed Video Library previews intermittently freezing two or three seconds
  before the end. Big Screen now disables `SongPreviewPlayer`'s automatic
  early return-to-menu-music timer and remains the sole owner of synchronized
  preview looping at the actual song/audio boundary.
- Corrected FFmpeg send/receive handling under decoder backpressure. Compressed
  packets rejected with `EAGAIN` remain referenced until accepted, and the
  asynchronous MediaCodec end-of-stream drain now waits for authoritative EOF
  instead of dropping final pictures or declaring completion early.
- Made the displayed Frames Skipped total monotonic by recording completed
  presentation deadlines. A decoder catching up can satisfy later deadlines
  without making an already missed frame disappear from the user-facing total.
- Limited mapper environment ownership to maps actually detected as using
  Chroma. Cinema metadata alone no longer bypasses Big Screen's environment
  settings, while Chroma maps and authored screen geometry remain supported
  when Allow Chroma Override is enabled.
- Removed the non-rendering Beat Saber 1.40.8 pause-menu controls and their
  unsafe hidden BSML hierarchy, which could leave a stale Unity animation
  reference and crash while exiting a video map.
- Repaired GitHub builds by installing and hash-verifying the immutable QPM
  1.5.11 release instead of relying on an expiring main-branch artifact. CI
  actions now use Node 24-capable releases and Ubuntu's packaged Ninja.
- Expanded regression coverage for repeated decoder EOF/seek loops, monotonic
  presentation misses, Cinema-versus-Chroma detection, and CI invariants.

## 0.7.0-alpha.6 — Playback adaptation and menu recovery

- Corrected frame-loss accounting to compare successful Unity uploads with
  source-aware song-time presentation deadlines instead of inferring loss from
  media timestamp gaps. Playback speed, source cadence, and the active FPS cap
  are now included in the expected-frame calculation.
- Expanded Automatic Performance with separate attack and release timing,
  configurable 1–5 FPS steps, repeated bidirectional adjustment, and optional
  oscillation prevention that can hold an unstable upper tier while retaining
  the ability to reduce FPS further.
- Fixed complete OST and DLC preview playback by requesting Beat Saber's
  original level-data variant with a valid `CancellationToken.None` value.
- Hardened normal and Showcase menu transitions against empty retained HMUI
  stacks, unsafe re-entrant dismissal, and premature Solo-flow presentation.
- Fixed playback errors opening an invisible Beat Saber modal behind Big
  Screen and capturing all controller input. Active-menu errors now appear in
  Big Screen's visible center-screen dialog.

## 0.7.0-alpha.5 — Complete built-in song previews

- Fixed Video Library previews for OST and DLC maps looping Beat Saber's short
  menu-audition clip. Big Screen now loads the complete official song audio for
  synchronized preview playback while retaining SongCore's established path
  for custom and WIP maps.
- Reworded the packaged Mods Before Friday description around the advanced
  playback features users can configure, and explicitly documented support for
  adding videos to OST, DLC, custom, and WIP maps without implying that the
  hard-coded Showcase choreography is a user-facing screen editor.
- Restored the exact QPM-pinned RapidJSON revision in GitHub host-test jobs so
  clean CI checkouts can compile the map-configuration tests.

## 0.7.0-alpha.4 — Mods Before Friday package fix

- Fixed the generated QMOD manifest using a UTF-8 byte-order mark that Mods
  Before Friday rejected with `expected value at line 1 column 1`. Big Screen's
  packaging path now writes strict BOM-free UTF-8 on Windows PowerShell 5.1 and
  newer PowerShell versions.
- Added byte-level manifest validation and repository invariants so a QMOD can
  no longer pass release packaging if its `mod.json` would fail MDF import.

## 0.7.0-alpha.3 — Showcase menu re-entry fix

- Fixed reopening Big Screen after playing the bundled Showcase throwing a
  `System.ArgumentOutOfRangeException` and leaving the player trapped in an
  environment-only menu scene. The Showcase launcher now restores Big Screen's
  neutral center controller while HMUI's retained stack is still valid, before
  dismissing the mod menu and entering gameplay.
- Added a repository regression invariant that requires the center-controller
  restoration to occur before the Showcase dismisses Big Screen, preventing a
  future change from attempting to rebuild the cleared HMUI stack after play.

## 0.7.0-alpha.2 — Beat Saber 1.40.8 port

- Added a notification-only Big Screen release checker. It checks the latest
  public stable GitHub release once when the menu first opens in each Beat Saber
  session and shows a popup only when a newer version exists. The Update tab
  displays the installed Big Screen and active yt-dlp versions, while its manual
  check always reports update, current, or unavailable status. A small creator
  credit now sits beneath the installed version without adding gameplay branding.
- Fixed completed YouTube downloads sometimes leaving stale resolution buttons
  visible after replacing a local assignment, added a reliable translucent file
  browser backing panel, and aligned the performance-panel reset control with
  the switch it resets.
- Compacted the thumbnail-picker backing plate around its controls, gave every
  Update-tab text row an explicit non-overlapping layout height, placed creator
  credit directly beneath the installed version, and reordered the tabs to
  General, Screen, Environment, Misc, Update without moving settings between
  categories.
- Condensed the Video Library child editor's song and artist heading onto one
  line and tightened only its noninteractive status/spacer rows so the complete
  Video Storage group remains visible within Beat Saber's fixed-height side
  panel without requiring scrolling.
- Corrected the Update tab's ignored text-row heights and retained scroll
  position so its installed version and creator attribution always open in
  view. Version and creator now share one inseparable display block. The Video
  Library timing controls also use one direct full-width row contract, with
  enforced row heights and explicit labels for both switches.
- Grouped the four full-height synchronization controls with a controlled
  negative spacing that removes the native rows' unused vertical margins, so
  the Playback and Video Storage groups retain their full layouts without
  clipping toggle, slider, label, arrow, or hit-area geometry; ignored local
  diagnostics captures; and corrected the README's renamed menu-environment
  placement workflow.
- Tightened the thumbnail picker's content column and translucent backing plate
  while preserving the full preview, scrubber, and button sizes.
- Kept the preview Play/Pause button inside a 12.5-unit rounded Playback panel,
  positioned its transport row with fixed top and flexible bottom allocations
  so Unity cannot erase the vertical correction, retained the button's own
  0.25-unit optical adjustment, placed the section title across the panel's top
  edge, set storage heading/value pairs to an explicit 70% line height, and set
  the Video Storage background to 17.5 units. Both panels use preferred-only
  sizing rather than hard minimums.
- Split the Windows source-build README into a standalone batch workflow first
  and a complete manual restore, test, build, package, and deploy workflow.

- Removed the playback-only resolution cap. Gameplay, song previews, and Video
  Library previews now present the selected local/downloaded file at its native
  presentation-oriented resolution; the thumbnail picker retains its explicit
  720p utility bound. Older `resolutionHeight` settings are silently removed on
  the next save, while download resolution buttons and codec safety policy are
  unchanged.
- Changed Automatic Performance to FPS-only adaptation. It now lowers the
  session presentation limit in 5 FPS steps to a 15 FPS floor, starts below the
  video's useful cadence including Fit to Song speed, and restores every exact
  reduction in reverse. It no longer reopens FFmpeg, recreates textures, changes
  resolution, or risks interrupting playback during a quality transition.
- Simplified the live and results diagnostics to show actual video resolution,
  source FPS, and the active FPS limit. Power benchmark CSVs use the same native
  resolution schema and preserve older headers in timestamped legacy files.
- Added `docs/BUFFERED_DECODER_DESIGN.md` for a future bounded, timestamped
  ready-frame queue intended to absorb brief decoder stalls. The design records
  memory budgets, MediaCodec constraints, synchronization hazards, FPS-limiter
  interaction, diagnostics, and the required host/on-device test plan.
- Completed the third pre-release hardening review. Non-looping videos now
  park the decoder at both stream boundaries instead of repeatedly decoding an
  opening/final GOP; decoder metrics survive backend swaps; circuit-breaker,
  manifest-write, retained-modal, transient-page, local-scan, and storage-
  cleanup failure paths are contained. Storage Maintenance now protects active
  download staging and every live thumbnail reference, while corrupt cached
  FFmpeg archives are discarded and fetched transactionally.
- Reset Screen and Reset to Defaults now force retained BSML switch graphics to
  match their authoritative settings, including Curved Screen, Show Menu
  Environment, and Show Performance Information. Screen Curve now appears
  immediately below Curved Screen, followed by Maintain Aspect Ratio.
- Reset and normal control refresh now update every retained BSML
  AnimatedSwitchView through its native value listener after changing its bool
  without notifying the settings delegate.
  This keeps all General, Screen, Environment, Update, and Misc switch knobs
  visually synchronized without firing duplicate settings callbacks.
- Kept strict compiler warnings for Big Screen's own code while marking QPM's
  generated dependency headers as system includes so third-party diagnostics
  no longer overwhelm normal build output.
- Suppressed compiler warnings emitted by pinned FFmpeg C sources under the
  newer Android NDK. The suppression is scoped to FFmpeg, retains compiler and
  build errors, is recorded in reproducibility metadata, and invalidates older
  staged runtimes without weakening Big Screen's own warning policy.
- Fixed the staged-runtime validator's software-HEVC check so the valid
  `CONFIG_HEVC_MEDIACODEC_DECODER` entry no longer forces FFmpeg 9 to rebuild
  on every otherwise incremental build.

- Fixed clean first-time deployment through `Build-And-Deploy.bat`: direct ADB
  deployment and QMOD packaging now share one generated runtime manifest, so
  the embedded CPython, yt-dlp, QuickJS provider, certificates, native Python
  extensions, and legal notices are installed even when no earlier Big Screen
  runtime exists on the headset. This resolves `BS-DL-INIT-101` after a clean
  install. Fresh settings now also default **Disable Rotation and Motion** to
  ON while preserving inversion of the legacy positive-state setting.

- Made `Build-And-Deploy.bat` work from a fresh clone or downloaded source
  archive by restoring QPM packages, resolving the pinned Windows NDK, and
  verifying/installing the separate pinned WSL NDK before CMake runs. The BAT
  now discloses all possible first-run network downloads and asks permission;
  individual fetchers report cache reuse or the exact component/source being
  downloaded. Added a complete dependency and manual-build inventory under
  `docs/DEPENDENCIES.md`. FFmpeg now builds and installs within a path-safe WSL
  cache before staging into the repository, so extracted checkout names with
  spaces or parentheses no longer break FFmpeg 4's generated shell scripts.

- Added Set Thumbnail to the map video editor for maps with a local video: a
  center-screen frame picker with a scrubber, single-frame stepping, and a
  time/frame readout saves any exact frame as the map's thumbnail PNG. Picking
  is read-only decoding — the video file is never modified. A map keeps one
  picked thumbnail: re-picking replaces it in place, unlinking the video keeps
  it so relinking restores the artwork, permanently deleting the local file
  deletes it, and Storage Maintenance only offers a picked PNG for cleanup
  after no map manifest entry references it.
- Deleting a LOCAL video file now asks for one more explicit confirmation that
  names the file and states it cannot be restored. Unlink remains one step, and
  re-downloadable YouTube videos keep the original single confirmation.
- The exact-resolution DOWNLOAD 480p/720p/1080p/1440p buttons now share the
  whole action row at one uniform size with even spacing and auto-fitting
  labels, instead of the cramped fixed-width group with clipped text.

- Retargeted the Quest package to Beat Saber 1.40.8 (`1.40.8_7379`) with
  bs-cordl 4008 bindings and the matching current Quest core dependencies.
- Migrated from Paper 3 to Paper2 Scotland2, updated CustomTypes declarations
  and generated nested-type names, and adapted preview-audio and environment
  transition APIs changed by Beat Saber 1.40.8.
- Preserved the map's original environment while applying Big Screen or
  mapper-requested overrides to Beat Saber's new target-environment field.
- This is a compile-validated alpha port. Full UI, gameplay, Chroma/Noodle,
  Replay, downloader, hardware-decoder, and showcase behavior still requires
  validation on a Quest running the exact 1.40.8 APK.

- Fixed development deployments leaving an older Big Screen binary in the
  opposite Scotland2 load phase. Loading both copies initialized CPython twice
  and could abort Beat Saber during startup.
- Completed the August 15 release-hardening review: decoder CPU/peak/allocation
  totals now survive facade shutdown, all five automatic-performance steps
  recover in exact reverse order, software and hardware decoding share crop and
  colorspace handling, and gameplay hooks preserve Beat Saber's original calls
  when Big Screen preparation or UI work fails.
- Downloader and storage ownership is stricter: URL thumbnails are best effort,
  metadata-check failures are labeled correctly, hidden resolution dialogs
  cancel their probes, menu teardown cancels only its own task, stale status
  files cannot revive failed jobs, WebM and abandoned replacement backups are
  cleanable, and local scans can stop without blocking a soft restart.
- Added behavioral codec-policy coverage for H.264, H.265/HEVC, VP8, VP9,
  HDR/10-bit rejection, and the hardware-only greater-than-1080p boundary.
- The managed showcase now receives a temporary No Fail modifier only when it
  is launched from Big Screen; normal launches retain the player's modifiers.
- Added Show Menu Environment for scenery, lighting, and floor together, plus
  an independent Show Lane Guides control for coordinate-referenced placement.
  Every captured menu renderer and light is restored at lifecycle boundaries.
- Resolve the renamed Beat Saber 1.40.8 menu-environment hierarchy through
  its BasicMenuGround renderer while preserving controller/pointer visuals.
- Raised the Screen Size Multiplier ceiling to 8.0x for both flat and curved
  layouts now that the menu floor can be hidden to expose the full canvas.
- Expanded docked Screen Distance, X, Y, and Tilt controls to +/-180 so an 8x
  canvas retains unrestricted placement and rotation authority.
- Prevented large or distant menu previews from strobing black backing-mesh
  fragments by scaling picture-layer separation with canvas size and skipping
  the backing renderer entirely when the picture already covers the frame.
- Per-layout Reset Screen now refreshes the visible state of every affected
  animated switch, including Advanced Screen Controls and Curved Screen.

## 0.6.6 — Beat Saber 1.37 alpha baseline

- Quest-native H.264 video playback for OST, DLC, custom, and WIP songs.
- Video Library with mapper, YouTube, map-folder, and Video Import sources.
- Added explicit per-source download buttons for every available 480p, 720p,
  1080p, and 1440p tier in the Video Library plus the same tier chooser on song
  selection. Replacement confirmation preserves the prior assignment until the
  new file commits, and 1440p carries its hardware-only warning.
- Added H.265/HEVC, VP8, and VP9 playback policy, WebM local-file support,
  1440p output limiting, HDR/10-bit rejection, and codec-aware diagnostics.
- Five flat/curved layouts, pause/song-selection controls, environment cleanup, Cinema/Chroma and Replay compatibility.
- Advanced layout-scoped screen rotation, video rotation/zoom/pan/perspective tilt, stretch or transparency-aware letterboxing, and controller-based undocked screen placement.
- Embedded CPython/yt-dlp plus an in-process QuickJS-NG challenge engine, with verified updates, provider compatibility testing, rejection, and rollback.
- Reproducible, hash-pinned source builds for QuickJS-NG, yt-dlp, and yt-dlp-ejs, including byte-for-byte release-payload comparison.
- Automatic library recovery, storage maintenance, performance diagnostics/reduction, error containment, host tests, and CI.
- Release hardening adds HTTPS YouTube mapper URL restrictions, metadata/list bounds, decoder-worker exception containment, pinned CI actions, and packaged third-party notices.
- Downloader hardening adds bounded QuickJS input/output/stack use, exception-safe Python ownership, real EJS startup validation, transactional update activation, stale native-extension pruning, fresh QMOD verification, and source-rebuild CI.
- Added an optional Misc-tab Up & Down showcase with a center readiness page, live Chroma/Noodle checks, separate user-triggered map/video downloads, hash-pinned BeatSaver installation, a motion warning, and keyed direct Lawless Expert+ launch. Post-song navigation remains Beat Saber's stock flow.
- Polished the Up & Down showcase with a lane-safe four-panel fly-by, an
  arena-sized flat punchline, denser upper/lower flapping and floating fields,
  timed side-pillar removal, a wider corkscrew entrance, and a twelve-panel
  closing corridor.
- Added one deterministic authored showcase glass sequence: every downward
  impact from about 2:03 accumulates cracks across one 200-piece pane, then the
  current video frame freezes onto the shards as they tumble well below the
  runway at about 2:15. Unrelated sections no longer receive generic fracture
  demonstrations.
- Hardened the showcase navigation lifecycle so the motion warning cannot
  survive as an input-blocking retained modal. Gameplay completion clears all
  launcher state and leaves Beat Saber's Results, Replay, and Continue paths
  unchanged, so reopening Showcase always starts from an idle readiness page.
- The movable performance panel now persists one shared menu/gameplay
  placement and includes an adjacent reset-glyph recovery button. Video
  Library tier actions use larger `DOWNLOAD 1080p`-style labels.
- Playback hardening adds source-aware/VFR timing, reusable RGBA buffers, full request-cost diagnostics, gameplay decoder prewarming, and material-state updates only when invariants change.
- Menu and storage work is rate-limited and cached; downloader progress avoids per-block flash synchronization, network waits are bounded, and error dialogs back off safely.
- The runtime now uses CPython 3.14.7 and one pinned Android NDK r27d toolchain. Reproducible LGPL FFmpeg 4.4.8 and 9.0.1 builds remain side by side for controlled on-headset comparison.
- Expanded host/CI coverage validates downloader policies, repository/toolchain/license invariants, real H.264 decoding, portrait media, worker shutdown, and RGBA buffer reuse.
