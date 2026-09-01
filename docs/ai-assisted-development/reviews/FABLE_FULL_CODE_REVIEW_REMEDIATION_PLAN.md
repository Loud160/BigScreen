# Fable full-review verification and remediation plan

This is the living implementation and Quest-validation record for the findings
in [FABLE_FULL_CODE_REVIEW.md](../../FABLE_FULL_CODE_REVIEW.md). The external
review is evidence and review input, not an instruction set. Every change in
this plan remains subject to verification against the current Big Screen code,
focused automated tests, and an on-headset regression pass.

The initial verification was performed against `main` commit
`6cbf791a8cee05a07a0ed2322923dcd931e6618b` on August 29, 2026. No behavior was
changed during that verification.

## Accepted product decisions

- Multiplayer and tutorial gameplay are unsupported for now. Big Screen must
  remain inert in those modes: it must not start a retained Solo/Campaign
  video, create screens, or apply video-related environment changes.
- Quest 2 is the primary development and acceptance headset. Quest 3 and Quest
  3S are field-tested by BSMG community testers. So far their reported defects
  have also reproduced on Quest 2; no Quest 3- or Quest 3S-specific defect is
  known. Public wording should distinguish primary Quest 2 validation from
  successful Quest 3/3S community testing without claiming identical internal
  hardware behavior.
- Active downloads may continue independently of the currently visible map, as
  designed. If a download fails or is cancelled, its incomplete staging file,
  `.part` file, and identity metadata must be discarded when the player leaves
  the originating map. A successful completed download is not removed.
- Public GitHub releases continue to contain the QMOD only. Native debug
  binaries may be retained as short-lived Actions-only artifacts for crash
  symbolization, but must not be added to public release assets.
- Disabled experimental bloom/color work is intentionally preserved. Review
  cleanup must not remove that work merely because it is compiled out.

## How this document is maintained

Each stage moves through these states:

1. `Planned`
2. `Implementation in progress`
3. `Automated verification passed`
4. `Awaiting Quest test`
5. `Quest validated`, or `Regression found`

After a stage is implemented, record its commit, automated test results, build
artifact, and Quest deployment method. After the user tests it, record the
reported result and any regression before starting the next stage. A stage is
not complete merely because it compiles.

Each implemented stage is checkpointed on its own
`codex/fable-review-stage-N` branch before work begins on the next stage. If an
earlier headset pass exposes a regression, that stage's branch can be fixed in
isolation and the corrective commit can then be applied to later stage branches
without mixing unrelated unfinished work.

| Stage | Scope | Status | Commit | Quest result |
| --- | --- | --- | --- | --- |
| 1 | Beta correctness, privacy, and unsupported-mode gate | Quest validated | `94ab939` (combined Stage 1/2 checkpoint) | No obvious regression in the user's initial focused pass |
| 2 | Unity screen-state and lifetime safety | Quest validated | `94ab939` on `codex/fable-review-stage-2` | User reported no regression in the focused Stage 2 pass |
| 3 | Video Library cache and menu responsiveness | Quest validated | `537678b` plus the live-update correction on `codex/fable-review-stage-3` | On August 30, 2026, the user verified that menu timing and screen edits remained responsive without interrupting video playback and could not reproduce the prior crash |
| 4 | Deployment, removal, and ownership parity | Host and focused Quest verification passed | Stage 4 checkpoint on `codex/fable-review-stage-4` | Source deploy/update/removal/reinstall, device selection, and support helpers verified; destructive user-data choices remain intentionally untested |
| 5 | Downloader operation cleanup and diagnostics | Quest validated | Stage 5 working tree on `codex/fable-review-stage-5` | User reported no issues in the focused downloader and URL-entry pass |
| 6 | Packaging, CI, and dependency reproducibility | Automated verification passed; final MBF test deferred | `b848661` on `codex/fable-review-stage-6` | Final clean MBF/package validation intentionally deferred until all remediation stages are complete |
| 7 | GPU presentation-path overhead | Awaiting Quest test | Stage 7 checkpoint on `codex/fable-review-stage-7` | Pending |
| 8 | Catalog lifetime and transport-state consolidation | Planned | — | Pending |
| 9 | Documentation and contained low-risk cleanup | Planned | — | Pending |

## Verified findings and disposition

### High severity

| Finding | Verdict | Planned correction | Stage |
| --- | --- | --- | --- |
| H-1: incompatible `.part` resume | Valid | Give incomplete transfers a stream-identity sidecar containing the canonical source/video identity, requested tier/FPS, selected yt-dlp format, transport/fallback mode, and downloader version where relevant. Resume only an exact match. Remove failed/cancelled staging data when leaving its originating map. | 1 |
| H-2: persistence failure trips the internal circuit breaker | Valid | Give expected write/rename/storage failures a dedicated persistence result or exception. Report them persistently and visibly without counting them as internal failures. Preserve `ReportInternal` for invalid generated state and unexpected programming failures. | 1 |

### Medium severity

