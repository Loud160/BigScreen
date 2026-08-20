# Self-updating Big Screen with automatic rollback

Status: feasibility study only; not implemented.

Date reviewed: August 17, 2026

## Decision

The proposed updater is **feasible with important limitations**.

Big Screen could safely support a self-updating core with A/B slots and
next-launch rollback, but the current single-library QMOD cannot safely update
itself as-is. The practical design is to keep `libbigscreen.so` as a small,
stable bootstrap installed by MBF/QMOD and move most of Big Screen into a
separate `libbigscreen_core.so` selected by that bootstrap.

Self-update should initially be restricted to the Big Screen core and private,
non-shared assets. Scotland2, the bootstrap, FFmpeg, CPython, BSML, SongCore,
CustomTypes, Paper, beatsaber-hook, and other shared dependencies must remain
under normal QMOD/mod-manager ownership.

This is a very large architectural change rather than a normal update-checking
feature. A focused on-device proof of concept is required before production
implementation.

## Current architecture relevant to this proposal

The current Beat Saber 1.40.8 QMOD contains:

- `libbigscreen.so` as a normal Scotland2 mod (`modFiles`, loaded from the
  `mods` phase rather than `early_mods`);
- 15 native library files, including two decoder backends, two private FFmpeg
  sets, beatsaber-hook, CPython, OpenSSL, and SQLite;
- 78 runtime file copies for CPython, yt-dlp, certificates, native Python
  extensions, and notices;
- normal QMOD dependencies on beatsaber-hook, Paper/Scotland2, SongCore, BSML,
  and CustomTypes.

At the time of this review, the QMOD was approximately 18.38 MiB compressed and
38.29 MiB extracted. The current `libbigscreen.so` was approximately 4.18 MiB.

Big Screen exports Scotland2's `setup()` and `late_load()` lifecycle functions.
`late_load()` initializes IL2CPP and CustomTypes, initializes the video library
and downloader, installs gameplay and menu hooks, registers SongCore callbacks,
and registers the settings menu. The implementation has no practical hot-unload
lifecycle.

Once a candidate core installs hooks, registers Unity custom types, creates
Unity objects, or starts background workers, it cannot safely be unloaded and
replaced by another core in the same process. Rollback after that point must
happen on the next complete Beat Saber launch.

Big Screen already proves that the necessary private-copy loading mechanism is
available. `DownloadManager` currently copies native CPython dependencies from
shared ModData into Beat Saber's private `code_cache` and loads them with
`dlopen()`. Native update files must follow the same trust boundary rather than
being loaded directly from `/sdcard`.

Scotland2 also copies `libs`, `early_mods`, and `mods` from shared ModData into
Beat Saber's private files directory before loading them. It opens libraries
with `RTLD_LOCAL | RTLD_NOW` and discovers the lifecycle exports with `dlsym()`.
Its libraries phase is loaded before normal mods, so a bootstrap-loaded Big
Screen core can use compatible dependencies already provided by the installed
QMOD.

## Recommended architecture

```text
Scotland2
    |
    v
libbigscreen.so
stable bootstrap installed by QMOD
    |
    +-- read redundant update state
    +-- choose known-good or trial slot
    +-- verify signature and file hash
    +-- copy the selected core into private code_cache
    +-- dlopen the private copy
    +-- resolve a small C ABI
    +-- forward setup/load/late_load
    +-- receive health checkpoints
    +-- record promotion, rejection, or rollback
    |
    v
libbigscreen_core.so
normal Big Screen implementation
```

### Stable bootstrap responsibilities

The bootstrap should contain only:

- Scotland2 lifecycle exports;
- minimal persistent logging;
- transactional update-state reading and writing;
- SHA-256 and Ed25519 verification;
- compatibility and ABI validation;
- bounded private-cache copying;
- `dlopen()` and `dlsym()`;
- candidate selection, rejection, and rollback;
- a small C function table passed to the core.

It must not contain networking, Python, yt-dlp, FFmpeg, BSML UI, video playback,
Unity custom types, gameplay hooks, complex settings migration, or general
release-management logic. The bootstrap is the recovery root and must remain as
small and rarely changed as practical.

