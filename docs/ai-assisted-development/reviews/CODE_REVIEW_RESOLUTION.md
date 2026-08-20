# External code-review resolution

This document records how the August 14 and August 15, 2026 external reviews
were evaluated. Each review was treated as a set of hypotheses, not as an
automatic change list, and every accepted item was checked against the current
source before implementation.

## August 19 independent review

The Claude Opus 4.8 review of `54bb2a1` was preserved under
`docs/ai-assisted-development/reviews/` and evaluated against the live tree.
The following confirmed findings were addressed without changing intended
playback, menu, download, or mapper behavior:

- The retained Cinema bloom experiment now has one descriptive CMake/source
  feature gate that defaults off. Parser support and the implementation remain
  available for future work, while release builds cannot accidentally compile
  only part of the abandoned experiment.
- Error reporting is nonthrowing all the way through correlation-ID creation,
  and an externally dismissed Beat Saber prompt can no longer latch the
  user-visible error channel. Resetting the circuit breaker also resets that
  presentation state.
- Song-selection scene objects now use Unity liveness-aware references. The
  selected `BeatmapLevel` remains under the detail view's existing teardown
  ownership because it is managed song data, not a `UnityEngine::Object`.
- Downloader diagnostic-throttle state is reset under its documented mutex;
  the legacy tombstone helper skips missing slots; and a parseable video that
  reaches EOF without decoding a single picture becomes an explicit playback
  failure instead of gating preview audio forever.
- Decoder startup was deliberately not moved to another thread. Its measured
  open duration is now appended to completed-session performance logs, making
  future decisions evidence-based without adding a new startup lifecycle.
- yt-dlp candidate promotion/rollback and rotating known-good library backups
  were isolated into Unity-free filesystem transactions. Host tests execute
  their real promotion, rejection, destructor rollback, rotation, corrupt-
  primary recovery, and missing-primary recovery behavior. The embedded
  downloader test now also cancels through the production progress hook and
  verifies its resumable partial-file contract.
- FFmpeg 9.0.1 is documented as the default runtime and 4.4.8 as the comparison
  runtime. Internal AI prompts, reviews, and planning records remain available
  by design, but are grouped beneath a clearly noncanonical AI-assisted-
  development archive instead of appearing as primary developer guidance.
- The Cinema compatibility maps are retained under `development-assets/` as
  manual PC/Quest fixtures. Repository tests prevent those maps from entering
  the QMOD or source-deployment scripts.

Several claims were stale or incorrect in the reviewed snapshot and were not
implemented: source deployment already writes and consumes ownership receipts;
library and file-copy deployment already performs read-back hashing; the ADB
completion helper is used by the public launchers; and the displayed yt-dlp
baseline was already current. The proposed migration of
`menuScreenPreviewEnabled` was also rejected after history showed that it was a
different placement-screen preference, not the old name of `showMenuPreview`.
The serial decoder retirement queue remains unchanged: a detached thread per
stuck backend would replace one bounded queue with unbounded threads and is not
a safer correction without evidence of a real stalled-close problem.

## August 15 second review

The second independent review was evaluated against the `d7efbaf` checkpoint.
Its concurrency audit found no active race or lock-order cycle, but its decoder
facade, feature-seam, error-handling, and stale-comment findings reproduced in
the current tree and were addressed as follows:

- `FrameDecoderFacade` now closes and joins its active backend before retaining
  the final decoder CPU, peak latency, and RGBA allocation counters. Benchmark
  results therefore keep the complete final session rather than reporting zero
  after the backend object is released. Diagnostics now call this value the
  decode method (hardware/software) so it cannot be confused with the selected
  FFmpeg 4.4/9 runtime.
- The automatic-performance history holds every possible reduction from
  1440p/60 through 480p/15, allowing exact reverse recovery. Host tests cover
  all five history slots and the supported-codec, HDR/10-bit, and hardware-only
  source policy.
- Crop metadata and colorspace/range configuration are applied uniformly to
  software and MediaCodec output, including a delayed final frame. Seek,
  conversion, and dynamic high-resolution failures retain their real cause.
- Beat Saber's scene transition, start, and results paths remain authoritative.
  Big Screen preparation, environment selection, player-setting copies,
  persistence, and results UI are individually contained by `ErrorManager`, so
  a mod-side failure cannot skip the original game call.
- URL metadata probing preserves a successful resolution list when thumbnail
  retrieval fails. Probe failures carry stable diagnostics and are described as
  checks rather than downloads in the song panel. Outside dismissal cancels the
  owned probe and clears modal state.