| Finding | Verdict | Planned correction | Stage |
| --- | --- | --- | --- |
| M-1: every save clears the descriptor cache | Valid | Invalidate only the changed level IDs. Reserve full invalidation for full reload/recovery. | 3 |
| M-2: unreachable yt-dlp rollback/retry | Valid | Remove the in-`Run()` retry. Retain the updater's transactional staging and smoke-test rollback. | 5 |
| M-3: unredacted exception diagnostics | Valid | Redact at the Python downloader boundary before status persistence, with a second persistence-boundary defense where appropriate. | 1 |
| M-4: lead-in material changed before state commit | Valid | Complete all fallible preparation before mutating materials and committing lead-in state. | 2 |
| M-5: inconsistent Unity liveness guards | Valid | Use shared `UnityW::isAlive` helpers for cross-frame screen, material, renderer, and transform access. | 2 |
| M-6: timing sliders save and restart on every callback | Valid | Update visible values immediately, debounce persistence, then update timing by flushing/seeking the existing decoder queue. Toggle and reset actions can remain immediate without rebuilding FFmpeg, MediaCodec, textures, or audio. | 3 |
| M-7: JSON Issues synchronously parses the catalog | Valid | Build an issue index/count during incremental metadata prewarming. Show a preparing state instead of forcing a synchronous full scan. | 3 |
| M-8: Linux deployment safety differs from PowerShell | Valid | Port partial-receipt recovery validation, managed-receipt checks, strict deployment retirement, and baseline restoration. Removal remains ownership-aware and intentionally hash-lenient for Big Screen-exclusive files. | 4 |
| M-9: packaged-library list is triplicated | Valid | Treat `mod.template.json` as canonical, derive packaging inputs, and fail when CMake staging differs. | 6 |
| M-10: workflow artifact/comment mismatch | Valid | Keep public releases QMOD-only, correct comments, enforce tag-to-manifest version equality, and retain unstripped symbols only as a private Actions artifact. | 6 |
| M-11: PowerShell suites are not executed | Valid | Run them in CI without adding PowerShell to the ordinary Linux/WSL build requirements. Add Python policy-parity tests where practical. | 4 and 6 |
| M-12: Chroma difficulty data parsed synchronously | Valid | Cache parsed mapper instructions by map path, characteristic, difficulty, and file fingerprint, then apply them to the current user/base configuration. | 3 |
| M-13: raw `BeatmapLevel*` survives SongCore refresh | Valid lifetime risk; not reproduced | Make stable level ID plus catalog generation authoritative and re-resolve managed objects after refresh. Reject callbacks using stale generations. | 8 |
| M-14: repeated per-frame shader/property setup | Valid | Cache property IDs, texture bindings, and the last applied conversion/effect state; update only changed state. Measure before and after. | 7 |
| M-15: restored packaged binary lacks a content hash | Valid | Commit and verify SHA-256 values for QPM-restored native inputs, at minimum the packaged `libbeatsaber-hook.so`. | 6 |

### Low severity and verification items

| Finding | Disposition | Stage |
| --- | --- | --- |
| Low-1 through Low-4: probe bounds, probe failure accounting, normalizer wording, diagnostic-throttle reset | Correct all four in the shared downloader operation path. | 5 |
| Low-5: geometry rollback retains mismatched deformation caches | Snapshot/restore the complete geometry/deformation state transactionally. | 2 |
| Low-6: CinemaScreen “Exact” accepts arbitrary suffixes | Require the canonical ID or a hierarchy-separator-qualified suffix and add fixtures. | 3 |
| Low-7: clone lights allegedly never unregister | Rejected without runtime evidence. Unity destruction invokes `LightWithIdMonoBehaviour::OnDisable`; adding explicit unregistering risks duplicating lifecycle work. Reopen only if diagnostics prove stale registrations. | Not scheduled |
| Low-8 and Low-9: dead diagnostics surface and disabled bloom normalization | Remove truly dead diagnostics code; normalize compiled-out state without deleting the intentionally preserved experiment. | 9 |
| Low-10: duplicated descriptor-to-editor blocks | Consolidate only with focused editor/status-label regression tests. | 8 |
| Low-11 and Low-12: missing level-ID guards and duplicated hover hints | Add defensive checks and one canonical hint source. | 9 |
| Low-13 through Low-17: logging command construction, ADB selection, Linux `--yes`, weak preserve-list test, silent optional tests | Correct tooling behavior and improve explicit skipped-test reporting. | 4, 6, and 9 |
| Low-18: privacy page omits Big Screen's release check | Add exact once-per-session and manual-check behavior. | 1 |
| Low-19 through Low-22: stale Paper wording, browser formats, crash-test macro, Quest support wording and grammar | Align maintained documentation and repair the native-logger crash-test build path. | 9 |
| Low-23: mixed-install removal differs by host | Align results while preserving MBF/shared ownership boundaries and user-requested removal behavior. | 4 |
| Multiplayer/tutorial stale prepared playback | Treat as a supported-mode authorization defect even before reproduction. Arm gameplay only from Standard/Mission transition hooks and clear authorization on every terminal path. | 1 |
| Preview transport boolean residue | Replace the overlapping booleans with a tested state machine rather than patching one flag. | 8 |
| Sprite LRU versus bound cells | Verify while revising cache behavior; do not destroy sprites still assigned to bound cells. | 3 |
| Release-asset mutability | Does not alter M-15: local hashes remain required for byte reproducibility. | 6 |
| Shader lookup failure frequency | Changes only M-4 severity, not the required transactional ordering fix. | 2 |

## Stage 1 — Beta correctness, privacy, and unsupported-mode gate

### Implementation

- Add exact incomplete-download identity and lifecycle ownership.
- Delete failed/cancelled staging data when its originating map is left.
- Keep active and successful background downloads working.
- Separate expected persistence failures from internal circuit-breaker failures.
- Redact downloader diagnostics before any status or log file receives them.
- Disclose the automatic Big Screen release check in the privacy document.
- Arm gameplay video/environment work only from supported Standard or Mission
  transitions. Multiplayer and tutorial must remain inert.
- Add host/invariant tests for each boundary.

### Focused Quest test after this stage

1. Start a high-resolution download, cancel or force a failure, leave the map,
   return, and confirm no stale transfer resumes.