The bootstrap should not self-update. A core requiring a newer bootstrap must
be rejected and installed through a new QMOD using MBF or another compatible
mod manager.

### Core ABI

The independently updated core must use a deliberately small C ABI. A
conceptual boundary could expose:

```cpp
BigScreenCore_GetApiVersion();
BigScreenCore_GetVersion();
BigScreenCore_SetHostApi(...);
BigScreenCore_Setup(...);
BigScreenCore_Load();
BigScreenCore_LateLoad();
```

Do not pass STL containers, C++ classes, exceptions, allocator-owned strings,
or implementation-specific configuration objects across the boundary. The host
API should provide function pointers for health reporting and minimal logging,
which avoids relying on symbols being globally visible.

A stop function may exist for orderly process shutdown, but it must not be used
as permission to `dlclose()` a core after hooks or Unity types have been
registered.

## Storage and activation

Use two fixed logical slots in Big Screen's ModData directory:

```text
/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Updater/
    slot-a/
        release-manifest
        libbigscreen_core.so
    slot-b/
        release-manifest
        libbigscreen_core.so
```

The shared-storage slots are untrusted input. Before each load, the bootstrap
must:

1. verify the signed release manifest;
2. verify compatibility and all declared sizes;
3. hash the selected core;
4. copy it to Beat Saber's private `code_cache` through a temporary file;
5. make the private copy executable;
6. atomically rename the private temporary file into place;
7. hash the private copy again;
8. load only the private verified copy.

The active slot must never be modified in place. An update is written into the
inactive slot, completed, verified, and only then selected through the update
state.

The update state should live primarily in app-private storage. Use two
alternating state files with generation numbers and checksums. Each update must
write and flush a temporary file, rename it atomically on the same filesystem,
and retain the previous valid generation. A human-readable mirror may be
written to ModData for diagnostics, but it must not be trusted for security
decisions.

## A/B update state machine

The necessary logical states are:

```text
known-good
downloaded
verified
trial-pending
trial-starting
startup-healthy
promoted
rejected
```

Recommended flow:

1. Slot A is known-good and remains active.
2. Download B to a `.partial` staging location.
3. Validate archive limits, manifest signature, compatibility, and every file.
4. Atomically complete slot B.
5. Persist `trial-pending` for B.
6. Continue running A until Beat Saber completely exits.
7. On the next launch, persist `trial-starting` before loading B.
8. Load B and forward the required lifecycle calls.
9. After the startup-health checkpoint, persist `startup-healthy`.
10. Continue retaining A.
11. Promote B after either a complete gameplay start/exit cycle or two
    successful startup-health confirmations.

If the process starts a trial but never records `startup-healthy`, the next
launch rejects B and loads A. This is a safe availability decision, but it is
not proof that B caused the failure. The user-facing message should say that
the candidate did not complete startup and Big Screen restored the previous
version as a precaution.

### Health checkpoints

`dlopen()` success is not a sufficient health check. Startup health should
require all of the following:

- the core loaded;
- the required exports resolved;
- ABI and version negotiation completed;
- settings loaded without an irreversible migration;
- the video library initialized;
- downloader initialization completed or failed safely;
- CustomTypes registration completed;
- all required hooks installed;
- the SongCore callback registered;
- the settings menu provider registered;
- Beat Saber reached a stable main-menu frame.

The user should not have to open the Big Screen menu to complete this health
check. Requiring actual menu interaction would leave a trial pending for too
long.

Keep `startup-healthy` separate from `known-good`. A later crash after startup
health is ambiguous and must not automatically be blamed on Big Screen.

### Same-process fallback boundary

Immediate fallback to A is reasonable only for clean failures before core
startup, such as:

- signature or hash failure;
- incompatible bootstrap/core ABI;
- `dlopen()` returning an error without a fatal constructor failure;
- required export missing;
- a structured pre-start validation failure.

After hook installation, Unity registration, or worker startup begins, fallback
must wait until the next process launch. Do not attempt to catch `SIGSEGV` and
continue running; the process may already contain corrupted or partially
modified state.

## Persistent-data compatibility

A retained native library is not enough to guarantee rollback. A candidate
must not irreversibly migrate shared settings, `library.json`, downloader state,
or other persistent data before promotion.