- Video Library teardown cancels only tasks it started. C++ terminal failures
  ignore any undeletable stale worker status until the next explicit operation,
  and cancel-marker or worker-start failures are logged instead of silently
  claiming success.
- Reset disables the live performance panel before restoring defaults, so its
  teardown cannot write the old transform over the reset placement.
- Storage scanning recognizes unassigned MP4 and WebM downloads plus aged
  replacement backups. Worker joins occur outside the state mutex, and local
  file scans use cooperative cancellation during UI teardown.
- Manifest recovery validates its republish operations, benchmark file failures
  enter the visible/persistent error path, and map/local-file path checks share
  one nonthrowing component-wise implementation.
- Dead local-video UI state and unused fields were removed. Soft restart now
  destroys the retained detail thumbnail and clears row-thumbnail failure
  caches. Shared downloader codec/tier helpers, byte/path formatting, explicit-
  content policy, toggle synchronization, and layout-component lookup reduce
  policy drift. Each menu still owns its intentionally different sizing and
  arrangement contract.
- Comments and architecture documentation were corrected for selection-row
  placement, tab count, deferred-error consumption, one-time showcase capture,
  downloader initialization, screen grip geometry, showcase buffer reuse, the
  dual-runtime `-Bsymbolic` requirement, embedded CPython, atomic status files,
  and storage fingerprinting.

The large-file responsibility split remains deliberately deferred and tracked
in [FUTURE_WORK.md](../../FUTURE_WORK.md); combining that mechanical refactor with this behavioral
hardening would make on-headset regression isolation substantially harder.

## August 14 first review

Changes below were accepted where they improved correctness, recovery,
diagnostics, or release reproducibility without replacing proven Quest-specific
behavior.

## Implemented

### Decoder and playback

- Decoder shutdown now changes the condition-variable predicate while holding
  the same mutex used by the waiter. This prevents a lost notification from
  hanging the game thread during teardown.
- FFmpeg end-of-stream and `EAGAIN` are handled as normal states. Read, send,
  receive, seek, flush, and conversion failures now retain a useful FFmpeg
  error and enter the existing one-shot playback-failure path.
- A playback failure closes the decoder immediately instead of retaining its
  worker and frame buffers until the map ends.
- Automatic Performance no longer changes resolution or rebuilds the decoder,
  primary surface, or optional showcase surfaces. It changes only the
  presentation limit using a configurable FPS step, eliminating the destroyed-texture and
  decoder-reopen failure class described by the earlier review.
- Screen opacity application now reports failure to its caller instead of
  silently retrying a failed presentation change every frame.

### Downloader and updater

- A C++-side terminal failure removes stale progress JSON before publishing
  the failure in memory, so a previous `preparing` or `downloading` state cannot
  resurrect and wedge future downloads.
- Download/probe/update start operations serialize their complete
  check/join/start transition. This makes the manager safe if a future caller
  starts work outside the current single Unity-thread call path.
- Probe and updater workers now honor the same cancellation file as downloads
  between network phases and streamed chunks. Network timeouts remain finite,
  so shutdown is bounded even when a blocking call itself cannot be interrupted.
- Python exceptions are converted into persistent Big Screen diagnostics;
  worker paths no longer use `PyErr_Print`, which could lose the traceback or
  terminate the process for `SystemExit`.
- Python's `builtins` module is imported through the supported import API; the
  soft-deprecated `PyEval_GetBuiltins` path is no longer used.
- Thumbnail fetching uses the validated YouTube video ID and fixed
  `i.ytimg.com` host. A transient fetch failure keeps an existing good
  thumbnail, removes only the temporary file, and records a warning.
- The updater removes a stale cancellation marker before starting a new job.

### Persistence, storage, and map inspection

- A missing library manifest now recovers from its rotating backups when they
  exist. A genuinely new installation with no manifest or backups still starts
  quietly with an empty library.
- Managed-video deletion validates that the stored name is a leaf filename,
  matching the existing read-path validation and preventing a modified shared-
  storage manifest from deleting outside Big Screen's video directory.
- Backup-copy errors, directory iteration errors, and file-size races are
  logged or handled without bogus multi-exabyte storage totals.
- Invalid settings JSON is timestamp-quarantined before defaults are written,
  and the user-visible error path points to the persistent diagnostic log.
- Chroma detection now uses an iterative JSON walk, nonthrowing filesystem
  queries, useful invalid-file logs, and a per-map metadata cache. This removes
  repeated multi-megabyte parsing during the same menu/game session without
  moving Unity-owned work onto a background thread.