2. Start one resolution, cancel/fail it, then select a different resolution for
   the same map. Confirm the resulting video plays and is not corrupt.
3. Let an active download continue while moving to another map and confirm a
   successful completion still assigns the correct video.
4. Trigger an ordinary library-save/storage error if a safe test fixture is
   provided. Confirm the popup is understandable, the menu remains usable, and
   repeating it does not disable Big Screen.
5. Produce a downloader failure and inspect a support bundle for signed query
   strings, authorization values, cookies, or PO tokens.
6. Select a Solo map with video, then enter multiplayer and tutorial. Confirm
   there is no stale screen, decoder, environment suppression, or Big Screen
   performance session.
7. Recheck normal Solo, Practice, Campaign, restart, fail, and ordinary exit.

### Most likely regression signs

- Downloads no longer resume when they should, or completed files are deleted.
- A background download is assigned to the wrong map.
- Big Screen disables itself after a recoverable storage failure.
- Solo/Campaign video does not start because the gameplay authorization was
  cleared too early.
- Environment switches affect a multiplayer/tutorial scene.

### Result

Implementation and automated verification completed on August 29, 2026.

- Resumable transfers now carry an atomic identity sidecar binding the partial
  bytes to the map, canonical source, requested resolution/FPS, selected
  format/protocol, fallback policy, and active yt-dlp version/channel. A
  missing or mismatched identity starts from byte zero.
- Active downloads remain process-owned across menu/map changes. Successful
  publication removes only staging data; failed/cancelled staging remains
  resumable while its originating map is selected and is discarded after the
  player leaves that map.
- `library.json` write/replace failures use a dedicated persistence exception.
  Menu callers retain the prior durable state, record the technical storage
  failure, show an actionable message, and do not increment the internal
  circuit breaker.
- Downloader error/status text applies one shared redaction policy before
  persistence. Authorization values, cookies, PO tokens, visitor tokens, and
  URL queries/fragments are covered by executable tests.
- The privacy page now documents the once-per-session background Big Screen
  release check, the manual check, its exact GitHub API endpoint, and the fact
  that it neither downloads nor installs a mod update.
- Gameplay surface creation, environment work, timing updates, and performance
  work are authorized only by Standard (Solo/Practice) or Mission (Campaign)
  transitions. Shared hooks remain inert in multiplayer/tutorial flows.

Automated evidence:

- Canonical host suite: 14/14 tests passed.
- Embedded downloader identity/redaction tests: passed.
- Repository lifecycle/privacy invariants: passed.
- Full ARM64 Quest build and validated QMOD packaging: passed.
- Artifact: `Big Screen.qmod`, 19,691,646 bytes.
- SHA-256: `c9c2d2ee102234d196437bd6b2721b44335c377df5d36db1add66bb6e7776ab3`.

Quest result: the ownership-safe source deployment completed on August 29,
2026, with every payload hash verified. The user reported no obvious breakage
in the initial focused pass and accepted moving to Stage 2. This records the
observed result without claiming that every optional failure fixture above was
forced on the headset.

## Stage 2 — Unity screen-state and lifetime safety

### Implementation

- Make lead-in activation transactional.
- Apply consistent Unity fake-null/liveness guards.
- Roll back complete mesh and deformation state on presentation failure.
- Add host tests where state is native-testable and invariant checks for the
  lifecycle rules that require Quest validation.

### Focused Quest test after this stage

1. Test positive and negative video offsets, including black and transparent
   lead-in behavior.
2. Start, pause, resume, seek, loop, and leave preview while video is active.
3. Switch between Embedded Video Shader modes and GPU/CPU presentation paths.
4. Exercise flat, curved, opacity, vignette, mapper-sized, and additional-screen
   Cinema maps.
5. Play the Showcase through deformation and shatter sections, then restart and
   exit early.
6. Rapidly switch maps and close/reopen Big Screen, watching for crashes,
   momentary black screens, stale panels, or screens that survive teardown.

### Most likely regression signs

- Video remains black after lead-in or seek.
- Screen opacity/background changes unexpectedly.
- Curved or deformed geometry snaps, corrupts, or uses the previous map's mesh.
- Screens disappear after a shader/presentation fallback.
- Crash or empty environment while closing the menu or leaving gameplay.

### Result

Implementation and automated verification completed on August 29, 2026.

- Black and transparent lead-in requests now verify the complete live Unity
  presentation set before committing visibility, material, backing-layer, or
  lead-in flags. Leaving a lead-in also restores its flags if presentation
  recovery cannot yet proceed, allowing a later decoded frame to retry.
- `ApplyPresentation` resolves shaders and the backing renderer before changing
  the live material. After that preflight, the video material, optional alpha
  guard, non-emissive backing, opacity, and renderer visibility are committed
  without another fallible shader lookup.
- Cross-frame presentation queries and public transform, scale, roll, opacity,
  culling, and deformation mutations now use Unity fake-null-aware liveness
  checks instead of trusting non-null IL2CPP pointers.
- Live geometry replacement snapshots the complete old mesh, dimensions,
  configuration, coverage flags, deformation vertex/UV buffers, and applied
  state. Mesh-generation or presentation failure destroys only the candidate
  meshes and restores the entire previous state as one transaction.
- Repository invariants verify the preflight-before-mutation ordering, lead-in
  retry state, liveness policy, and both geometry rollback exits.

Automated evidence:

- Canonical host suite: 14/14 tests passed.
- Repository screen-lifecycle invariants: passed.
- Full ARM64 Quest build and validated QMOD packaging: passed.
- Artifact: `Big Screen.qmod`, 19,692,295 bytes.
- SHA-256: `532aa225223a734661ca3bc9e8218a647e3735e15533581d4d7b5824ec1641bc`.