Otherwise a failed candidate could leave the previous core unable to read its
data. Trial versions therefore require at least one of:

- backward-compatible additive schemas;
- copy-on-write trial data;
- a transactional pre-trial backup with verified restoration;
- migrations deferred until promotion.

Downloaded videos should remain outside core rollback. Existing video-library
backups help with corrupt files but do not by themselves guarantee compatibility
between two core versions.

## Security model

Self-update installs native executable code. HTTPS and a SHA-256 file hosted
beside the release are not sufficient if the hosting account or release
pipeline is compromised.

Use an Ed25519-signed canonical release manifest containing:

- manifest schema version;
- Big Screen core version;
- a monotonically increasing release sequence;
- release channel;
- Beat Saber package version;
- target ABI (`arm64-v8a`);
- supported bootstrap ABI range;
- core ABI version;
- compatible QMOD shell version;
- exact required shared-dependency versions or ranges;
- every file's relative path, byte size, and SHA-256;
- total expanded size;
- stable or beta designation.

The bootstrap contains the trusted public key and verifies the signature and
selected core hash before every load. GitHub Releases is only the transport.

Additional requirements:

- use different stable and beta signing keys;
- never permit the beta key to sign a stable release;
- keep signing private keys outside the public repository;
- prefer offline or hardware-backed release signing;
- retain a signed release-sequence high-water mark against silent downgrades;
- reject absolute paths, `..`, symbolic links, duplicate archive paths,
  excessive entry counts, and decompression bombs;
- place strict limits on download and extracted sizes;
- record a rejected candidate by version and manifest hash;
- retry an identical rejected candidate only after explicit user action;
- allow a corrected same-version build only with a new signed sequence and
  manifest hash.

The bootstrap should reverify the executable immediately before every load,
not merely when it was first downloaded.

## Dependency policy

Initial self-update support should use this ownership model:

| Component | Self-update | Reason |
| --- | --- | --- |
| `libbigscreen_core.so` | Yes | Intended A/B update unit |
| QuickJS compiled into the core | Yes | Travels with the core |
| Private non-executable core assets | Possibly | Can be covered by the signed slot manifest |
| yt-dlp | Keep its existing updater | Already has independent verification and rollback |
| FFmpeg libraries/backends | No initially | Native ABI, SONAME, and load-order risk |
| CPython/OpenSSL/SQLite libraries | No | Treat as one process-level ABI unit |
| beatsaber-hook | No | Shared QMOD dependency |
| BSML, SongCore, CustomTypes, Paper | No | Shared mod-manager-owned dependencies |
| Scotland2/bootstrap | No | Recovery root |
| New required dependency versions | No | Require a complete QMOD update |

Only one Big Screen core may be started in a process. Never preload a candidate
for validation while the known-good core is running, and never keep both active.

## QMOD and mod-manager compatibility

MBF and QuestPatcher install and uninstall files explicitly listed in
`mod.json`. They do not discover arbitrary future files created by a mod.

Consequences of self-updating include:

- MBF reports the QMOD/bootstrap version, which can differ from the active core;
- a normal QMOD reinstall can overwrite the shipped slot and reset an update;
- dynamically created slot or state files can remain after uninstall;
- self-update cannot change dependency declarations known to the mod manager;
- a core must never replace a shared library owned by another mod.

To reduce orphaned files, use fixed slot file names and declare baseline or
placeholder files at those exact external destinations in the QMOD's
`fileCopies`. A normal QMOD reinstall should intentionally restore its shipped
baseline and invalidate incompatible candidate state.

App-private state and code-cache files cannot be assumed to be removable by a
normal QMOD uninstaller. They should remain small, inert when the external
bootstrap is absent, and safe for Android to clear. Fully clean removal would
require explicit mod-manager support or a privileged external cleanup path.

The Big Screen UI should report both versions:

```text
QMOD/bootstrap version: 0.x.y
Active core version: 0.x.z
```

Self-update should be presented as a compatible core hotfix. Releases requiring
a new bootstrap, dependency, QMOD manifest, or native runtime must direct the
user to MBF.

## Companion Android application

