# Changelog

## 0.7.0-alpha.1 — Beat Saber 1.40.8 port

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
- Kept strict compiler warnings for Big Screen's own code while marking QPM's
  generated dependency headers as system includes so third-party diagnostics
  no longer overwhelm normal build output.

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