Quest result: the ownership-safe source deployment completed with every payload
hash verified. The user completed the focused Stage 2 pass without finding an
obvious regression and approved moving to Stage 3.

## Stage 3 — Video Library cache and menu responsiveness

### Implementation

- Replace global descriptor-cache clearing with targeted invalidation.
- Debounce continuous timing-slider persistence and in-place decoder seeking.
- Maintain JSON-issue information during incremental metadata prewarming.
- Cache Chroma difficulty parsing by an invalidatable file fingerprint.
- Tighten CinemaScreen exact-ID matching.
- Ensure thumbnail sprites assigned to visible cells survive LRU eviction.

### Focused Quest test after this stage

1. Open the library, enter a map, change settings, return, and confirm every
   visible song title, thumbnail, error badge, and video badge is still correct.
2. Scroll and use jump letters before and after editing several maps.
3. Move offset and playback-speed sliders continuously. Values should update
   smoothly; after movement settles the picture should seek to the new mapping
   without pausing song audio or rebuilding the decoder/screen.
4. Leave immediately after moving a slider, reopen the map, and verify the final
   value persisted.
5. Open JSON Issues with a large library. Confirm there is no long UI freeze and
   that preparing/completed counts are honest.
6. Test valid and malformed Cinema/Chroma maps, difficulty changes, mapper screen
   placement, additional screens, and environment-only metadata.
7. Install/refresh a new SongCore map and confirm it appears without restarting
   Beat Saber.

### Most likely regression signs

- Rows revert to “checking metadata,” lose thumbnails, show the wrong song, or
  shift when hovered.
- A changed map keeps stale timing, URL, error, or mapper metadata.
- The final slider value is lost or the preview continues using an older value.
- JSON Issues misses maps or blocks the UI.
- Chroma preview differs from gameplay after difficulty changes.

### Result

Implementation and automated verification completed on August 29, 2026.

- Durable one-map library edits invalidate only that level's parsed descriptor.
  Full cache invalidation remains reserved for full manifest load/recovery.
- Playback-speed and offset movement updates the visible controls immediately,
  then coalesces persistence and an in-place decoder seek until 250 ms after
  input settles. Map changes, editor exit, metadata refresh, and menu
  deactivation flush the final pending value before their navigation boundary.
- Incremental catalog preparation now maintains an O(1)-updated Cinema JSON
  issue index. Its title button reports honest checked/total progress and its
  dialog reads the native index rather than synchronously parsing every map.
- Selected-difficulty Chroma preview instructions are cached by normalized map
  path, characteristic, difficulty, and `.dat` file fingerprint, then replayed
  against the current user/Cinema base configuration on each cache hit.
- Exact Chroma matching accepts only `CinemaScreen` or a hierarchy-qualified
  final path component; arbitrary names ending in `CinemaScreen` are rejected.
- Off-screen row models no longer retain raw thumbnail sprite pointers. The LRU
  pins sprites bound to visible virtualized cells, retires explicit replacements
  safely, and destroys them only after a subsequent binding pass releases them.

Automated evidence:

- Canonical host suite: 14/14 tests passed.
- Chroma cache/current-base and exact-ID fixtures: passed.
- Repository debounce, incremental-index, targeted-cache, and thumbnail-lifetime
  invariants: passed.
- Full ARM64 Quest build and validated QMOD packaging: passed.
- Artifact: `Big Screen.qmod`, 19,710,148 bytes.
- SHA-256: `2755b5bedb22b46fa4f5e899939a748b94d3cd2d10a72ccf198746e0652dfc6a`.

### Quest regression and corrective implementation

The exact `537678b` Stage 3 library was later deployed and verified by SHA-256
before testing. Ordinary cache, row, and metadata behavior appeared correct,
but repeatedly changing timing while previewing a 2560x1440 hardware-decoded
video eventually ended Beat Saber with `SIGKILL`. Android `lmkd` kill records,
the system memory-factor transition, and the absence of a tombstone established
that this was a Quest low-memory kill rather than a native access violation.
Screen recording, the 64 MiB read-ahead reserve, MediaCodec/driver allocations,
GPU textures, and repeated full preview reconstruction collectively left too
little system headroom. The Stage 3 debounce reduced reconstruction frequency,
but its settled callback still reused `StartSelectedPreview`, which closed and
recreated the complete high-resolution decoder and presentation session.

The corrective implementation separates update ownership explicitly:

- Offset, playback speed, Fit to Song, and lead-in background update the cached
  timing in place. Mapping changes invalidate and seek the existing decoder's
  read-ahead generation while audio continues and the last uploaded picture
  remains visible until its replacement arrives.
- Screen X/Y/distance/rotation changes update only the existing Unity transform.
- Size, curvature, aspect, opacity, and picture-framing changes transactionally
  replace meshes/material state around the existing decoded texture.
- Cinema additional screens update their shared-texture geometry in place; a
  changed clone count rebuilds only that lightweight presentation group.
- FFmpeg version, hardware decoding, embedded shader, GPU conversion, and YUV
  upload layout retain an explicit full-rebuild path because those choices are
  fixed when decoder/material resources are created.
- Live material updates now account for vignette-authored alpha and mapper
  blending changes, and CPU visual-effect changes invalidate only prepared
  pixels rather than the complete playback session.

Corrective automated evidence:

- Canonical host suite: 14/14 tests passed.
- New repository invariants require timing and display paths to contain no
  `Stop`, decoder close, or full preview initialization; pipeline toggles must
  use the separately named reconstruction path.