A companion APK is optional and is not recommended for the first
implementation.

It could provide a conventional Android update UI, download into shared ModData
while Beat Saber is closed, and launch Beat Saber through an explicit intent.
It would not normally be able to access Beat Saber's app-private files,
force-stop Beat Saber, update QMOD bookkeeping, replace Scotland2, or recover a
broken bootstrap. The Beat Saber bootstrap would still have to verify and copy
the selected core privately.

An Activity runs only when opened. Independent background checking would need
WorkManager, JobScheduler, or a foreground service, adding battery use,
permissions, notifications, installation complexity, and another security
surface. That does not materially improve rollback reliability.

The initial user flow should stage an update while Big Screen is running and
ask the user to close and reopen Beat Saber. A one-click restart must not be
offered unless a clean Quest/Beat Saber shutdown path is proven. Launching an
Android intent does not guarantee that the existing Beat Saber process has
exited safely.

## Failure-state matrix

| Failure point | Current result | Next launch | Data-loss risk | Automatic recovery |
| --- | --- | --- | --- | --- |
| Network/download interrupted | Partial staging remains | Continue A and clean partial | None | Yes |
| Signature/hash invalid | Candidate rejected | Continue A | None | Yes |
| Storage fills during staging | Incomplete slot ignored | Continue A | None | Yes |
| Power loss during state write | One state copy may be invalid | Use older valid generation | Low | Yes |
| `dlopen()` returns an error | Candidate never starts | Load A immediately or next launch | None | Yes |
| Required export or ABI missing | Candidate never starts | Reject B and load A | None | Yes |
| Crash in a core constructor | Beat Saber crashes once | Missing health causes fallback to A | Low | Next launch |
| Crash during hook installation | Beat Saber crashes once | Missing health causes fallback to A | Medium | Next launch |
| Intentional close before health | Looks like an early failure | Conservatively restore A | None | Yes, false rejection |
| Crash after startup health | Ambiguous failure | Keep B and offer manual rollback | Variable | No automatic blame |
| Both state files corrupt | No trusted selection | Load signed QMOD baseline and disable updater | Low | Yes |
| Bootstrap crashes | Recovery root unavailable | MBF/QMOD reinstall required | Variable | No |
| Normal QMOD reinstall | Shell and baseline replaced | Reset incompatible trial state | User videos preserved | By design |
| Normal QMOD uninstall | Declared files removed | Some private/dynamic files may remain inert | Policy dependent | Partial |

## Major risks

### Critical

- A bootstrap defect cannot be self-rolled back.
- A native candidate can crash Beat Saber before reporting health; the first
  failed launch cannot be prevented.
- Candidate data migrations can make the previous core unusable after rollback.
- Same-process fallback is unsafe after hooks or Unity types are partially
  registered.

### High

- Native dependency or SONAME collisions between core versions.
- QMOD metadata diverging from the active core.
- Dynamic files becoming orphaned after normal uninstall.
- Release-signing key or pipeline compromise.
- False rollback when the process is deliberately closed before health.
- Partial private copies or state corruption without transactional writes.

### Medium

- Startup delay from copying, hashing, verifying, and relocating the core.
- Storage for two cores and a temporary private copy.
- More complicated release engineering and on-device fault testing.
- User confusion between the installed QMOD and active core versions.

### Low

- Gameplay overhead after startup. The bootstrap should not remain on any
  per-frame path.

## Implementation options

| Option | Feasibility | Reliability | Recovery | Complexity | Decision |
| --- | --- | --- | --- | --- | --- |
| Simple staged replacement | Technically easy | Poor | None if the replacement fails to load | Moderate | Do not use |
| Stable A/B bootstrap | Feasible with limitations | Best available | Automatic next-launch rollback | Very large | Recommended architecture |
| Companion APK only | Possible | Moderate | Cannot replace the recovery bootstrap | Large | Not sufficient |
| A/B bootstrap plus companion | Possible | High | Same core recovery as A/B | Extremely large | Consider only later |

A simple replacement can overwrite the external `libbigscreen.so` for the next
launch while the current process keeps its mapped old file. It cannot repair
itself if the replacement crashes before Big Screen starts, so it does not meet
the recovery requirement.

