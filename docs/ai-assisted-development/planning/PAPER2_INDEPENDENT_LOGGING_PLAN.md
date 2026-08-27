# Paper2-Independent Logging Plan

Status: native logger cutover completed on `codex/first-party-logger`; Big
Screen's direct Paper2 dependency has been removed

Last reviewed: August 26, 2026

## Purpose

This document records why Big Screen should consider replacing its Paper2
logging dependency with a small first-party logger, what that logger must do,
and how the migration should be implemented and verified without weakening
crash diagnostics or affecting video performance.

This remains the migration and verification record. Big Screen now emits only
through its first-party backend and no longer declares, links, initializes, or
globally intercepts Paper2. Paper2 can still appear in the QPM restored lock
and on the headset because beatsaber-hook, BSML, SongCore, and Custom Types use
it independently.

## Current implementation checkpoint

All Big Screen call sites use one `BigScreenLogger` facade backed only by the
first-party native logger. The earlier Paper-only/native-only/dual comparison
modes were removed after the dual and native-only tests described below. This
prevents a later build option or stale environment variable from accidentally
restoring Big Screen's Paper dependency.

The native backend currently provides:

- Android logcat output in native-only builds (Paper supplies it in dual mode);
- one owned asynchronous writer thread and no detached workers;
- a 1 MiB/2,048-record ordinary queue with a reserved warning/error margin;
- a 5 MiB active log and one 5 MiB previous rotation;
- immediate completed-batch flushing into the OS, no periodic idle wakeup,
  and a bounded critical-error completion request;
- retry after directory/open/write failures while logcat remains available;
- dropped-record accounting and a later warning summary;
- source filename, line, severity, timestamp, and producer-thread identity;
- host tests for lifecycle, rotation, bounded overflow, and concurrent writers.

The active files are:

```text
/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/bigscreen-native.log
/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/bigscreen-native.previous.log
```

The Windows and Linux support collectors include these files. Host tests and
the ARM64 Quest build/package pass. A dual-backend build was also exercised on
a Quest 2 on August 26, 2026: the support collector retrieved the native and
Paper2 files while Beat Saber was still running, and both contained the same
complete Big Screen event set through the same final message. The native file
also contained its own session header and preserved one embedded newline as a
multiline record, while Paper2 prefixed the resulting lines separately. No
native dropped-record or file-failure marker was present. A host test now also
verifies that an ordinary record is readable before explicit flush or shutdown,
proving completed-batch crash-tail persistence without relying on a periodic
timer. A controlled dual-backend crash test on August 26, 2026 then confirmed
that both sinks retained the identical multiline baseline, INFO tail, WARNING
tail, and final CRITICAL record immediately before the deliberate `SIGABRT`.
Android logcat and the Quest tombstone independently confirmed that the abort
originated in Big Screen's test harness. A subsequent native-only build was
tested through normal menu and video-preview use, then the same deliberate
crash harness. Its unique crash-tail token appeared in the first-party log and
not in Paper logs, with no native dropped-record or write-failure marker. The
installed ELF matched the tested build, contained no Paper2 `DT_NEEDED` entry,
and exposed no Paper compatibility symbol.

A development-only crash harness is compiled only when
`BIGSCREEN_ENABLE_LOGGER_CRASH_TEST=ON`. It adds a red **TEST LOGGER CRASH**
control to the Update tab and requires a second confirmation before doing
anything destructive. The harness writes a flushed baseline followed by INFO,
WARNING, and CRITICAL tail records carrying one unique token, then calls
`SIGABRT` through `std::abort()` without orderly logger shutdown. Normal builds
default the option to `OFF`, so this button and its deliberate crash path cannot
enter an ordinary QMOD accidentally.

The final build removes QPM's unconditional Paper2 link from `libbigscreen.so`.
Current
beatsaber-hook inline abort helpers still emit through two Paper2-named C-ABI
functions, so the native build uses link-time `--wrap` only for references made
inside `libbigscreen.so` and routes those rare fatal-path records into the
first-party logger. The bridge symbols are hidden and are verified absent from
the dynamic symbol table; they cannot replace or intercept Paper2 for BSML,
SongCore, or another mod. The build pipeline rejects a native package if
`libbigscreen.so` retains Paper2 in `DT_NEEDED` or exposes a bridge symbol.