- Warning-clean full ARM64 Quest build and validated QMOD packaging: passed.
- Artifact: `Big Screen.qmod`, 19,715,363 bytes.
- SHA-256: `b6585cfad2e6e71244e9ae26156949dc91ac3f6b51605c5850a81f47b63747f5`.

Focused Quest retest for this correction:

1. Play a 1440p video in the Video Library and repeatedly change offset,
   playback speed, Fit to Song, and lead-in background. Song audio must never
   pause or restart. The picture may jump to its newly synchronized frame but
   must not disappear or recreate the screen.
2. While the same preview plays, change distance, X, Y, tilt, roll, and layout.
   Movement must be continuous with no audio interruption or first-frame flash.
3. Change size, curvature, Maintain Aspect Ratio, video rotation/zoom/X/Y/tilt,
   opacity, stretch, and letterbox transparency. The current picture must stay
   visible while the mesh changes.
4. On Cinema maps with additional screens, toggle Respect Mapper Settings and
   verify primary/additional panels appear, disappear, and update without
   restarting playback.
5. Separately toggle FFmpeg, hardware decoding, Embedded Video Shader, GPU
   conversion, and Consolidated YUV Upload. These controls should still perform
   one deliberate reconstruction and resume at the retained song position.
6. Repeat the 1440p timing stress test while recording. Check that Beat Saber
   remains alive and that logs show `Applied live Video Library timing` rather
   than repeated decoder startup lines.

### Corrective Quest result

On August 30, 2026, the corrected Stage 3 build was installed through the
ownership-safe source deployment path and its installed native-library hash was
verified against the local build. The user tested the Video Library controls and
reported that everything in the menu appeared to work correctly. Timing and
screen changes no longer stopped the video, making continuous adjustment notably
better to use, and no menu operation tested reproduced the earlier game crash.

Stage 3 is therefore **Quest validated**. The code remains on
`codex/fable-review-stage-3`; Stage 4 remains isolated on its separate branch.

## Stage 4 — Deployment, removal, and ownership parity

### Implementation

- Give Python/Linux deployment the same receipt recovery, ownership assertions,
  retired-path validation, and baseline restoration as PowerShell/Windows.
- Preserve the intended difference between strict deployment and permissive
  removal of confirmed Big Screen-exclusive files.
- Align mixed MBF/source removal outcomes without deleting MBF-owned or shared
  dependency files.
- Use the shared multi-device Quest selector in every ADB-touching script.
- Add/execute cross-host ownership fixtures and the PowerShell suites in CI.

### Focused host/Quest test after this stage

1. Build QMOD on Windows/WSL and native Linux and compare hashes.
2. Source-deploy on Windows and Linux, then verify every receipt entry and file
   hash on Quest.
3. Update an existing source install and verify retired files restore their
   recorded baseline when appropriate.
4. Interrupt deployment deliberately at a documented safe point, rerun it, and
   verify recovery.
5. Confirm source deployment refuses MBF-managed and ambiguous ownership before
   changing files.
6. Test removal choices independently: mod only, settings, and downloaded videos.
7. With multiple Android devices connected, confirm each tool selects a Quest or
   asks which Quest to use.
8. Confirm MBF can connect after the scripts complete and ADB is stopped.

### Most likely regression signs

- Windows and Linux produce different QMOD bytes.
- An update deletes a shared dependency or fails to restore a prior file.
- A mixed install becomes unremovable or silently damages MBF ownership.
- Removal deletes settings/videos without the matching confirmation.
- A script targets a phone or the wrong Quest.

### Result

Implementation was rebuilt and automated verification was completed on
August 30, 2026, on `codex/fable-review-stage-4`. The branch now starts from
the Quest-validated Stage 3 correction at `42934a1`; the preceding Stage 4 tip
is preserved separately as `codex/fable-review-stage-4-pre-live-stage3`.

- Linux/Python source deployment now rejects changed receipt-owned bytes before
  mutation, resumes only provable partial states, strictly validates retired
  payload hashes, and restores a verified recorded baseline when appropriate.
- Both host paths retain the preceding complete receipt while a new partial
  plan exists. An interrupted retirement pass is replayed on the next deploy,
  while an already-restored exact baseline is accepted as completed work.
- Confirmed uninstall remains intentionally permissive for exact
  `BigScreenExclusive` receipt paths, but shared dependencies are preserved.
  Linux now matches Windows by allowing guarded mixed source/MBF cleanup only
  from readable receipts and by excluding every path claimed by MBF.
- Live logging, tombstone retrieval, and standalone game restart now use the
  shared Quest/Beat Saber device selector. The logging helper no longer uses
  `Invoke-Expression`; regex and output-file inputs stay data, not code.
- The documented Linux `--yes` option is accepted, optional CTest omissions
  produce visible warnings, and the three Windows-specific PowerShell policy
  suites are now executed in GitHub Actions without becoming Linux build
  prerequisites.

Automated evidence:

- Canonical host suite: 14/14 tests passed.
- Python/Linux ownership, removal, deterministic package, dependency, and
  device-selection fixtures: passed.
- PowerShell source-ownership, dependency, deterministic ZIP, and parser
  checks: passed.
- Full ARM64 Quest build and validated QMOD packaging: passed.
- Artifact: `Big Screen.qmod`, 19,715,363 bytes.
- SHA-256: `b6585cfad2e6e71244e9ae26156949dc91ac3f6b51605c5850a81f47b63747f5`.

Focused Quest verification completed on August 30, 2026:

