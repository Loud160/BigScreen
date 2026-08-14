# External code-review resolution

This document records how the August 14, 2026 external review was evaluated.
The review was treated as a set of hypotheses, not as an automatic change
list. Each reported issue was checked against the current source after the UI
work that preceded the review. Changes below were accepted where they improved
correctness, recovery, diagnostics, or release reproducibility without
replacing proven Quest-specific behavior.

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
- Automatic resolution changes rebuild both the primary surface and optional
  showcase surfaces so no material retains a destroyed texture. A failed tier
  change is latched once and stops that playback session safely.
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
  `FUTURE_WORK.md`. It should be done after the major behavior is stable because
  a mechanical split currently carries more regression risk than runtime value.
- FFmpeg 4.4.8 and 9.0.1 now ship together under isolated SONAMEs, symbol
  namespaces, and decoder backend libraries. The Misc selector permits true
  in-game A/B testing, while 4.4.8 remains the default until repeated Quest 2
  and Quest 3 testing establishes equal or better latency, stability, power,
  and compatibility for 9.0.1.
- Upstream artifact signatures can complement the existing pinned SHA-256 and
  reproducible-source checks later. They do not replace compatibility testing,
  and they are not required for this hardening batch.