## Recommended proof of concept

The first experiment must not include networking or real release downloads.

1. Build a tiny `libbigscreen.so` bootstrap.
2. Build two trivial core libraries with the proposed C ABI.
3. Place A/B cores and local test manifests manually with ADB.
4. Copy and load A through Beat Saber's private code cache.
5. Confirm lifecycle forwarding and logging.
6. Select B as a trial through a transactional state file.
7. Test candidates that:
   - initialize successfully;
   - return a structured initialization failure;
   - omit a required export;
   - fail `dlopen()`;
   - deliberately crash before health;
   - deliberately crash after health.
8. Verify that a pre-health crash causes A to load on the next launch.
9. Verify that a post-health crash does not automatically blame B.
10. Corrupt each state copy independently and verify recovery.
11. Interrupt staging/copying and verify that A remains untouched.
12. Repeat using a minimally split real Big Screen core and verify hooks,
    CustomTypes, BSML, SongCore, FFmpeg, Python, campaign, restart, Replay,
    preview, and repeated menu entry.
13. Test a normal MBF QMOD reinstall and uninstall and document every retained
    file.

Only after these tests pass should networking, signed GitHub manifests, update
dialogs, or release-channel selection be implemented.

## Performance and storage estimate

A core-only A/B system would likely add approximately 8 to 10 MiB for two core
slots plus one temporary private copy. Duplicating the entire current extracted
QMOD would add roughly 40 MiB or more and greatly increase native dependency
risk.

Startup would gain a file copy, signature check, SHA-256 pass, and an additional
`dlopen()`. This should create a modest startup delay but no meaningful gameplay
cost if the bootstrap performs no per-frame work and only one core is mapped.

## Future Rust and PC relevance

A future thin C++ Quest host plus a Rust `cdylib` core could make the stable C
ABI easier to enforce. Rust panics must never cross FFI, allocation ownership
must remain on the originating side, and exported structures must remain
explicitly versioned. A Rust rewrite is not required for the A/B proof of
concept.

The signed manifest, update states, health checkpoints, and rollback model could
later be shared with a PC implementation, where filesystem access and external
updater processes would be less restrictive.

## Final recommendation

Keep the current notification-only Big Screen release checker until the
bootstrap/core proof of concept proves all of the following on Quest:

1. hooks and CustomTypes work safely from a secondary loaded library;
2. the private-copy loading path works repeatedly;
3. pre-health native crashes reliably roll back on the next launch;
4. rollback preserves settings and the video library;
5. Python and FFmpeg are never initialized or loaded twice;
6. normal MBF installation, reinstall, and removal remain understandable;
7. the signed release process can be operated without exposing a private key.

Even after those conditions are met, the updater should remain a core-hotfix
system rather than a replacement for MBF. Bootstrap, dependency, QMOD, and
native-runtime changes must continue to use ordinary signed QMOD releases.

## References inspected

- [Scotland2 mod-directory copying and phase loading](https://github.com/sc2ad/scotland2/blob/003f28dd6e47285cf441a1a195206e94ec44971b/src/modloader.cpp#L129-L182)
- [Scotland2 native loading and lifecycle export discovery](https://github.com/sc2ad/scotland2/blob/003f28dd6e47285cf441a1a195206e94ec44971b/src/loader.cpp#L339-L415)
- [MBF QMOD file installation and removal](https://github.com/Lauriethefish/ModsBeforeFriday/blob/b16beadf8320ca48d374b7e6f3efec298474c4cc/mbf-agent/src/mod_man/loaded_mod.rs#L78-L146)
- [QuestPatcher QMOD ownership and uninstall behavior](https://github.com/Lauriethefish/QuestPatcher/blob/1c7d2ca404563d920488b949f58aee773f1497d9/QuestPatcher.Core/Modding/QPMod.cs#L85-L148)
- [Android dynamic-code-loading security guidance](https://developer.android.com/privacy-and-security/risks/dynamic-code-loading)
- [Android security guidance for external executable files](https://developer.android.com/privacy-and-security/security-tips)
- [Android background-work guidance](https://developer.android.com/develop/background-work/background-tasks)