- The canonical Windows source-deploy path selected the authorized Quest 2
  while an unrelated unauthorized Android device was also connected. All 95
  receipt entries matched their deployed SHA-256 values, no partial receipt
  remained, and the native library matched
  `19b6d18f36f620c0a25e2259f2547d17871988a924a6155276ba947d29606bcd`.
- Repeating source deployment correctly classified the existing install as
  `SOURCE_MANAGED`. Mod-only removal then removed the native/runtime payload
  and receipt while preserving the settings file, library data, and all seven
  managed video files with their original hashes.
- That live removal/reinstall cycle exposed a false legacy-install diagnosis:
  generated updater/status/cache files intentionally left under `Runtime`
  were being treated as an old embedded runtime. Windows and Linux now detect
  legacy installs only from immutable shipped runtime markers. Policy tests
  cover generated-only and real shipped-runtime cases. A second real removal
  and reinstall consequently classified the headset as `NOT_INSTALLED`, did
  not request destructive legacy migration, and recreated a complete
  zero-mismatch receipt without altering user data.
- Standalone restart, tombstone retrieval, and live logging each selected the
  Quest 2 rather than the unrelated device. Restart issued the expected Beat
  Saber launch intent; tombstone retrieval pulled the newest 513,978-byte
  record; and a bounded live-log test wrote 32 lines (8,128 bytes).
- The QMOD is still byte-identical to the validated Stage 3 package because
  Stage 4 changes only host deployment, removal, logging, policy, CI, tests,
  and documentation—not the packaged Quest payload.

Tests that would destroy user data or require unavailable external state were
not forced merely to satisfy the checklist: settings/video deletion choices,
an actual MBF-owned conflict, two simultaneously authorized Quests, a
deliberately interrupted live deployment, and a native-Linux USB deployment.
Their policy paths remain covered by the automated cross-host fixtures.

## Stage 5 — Downloader operation cleanup and diagnostics

### Implementation

- Remove the unreachable in-runtime yt-dlp rollback retry.
- Apply bounded timeout/retry behavior to URL probes.
- Keep probe failures separate from completed-download failure streaks.
- Report software validation only when it actually ran.
- Reset diagnostic throttling for every queued operation.
- Preserve stable/nightly updater behavior, HLS fallback, remux, transcode, and
  three-failure guidance.
- Distinguish YouTube's temporary network/IP verification challenge from videos
  that genuinely require an account. The popup recommends changing networks,
  waiting several hours, or checking for a yt-dlp update; it deliberately does
  not suggest Meta Quest Browser sign-in.
- Make clipboard paste a local field operation rather than a validator/network
  action. Check accepts scheme-less YouTube links for the Quest keyboard, reads
  the currently visible field text, canonicalizes the exact 11-character video
  identity, and rejects junk appended to that identity.

### Focused Quest test after this stage

1. Check and download a normal public YouTube video.
2. Test an invalid URL, removed video, private/auth-required video, 403 response,
   temporary network loss, and cancellation.
3. Perform three probe failures and verify they do not trigger the “three failed
   downloads” guidance.
4. Perform three real download failures and verify the update/nightly guidance
   still appears at the intended threshold.
5. Exercise direct MP4, HLS/remux fallback, and forced transcode fixtures.
6. Check stable and nightly yt-dlp updates, channel switching, progress, close
   game flow, and restart-required message.
7. Pull logs and confirm each operation begins with the correct stage rather
   than inheriting the previous operation's diagnostic state.

### Most likely regression signs

- Valid downloads time out too aggressively on slow internet.
- Probe failures incorrectly offer a yt-dlp update.
- Remux/transcode progress sticks or video controls appear before preparation.
- Switching yt-dlp channels corrupts the runtime or toggle state.
- Error dialogs omit the actual failure or expose sensitive URL data.

### Result

Implemented on `codex/fable-review-stage-5`. The implementation removes the unreachable transfer-time
runtime rollback while retaining transactional updater activation and startup
smoke-test rollback. Probe requests now use the same finite timeout/retry policy
as transfers, probe failures no longer increment the consecutive real-download
failure streak, every queued operation resets its diagnostic throttle, and
normalization reports software validation only when that validation ran.

The downloader now gives YouTube's guest/network verification challenge its own
`BS-DL-YOUTUBE-VERIFY` support code and user-facing explanation instead of
misreporting it as an account-required video. Automated coverage verifies the
classification on both probe and transfer scripts and prevents browser-login
advice from returning.

The editor's **Paste** action now copies arbitrary clipboard text without
silently starting a network request. **Check** accepts `youtu.be/...` and
`youtube.com/...` without `https://`, consumes the visible input rather than a
possibly stale committed value, and converts a valid link to a canonical HTTPS
URL. Host tests cover scheme-less links, tracking parameters, raw IDs,
arbitrary text, and malformed IDs with appended characters.

Host validation on August 31, 2026 passed all 14 canonical tests, including the
embedded downloader fixtures, core error-presentation checks, updater activation
tests, and repository invariants. The ARM64 Quest build, dual-FFmpeg isolation
check, and complete QMOD validation also passed. The final 19,715,746-byte
QMOD had SHA-256
`6d73ab06f76c59f802069c883bbe9743b3dd08c3e7b9355a3b4341c2b9d93d91`.
The ownership-safe source deployment selected the only authorized Quest 2,
verified every deployed payload, and installed `libbigscreen.so` with SHA-256
`3628b6e1249147d0be6a9a494724e78f566b64ef1012b5a25f3eec3e69d8e557`.
On September 1, 2026, the user completed the focused Quest pass and reported no
issues. A real YouTube network/IP verification challenge remained unavailable
to reproduce safely, so its exact yt-dlp wording and presentation remain covered
by deterministic host tests rather than by deliberately provoking a block.