## Why this change was made

Big Screen 0.7.0-alpha.12 is linked against `paper2_scotland2` 4.8.0 and its
generated QMOD requires version `^4.8.0`. Paper2 4.8.0 introduced the
`paper2_queue_log_bytes_ffi` function used by the Paper C++ headers. A headset
with Paper2 4.7.0 cannot resolve that symbol, so Scotland2 rejects
`libbigscreen.so` before Big Screen's own startup or dependency-checking code
can run.

The Beat Saber 1.40.8 AudioLink QMOD investigated after a user report requires
exactly `paper2_scotland2` 4.7.0. That dependency cannot be satisfied at the
same time as Big Screen's `^4.8.0` requirement. Installing AudioLink can leave
the headset with Paper2 4.7.0, producing an error equivalent to:

```text
dlopen failed: cannot locate symbol "paper2_queue_log_bytes_ffi"
referenced by libbigscreen.so
```

This is a dependency ABI conflict, not evidence that AudioLink's audio hooks or
shader texture conflict with Big Screen's playback implementation.

Paper's public repository and the v4.8.0 source tag also do not currently
contain an explicit `LICENSE` file or declared SPDX license. Public source is
not automatically permission to copy, modify, statically link, or redistribute
that code. Big Screen should therefore not vendor Paper's source or binary
inside its own QMOD without an explicit license or permission from Paper's
copyright holders.

References:

- [Paper repository](https://github.com/Fernthedev/paperlog)
- [Paper2 4.8.0 release](https://github.com/Fernthedev/paperlog/releases/tag/v4.8.0)
- [Beat Saber 1.40.8 AudioLink fork](https://github.com/squishy537/BSAudioLink-Quest)
- [AudioLink dependency manifest source](https://github.com/squishy537/BSAudioLink-Quest/blob/master/qpm.json)

## Why bundling the existing Paper library is not the solution

Adding `libpaper2_scotland2.so` to Big Screen's `libraryFiles` would package a
copy, but it would not create a private dependency. Both QMODs would still own
and install the same library filename. Install order could replace 4.8.0 with
4.7.0 or replace the version expected by AudioLink, and uninstalling either
package could leave shared-file ownership in an unsafe state.

Renaming or statically linking a private Paper build would require permission
to redistribute it. It would also require controlling the ELF SONAME, hiding or
renaming exported `paper2_*` symbols, reproducing Scotland2 initialization, and
preventing two independent Paper runtimes from competing for the same log
files. That is much more complicated and risky than the functionality Big
Screen actually needs.

## Current Big Screen usage

The Paper dependency is broad in call count but narrow in API surface. The
pre-migration source contained approximately 348 ordinary calls through the shared
`PaperLogger` object across 26 files:

| Method | Approximate calls |
|---|---:|
| `info` | 145 |
| `error` | 118 |
| `warn` | 80 |
| `debug` | 5 |

Outside those calls, Big Screen created one constant logger context and
registered that context for a per-mod file during `setup()`. It did not use
Paper's profiler, backtraces, custom sinks, or other advanced features. The
current facade preserves that narrow surface without retaining Paper as a Big
Screen backend.

This makes a compatibility-shaped replacement practical. The existing format
strings and arguments can remain unchanged while the logger object and backend
are replaced.

## Recommended architecture

Create a first-party `BigScreenLogger` component and compile it directly into
`libbigscreen.so`. It should not be a second independently installed shared
library. This keeps ownership, versioning, and initialization inside Big
Screen and prevents another QMOD from replacing it.

### Public interface

The interface should intentionally remain small:

```cpp
BigScreenLogger.debug("message {}", value);
BigScreenLogger.info("message {}", value);
BigScreenLogger.warn("message {}", value);
BigScreenLogger.error("message {}", value);
```

Use Big Screen's existing header-only `fmt` dependency for `{}` formatting.
The logger should expose only the features Big Screen uses rather than growing
into a general logging framework.

All public logging functions must be safe at hook boundaries:

- no exception may escape a logging call;
- logging must remain usable before full initialization and during teardown;
- formatting, allocation, file, or thread failures must fall back safely;
- a logger failure must never trip Big Screen's circuit breaker or crash Beat
  Saber.

### Output destinations

Every accepted message should be sent to Android logcat through Quest's system
`liblog`, which Big Screen already links through `-llog`.

The first-party file sink should write to a Big Screen-owned path such as:

```text
/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/bigscreen-native.log
```

Each record should include at least:

- timestamp;
- severity;
- `BigScreen` tag;
- thread identifier or a stable short thread label;
- message text.

The file should begin each game session with the Big Screen version, Beat Saber
package version when available, and a session-start marker. This preserves the
diagnostic value users currently obtain from `BigScreen.log`.

### Threading and performance

File I/O must not occur directly on Beat Saber's UI, Unity, decoder, or download
worker paths merely because one of those paths emitted a routine log message.

Use one owned background writer with a bounded queue:

1. The caller formats a complete record.
2. The record is written to logcat.
3. The record is placed into the bounded file queue with a short, non-blocking
   or tightly bounded critical section.
4. The writer drains records in batches and appends them to the file.

The queue must have both entry-count and byte-size limits. It must never grow
without bound during a repeated error. When the limit is reached, use a
documented drop policy and later emit one summary stating how many records were
dropped. Do not recursively log a logger failure through the logger itself.

The writer must be an owned, joinable thread. It must not be detached. Startup,
shutdown, and bounded flushing must have explicit lifecycle methods so the
thread cannot access destroyed state during a game restart or process exit.

Critical errors may request a bounded flush, but logging must not wait
indefinitely for storage. The existing `ErrorManager` and diagnostic-session
logs remain separate fault records and must not acquire the new logger's mutex
while holding their own important locks.

### Rotation and retention

The native implementation uses a 5 MiB maximum for
`bigscreen-native.log` and retains one previous 5 MiB file, matching the
approved 10 MiB total budget. Rotation must be robust enough that a failed rename
or storage error leaves at least one readable log. File creation, rotation, and
append failures should fall back to logcat.

The regular Big Screen log is separate from:

- `error-history.log`;
- `performance-history.log`;
- detailed Menu and Download session logs;
- power benchmark CSV files.

Those existing purpose-specific records should not be collapsed into the new
general logger during this migration.

## Implementation sequence

### 1. Establish behavior with host tests — implemented

Add a logger header, implementation, and focused host tests before altering all
call sites. Tests should cover:

- formatting common value types used by current calls;
- severity and timestamp formatting;
- concurrent producers;
- queue overflow and its dropped-record summary;
- file rotation;
- unavailable or read-only storage;
- calls before initialization and after shutdown;
- repeated initialization and shutdown;
- bounded flush behavior;
- the guarantee that logging methods do not throw.

The file sink should be injectable or otherwise redirectable in host tests so
tests never write into a real Quest path.

### 2. Replace the logger boundary — completed

Replace the Paper include and constant context in `include/main.hpp` with the
first-party logger. Mechanically rename `PaperLogger` call sites to
`BigScreenLogger` while preserving every existing message, severity, format
argument, and control-flow decision.

Remove `Paper::Logger::RegisterFileContextId(...)` from `setup()` and initialize
the Big Screen logger through its own idempotent lifecycle. Do not use this
migration as an opportunity to rewrite unrelated messages or subsystem logic.

### 3. Remove build and packaging dependencies — completed

`paper2_scotland2` was removed from Big Screen's direct dependency lists in:

- `qpm.json` and the `qpm.shared.json` configured dependency list;
- generated `mod.json` dependency rules;
- dependency-manifest generation and validation;
- dependency checks and user-facing minimum-version diagnostics.

The QPM restored dependency lock can still contain Paper2 transitively because
other direct dependencies compile against and require it. Their requirements
were not altered. CMake filters that transitive library only from
`libbigscreen.so`'s link set.

The finished `libbigscreen.so` must have no undefined `paper2_*` symbols, and
the generated QMOD must neither contain Paper nor request it as a dependency.

### 4. Update diagnostics and documentation — completed

Update the support-log collector to treat the new Big Screen-owned log as the
primary general mod log. It may retain Paper log collection as optional
third-party context, but Big Screen crash diagnosis must no longer depend upon
Paper files being present.

Update at least:

- dependency/build documentation;
- troubleshooting and log-location documentation;
- third-party notices and provenance documentation where Paper is currently
  mentioned;
- repository invariant tests that validate the QMOD dependency list;
- source-install and removal documentation if log preservation paths change.

### 5. Verify on Quest — native cutover validated; ecosystem matrix remains

The native-only QMOD was installed through MBF on the current Beat Saber
1.40.8 Quest 2 target. Big Screen menu and preview operations created the
first-party log, and the deliberate crash test retained the same final records
proved during dual-backend comparison. Support-log collection successfully
retrieved the native file and independent Android crash evidence.

Remaining broader release-candidate verification should include:

- Big Screen loads and its main menu opens;
- the new log is created and contains a session header;
- preview playback, scrubbing, looping, and teardown;
- YouTube checking, download, cancellation, remux, and downloader errors;
- normal gameplay, restart, fail, completion, and early exit;
- Showcase playback and return to the game menus;
- repeated menu entry and exit;
- support-log collection;
- game shutdown with no logger-thread crash or hang;
- comparison of gameplay and decoder performance before and after migration.

Then install the Beat Saber 1.40.8 AudioLink QMOD that pins Paper2 4.7.0 and
repeat Big Screen startup, preview, and gameplay checks. Big Screen itself must
remain independent of the Paper version selected for its other dependencies.
A headset with this dependency set cannot be expected to have no Paper library
at all because those other mods still require it.

## Risks and safeguards

### Lost tail records

An asynchronous writer can lose queued records if Android kills the process.
Keep the queue short, drain promptly, periodically flush, and provide a bounded
critical-error flush. Continue using the synchronous, purpose-specific error
history for important user-visible failures.

### Main-thread stalls

Unbounded queue locks, synchronous filesystem calls, or expensive formatting
inside high-frequency loops could affect menu or video timing. Keep queue
ownership simple, bound contention, and audit repeated per-frame logging during
Quest testing.

### Shutdown races

The worker must stop accepting file records, drain within a fixed deadline,
close its stream, and join before owned state is destroyed. Late calls should
continue to logcat or be discarded safely rather than touching freed memory.

### Duplicate or fragmented diagnostics

The general logger, persistent error history, performance history, and detailed
session logger serve different purposes. Document their roles and ensure the
support collector gathers all of them without presenting stale Paper files as
Big Screen's current primary log.

## Alternatives considered

### Ask AudioLink to update its dependency

AudioLink should ideally stop pinning Paper2 exactly to 4.7.0 and be tested
against 4.8.0. That is the smallest ecosystem-wide correction, but Big Screen
cannot control when every third-party QMOD updates its dependency manifest.

### Build Big Screen against the Paper2 4.7 ABI

Paper2 4.8.0 retains the older `paper2_queue_log_ffi` export, so Big Screen
could potentially compile against the 4.7 headers and accept either runtime.
This would be a smaller compatibility workaround, but it would retain an
external unlicensed dependency and leave Big Screen exposed to future Paper ABI
or packaging conflicts.

### Vendor or rename Paper2

Do not pursue this without explicit licensing permission. It also creates
avoidable symbol, initialization, file-ownership, and package-removal risks.

## Recommended decision

The selected design is a small first-party logger directly inside Big Screen,
not another generally distributed shared logging library. It preserves the
existing call shape, keeps routine file I/O on one bounded background worker,
and retains logcat and all purpose-specific diagnostic files. Paper has been
removed from Big Screen's direct runtime, link, and QMOD dependency graph after
dual and native-only Quest validation; transitive dependency metadata remains
where other libraries legitimately require it.

This provides the durable outcome sought by the change: Big Screen can no
longer fail to load because another QMOD installed a different Paper2 version,
and Big Screen's logging behavior, license, versioning, and release packaging
remain under the project's control.