- Storage scan/cleanup entry points provide a defensive fallback message even
  if a future internal failure forgets to supply one.

### Menu and IL2CPP lifetime safety

- Settings, Video Library, Storage Maintenance, and Local Video Browser menus
  have explicit `ForgetUi` teardown paths. A newly created flow discards every
  cached object from the previous MenuCore hierarchy, including modal pointers
  and cached thumbnail sprites.
- The globally reachable menu flow is a `UnityW` reference instead of a raw
  IL2CPP pointer. Lifecycle and Back handlers contain exceptions at the menu
  boundary and invoke the existing fail-safe exit path.
- Per-frame menu UI work runs only while Big Screen's flow is active. The song-
  selection download surface keeps its independent update path.
- Selection teardown also drops its retained level reference and download
  descriptor state.
- Hiding the song-selection panel cancels only a download started by that panel
  for that same level; it no longer aborts work started in the Video Library.
- The Misc-tab slider refresh uses the correct tab index, pending error dialogs
  are not consumed before a modal exists, and nullable song metadata/table
  objects are guarded.
- Recoverable IL2CPP audio races are handled by exception type and operation
  scope instead of matching unstable English text in `what()`.

### Build and runtime integrity

- CMake build-type handling is quoted and case-insensitive, so canonical
  `Release`, an empty value, and the documented configurations are deterministic.
- Two unused CMake fragments were removed; one was syntactically invalid and
  the other was an unhashed network fetch.
- Interrupted CPython extraction is detected from the complete required-file
  set and repaired. Only `libpython3.14.so` is exposed to CMake's recursive
  linker input; SSL, crypto, and SQLite remain packaged runtime dependencies
  rather than accidental direct `DT_NEEDED` dependencies.
- Windows-side FFmpeg staging validates the readiness record and every staged
  library hash. WSL paths are resolved with `wslpath`, including nonstandard
  mount layouts.
- Repository invariant tests refuse Python optimized mode, where bare asserts
  would otherwise disappear.
- Thread-safe `localtime_s`/`localtime_r` replaces shared-state `std::localtime`.
- Both isolated FFmpeg builds now contain software H.264 and the optional LGPL
  MediaCodec decoder, with configure-output gates that reject a silently
  disabled JNI, MediaCodec, or decoder component. Hardware decoding remains an
  off-by-default experiment. Each private libavcodec receives the Quest Java VM
  captured by Scotland2 at preload rather than probing the linker-hidden
  `JNI_GetCreatedJavaVMs` symbol. Each runtime registers that pointer
  independently, reports the backend that actually opened, and automatically
  reopens a mid-stream MediaCodec failure with software at the latest requested
  timestamp. The established CPU-readable YUV, reusable RGBA, and Unity texture
  path remains in place so hardware testing does not remove screen features.

## Reviewed and intentionally not changed

- RapidJSON `GetFloat()` accepts every value for which `IsNumber()` is true,
  including integer JSON. Changing that call would not fix a real defect.
- This Beat Saber target uses Unity 2021 APIs. Replacing
  `FindObjectsOfType` with `FindObjectsByType` would break compatibility with
  the target headers/runtime; scene scans can be optimized independently when
  profiling shows value.
- The storage UI fingerprint is a redraw optimization, not an ownership or
  deletion decision. A standard-library hash collision is not a credible
  release failure for this short-lived, user-confirmed view.
- The Performance Panel border-array report described an older implementation;
  the current code uses its border collections.
- The runtime rollback inside an active download and the startup candidate-
  activation transaction cover different phases. Combining them now would add
  risk without eliminating duplicated behavior visible to users.
- Generated/ignored QPM files and user-owned diagnostic captures were not
  deleted or committed. They are not part of the repository payload.

## Deferred with explicit release tracking

- Splitting the largest UI/manager translation units is still tracked in
  [FUTURE_WORK.md](../../FUTURE_WORK.md). It should be done after the major behavior is stable because
  a mechanical split currently carries more regression risk than runtime value.
- FFmpeg 4.4.8 and 9.0.1 now ship together under isolated SONAMEs, symbol
  namespaces, and decoder backend libraries. This review initially retained
  4.4.8 as the conservative default. Subsequent on-device testing promoted
  FFmpeg 9.0.1 and MediaCodec to the defaults while preserving both menu
  switches for compatibility checks and true A/B comparisons.
- Upstream artifact signatures can complement the existing pinned SHA-256 and
  reproducible-source checks later. They do not replace compatibility testing,
  and they are not required for this hardening batch.