## Stage 6 — Packaging, CI, and dependency reproducibility

### Implementation

- Make the manifest library list authoritative and cross-check staged files.
- Enforce Git tag, QPM, manifest, and release version agreement.
- Keep public releases QMOD-only; upload unstripped symbols as a short-lived
  Actions-only artifact rather than as a release asset.
- Wire currently orphaned test suites into CI.
- Verify hashes for QPM-restored native inputs.
- Strengthen protected-path tests and explicitly report optional test skips.

### Focused package/release test after this stage

1. Build through Windows and Linux one-click QMOD launchers and compare hashes.
2. Inspect the QMOD manifest and entries for missing, duplicate, or unexpected
   libraries and runtimes.
3. Install the QMOD with MBF and verify dependencies, yt-dlp runtime, downloads,
   preview, and gameplay.
4. Run source deployment separately and confirm it remains distinct from MBF.
5. Push a non-release branch and confirm CI artifacts are named clearly.
6. Test a release tag in a safe release workflow: a version mismatch must fail;
   a valid tag must publish only the QMOD as a public binary asset.
7. Confirm the short-lived debug-symbol artifact is downloadable from the
   Actions run for crash analysis but absent from the public release.

### Most likely regression signs

- QMOD grows unexpectedly, misses a runtime, or contains duplicate libraries.
- MBF reports a manifest/schema/dependency error.
- Source and QMOD installs overlap or claim the wrong ownership.
- Release ordering/version checks fail despite a correctly bumped version.
- Public releases regain `.so` or FFmpeg source/archive assets.

### Result

Implemented on `codex/fable-review-stage-6`. `mod.template.json` is now the
authoritative native-library declaration: package creation refuses a staged
root-library set that is missing, duplicates, or adds a declared native input,
and generated `mod.json` validation requires exact agreement with the template.
Release validation also requires the template, generated manifest, top-level
QPM metadata, shared QPM metadata, Git tag, and packaged manifest to identify
the same version.

The ten QPM-restored release/debug native inputs used by the build are pinned in
`qpm-native-inputs.sha256.json`. Linux bootstrap and Windows/WSL preflight now
verify those hashes after both fresh restores and cache hits. The stale Paper2
cache-readiness check was replaced with the actual `custom-types` input. A
changed or unexpected QPM native input therefore stops the build before it can
be linked silently.

The release workflow now publishes only the QMOD as a release binary asset.
The unstripped `libbigscreen.so` is retained for 14 days as a clearly named
Actions artifact for crash analysis and is not attached to the GitHub release.
The release workflow validates the tag against repository metadata before the
build and validates the finished QMOD against the tag before publishing.
Previously orphaned source-ownership, Quest-dependency, and deterministic-ZIP
PowerShell suites now run in core CI on every push.

Recursive removal is guarded by explicit, shared allowlists. Tests assert the
only removable Big Screen trees are `Runtime`, `Videos`, and `SourceInstall`,
while `Thumbnails`, `Video Import`, `library.json`, and `Logs` remain protected
unless an explicit user choice uses the appropriate non-recursive path.
Optional CMake-backed host tests now identify themselves as intentionally
skipped when their prerequisites are unavailable rather than disappearing from
the result.

Host and package verification on September 1, 2026 passed all 14 canonical
tests with no optional suites skipped, plus the source-ownership,
Quest-dependency, and deterministic-ZIP PowerShell fixtures. The ten QPM input
hashes matched, both workflow files parsed as YAML, and the release gate rejected
`v0.7.0-alpha.99` while accepting `v0.7.0-alpha.13`. Windows and native-Linux
one-click launchers independently produced the same 19,715,746-byte QMOD with
SHA-256
`6d73ab06f76c59f802069c883bbe9743b3dd08c3e7b9355a3b4341c2b9d93d91`.
Archive inspection found 96 unique entries, all 16 declared native binaries,
all 79 declared runtime copy sources, no missing or unexpected entries, and no
debug-symbol payload.

No Quest payload behavior was intentionally changed in this stage. The focused
MBF install/download/preview/gameplay pass, separate source-deploy ownership
pass, and live GitHub Actions/tag-release checks remain pending because they
require an explicit headset install or remote push/tag authorization. A public
repository's Actions artifact is Actions-only and short-lived, not confidential;
the important release invariant is that symbols are absent from public release
assets.

## Stage 7 — GPU presentation-path overhead

### Implementation

- Cache shader property IDs.
- Bind YUV textures only when created or replaced.
- Cache conversion matrix, range, rotation, packed dimensions, and visual-effect
  state; update material properties only when those values change.
- Add counters or profiling that demonstrate reduced work without altering
  displayed diagnostics semantics.

### Focused Quest test after this stage

1. A/B GPU Video Conversion off/on with the same 1080p60 and 1440p60 videos.
2. Compare uploaded/deadline counts, frame loss, decoder CPU, presentation time,
   queue depth, gameplay FPS, and visible playback smoothness.
3. Test three-plane and packed upload modes plus their fallback path.
4. Test color range/matrix differences, rotation metadata, color correction,
   vignette, opacity, transparency, and mapper-authored screens.
5. Play the Showcase and several ordinary maps through completion, restart,
   pause, Practice seek, and early exit.
6. Ask Quest 3/3S testers to repeat the same A/B rather than relying only on
   Quest 2 results.

### Most likely regression signs

- Incorrect brightness, color range, orientation, or aspect ratio.
- First frame is correct but later setting/effect changes do not update.
- Black, white, transparent, or stale textures after fallback.
- Improved CPU numbers accompanied by worse frame deadlines or gameplay FPS.
- Performance panel values no longer agree with session logs.

### Result

Implemented on `codex/fable-review-stage-7`. Both GPU YUV shaders now share
nineteen `Shader::PropertyToID` values initialized once per game process, so
ordinary uploads no longer convert property names into managed strings on the
per-frame Unity path. Three-plane uploads bind their U/V textures only when the
material or either texture is created or replaced. An explicit replacement bit
also covers the rare case where Unity reuses the same native address for a new
texture.

Conversion matrix/range/rotation, packed-atlas dimensions, and the complete
visual-effect snapshot are committed to cache only after every Unity property
call in that group succeeds. A partial IL2CPP exception therefore leaves the
group dirty and retryable. Replacing the material—including the live
packed-to-three-plane fallback—invalidates property and binding state without
losing the session counters.

For a stable three-plane video, the first frame applies fifteen material
properties and two chroma bindings; later frames avoid all seventeen. For a
stable packed video, the first frame applies seventeen material properties;
later frames avoid all seventeen. Changes to color metadata, rotation, packed
dimensions, or mapper effects still invalidate and update only their affected
group. Teardown writes one normal log summary containing uploaded frames plus
applied/avoided material writes and chroma bindings. These counters are not
added to the live panel or end-of-map summary, preserving their established
meaning and per-frame cost.

Automated verification passed on September 1, 2026: all 14 canonical host
tests and the new hot-path repository safeguards passed, the ARM64 Quest target
compiled and linked successfully, dual-FFmpeg ELF isolation passed, and the
complete QMOD validated. The 19,721,738-byte package has SHA-256
`6bc6e43bcdc01cb7b16f8080c4f9dd79fdfb9b0aa0e55e5d405e6ad18ca08ef8`.
Visual equivalence and performance impact remain awaiting the focused Quest 2
pass described above.

## Stage 8 — Catalog lifetime and transport-state consolidation

### Implementation

- Make stable level ID and catalog generation authoritative across SongCore
  refreshes; re-resolve managed objects at use boundaries.
- Replace the overlapping preview transport booleans with one explicit state
  machine and documented transitions.
- Consolidate descriptor-to-editor application without changing status-label
  ownership.
- Centralize terminal download-progress ownership rather than clearing the same
  identifier at dozens of call sites.
- Consolidate shared download policy between the Video Library and song-selection
  shortcut while keeping their UI presentation separate.

### Focused Quest test after this stage

1. Preview, pause, seek, loop to the end, replay, and leave several videos.
2. Play a map, return, and confirm its audio/video preview restarts correctly.
3. Test Practice seek/speed, restart, fail, finish, and early exit.
4. Start a download, navigate across maps and menus, then return when it finishes.
5. Use Configure Video repeatedly before and after opening Big Screen normally.
6. Refresh/install SongCore maps while Big Screen is alive, then operate on rows
   that were visible before and after the refresh.
7. Recheck the per-map status label: no stale download text may cross maps, and
   timing controls must publish only the current map's notices.
8. Recheck thumbnails, song titles, selection highlight, back navigation, jump
   letters, JSON errors, and large-library responsiveness.

### Most likely regression signs

- Crash or wrong map after SongCore refresh.
- Preview starts unexpectedly, fails to restart, or keeps playing after leaving.
- Status text from one map appears on another map.
- Configure Video opens the wrong map or becomes locked out.
- Background download completion changes the currently edited map.
- Song titles/thumbnails regress after returning from the editor.

### Result

Pending implementation and Quest testing.

## Stage 9 — Documentation and contained low-risk cleanup

### Implementation

- Remove dead diagnostics code while preserving disabled experimental work.
- Add missing level-ID guards and canonicalize hover hints.
- Replace `Invoke-Expression` logging construction with real argument passing.
- Accept the documented Linux `--yes` flag in the internal build script.
- Repair the optional native-logger crash-test build path.
- Correct privacy, native-logger/Paper2, local-container, Quest support, mapper
  grammar, test-skip, and tool usage documentation.
- Perform a repository-wide final audit for stale review statements and verify
  that no behavior changes slipped into documentation cleanup.

### Focused test after this stage

1. Open every Big Screen tab and inspect labels, hover hints, enabled/disabled
   states, reset buttons, dialogs, author/version text, and storage pages.
2. Run the support-log collector with default options, a custom output path,
   multiple devices, cold ADB, and paths containing spaces.
3. Run normal builds and a build with the logger crash-test option enabled.
4. Verify the crash-test control is absent from release builds and functional in
   the explicit diagnostic build.
5. Browse local MP4 and WebM files and confirm unsupported formats are described
   accurately.
6. Review README and maintained docs as a new Quest 2/3/3S user, source builder,
   mapper, and support-log submitter.
7. Run the complete host/invariant suite and one final focused Quest smoke pass:
   download, preview, Solo, Campaign, Showcase, restart, exit, and reopen menus.

### Most likely regression signs

- Hover hints describe the wrong neighboring control.
- Logging/tombstone tools fail on spaces, cold ADB, or multiple devices.
- Diagnostic build fails despite the release build passing.
- Documentation claims unsupported formats, devices, dependencies, or release
  assets.
- “Cleanup” accidentally removes disabled experimental code or changes runtime
  behavior.

### Result

Pending implementation and final validation.

## Completion rule

The review is resolved only when all accepted stages are marked Quest validated,
all discovered regressions have their result recorded here, the complete host
suite and QMOD build pass, and the final diff audit confirms that rejected
findings—especially explicit clone-light unregistering—were not applied without
new evidence.
