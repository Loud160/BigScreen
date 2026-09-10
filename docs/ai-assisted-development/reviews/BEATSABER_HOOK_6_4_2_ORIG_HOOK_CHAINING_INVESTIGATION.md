# Beat Saber Hook 6.4.2 multi-mod original-hook chaining investigation

**Status:** Maintainer discussion brief; no upstream fix has been selected or
approved.

**Prepared:** September 7, 2026

**Runtime under investigation:** Beat Saber 1.40.8 on Quest, with the currently
supported 1.40.8 mod ecosystem resolving `beatsaber-hook` 6.4.2.

## Purpose

This document records a repeatable startup-crash signature and a code-level
problem found in Beat Saber Hook 6.4.2's `INSTALL_HOOK_ORIG` implementation. It
is intended to support a technical discussion with Fern and other BSMG/Quest
maintainers before any ecosystem-wide change is attempted.

The important distinction is that this is not simply "Chroma crashed" or "Big
Screen crashed." The tombstone shows Chroma failing while it installs a hook,
but multiple mods hook the same Unity method and the failure is consistent with
the shared 6.4.2 hook-chain bookkeeping becoming invalid. Chroma is where the
bad state became fatal; it may not be the component that originally created
that state.

## Executive summary

- A Quest 2 startup on September 7, 2026 ended in `SIGSEGV` while Chroma 2.10.3
  was installing its `BeatmapObjectSpawnController::Start` hook during
  `late_load`.
- Big Screen, Tracks, and Chroma all hook that same method in the source versions
  matching the installed 1.40.8 mod set.
- Big Screen uses the normal tracked `INSTALL_HOOK`; Tracks and Chroma use
  `INSTALL_HOOK_ORIG`.
- Beat Saber Hook 6.4.2 updates the tracked chain with the new hook's trampoline
  *before* `A64HookFunction` has populated that trampoline. At that point the
  value is normally null or stale.
- A later original-hook installer can consequently receive an invalid address.
  The defect is load-order dependent and is most exposed when three or more
  hooks share one target.
- Beat Saber Hook 8.2.1 is the current upstream release, but it is not a drop-in
  binary replacement for the 1.40.8 mods compiled against 6.4.2. Its hook system
  uses Flamingo and a different API/ABI.
- Because the faulty 6.4.2 sequence is a header template compiled into each
  mod's `.so`, replacing only `libbeatsaber-hook.so` cannot rewrite already-built
  callers.
- A local 6.4.x proof-of-concept reorders the bookkeeping correctly, but it has
  not been proposed as an upstream solution and must be tested across mixed old
  and rebuilt mods before it can be considered safe.

## Captured crash evidence

The retained tombstone is:

`diagnostics/startup-crash-20260907/extracted/recent-files/sdcard__Android__data__com.beatgames.beatsaber__files__tombstone_02`

The relevant facts are:

- Timestamp: `2026-09-07 00:00:28.038 -0400`
- Thread: `UnityMain`
- Signal: `SIGSEGV`, `SEGV_ACCERR`
- First native frame: `libc.so::__memcpy_aarch64_simd+36`
- Next frames:
  - `libchroma.so + 0x333f44`
  - `libchroma.so::BeatmapObjectSpawnControllerHook()+264`
  - `libchroma.so::late_load+688`
  - `libsl2.so`
- Chroma Build ID:
  `ade025acf33c57a165d6722887896d52ab7876a8`
- Registers `x6` and `x7` both pointed into executable code in
  `libtracks.so`, whose Build ID was
  `520af45d88793cd217d1f7c5354b59da7abee27b`.
- Big Screen was mapped in the process, but no Big Screen frame appears in the
  crashing stack.
- Saber Stage does not appear in the crashing stack.
- Noodle Extensions was loaded, but it does not hook
  `BeatmapObjectSpawnController::Start` in the inspected source and is not part
  of the confirmed same-target chain.
- The tombstone does not indicate an out-of-memory termination.

`BeatmapObjectSpawnControllerHook()` is Chroma's hook-installation function; the
stack does not show Chroma's replacement `Start` method executing during normal
gameplay. This is therefore an installation/chaining crash during startup or
scene initialization, not evidence of a fault in Chroma's normal per-frame
lighting logic.

## Same-target hook chain

The inspected source establishes the following overlap:

| Component | Target | Installation form |
| --- | --- | --- |
| Big Screen | `BeatmapObjectSpawnController::Start` | `INSTALL_HOOK` |
| Tracks | `BeatmapObjectSpawnController::Start` | `INSTALL_HOOK_ORIG` |
| Chroma | `BeatmapObjectSpawnController::Start` | `INSTALL_HOOK_ORIG` |

Relevant local source locations are:

- Big Screen: `src/main.cpp`
- Tracks: `src/Animation/Events.cpp`
- Chroma: `src/hooks/BeatmapObjectSpawnController.cpp`

The leading explanation for this specific crash is:

1. A standard tracked hook establishes the chain root for
   `BeatmapObjectSpawnController::Start`.
2. The first 6.4.2 `INSTALL_HOOK_ORIG` caller finds that tracked root.
3. It writes its not-yet-populated trampoline into the root's tracked `orig`
   field.
4. The native hook installation then populates the caller's trampoline, but the
   tracker has already stored the earlier null/stale value.
5. A later `INSTALL_HOOK_ORIG` caller asks the tracker for the current original
   address and receives the invalid value.
6. Native hook installation fails while operating on that bad address.

The tombstone is consistent with Big Screen being the tracked root, Tracks
being an earlier original-hook caller, and Chroma being the later caller where
the invalid state becomes fatal. That exact load order has not yet been proven
from a complete loader trace, so it must remain a **strong hypothesis**, not a
final attribution of fault to one mod.

## The 6.4.2 ordering defect

The official 6.4.2 implementation is in
[`shared/utils/hooking.hpp`](https://github.com/QuestPackageManager/beatsaber-hook/blob/v6.4.2/shared/utils/hooking.hpp#L650-L673).
Its effective order is:

```cpp
auto* origAddr = const_cast<void*>(HookTracker::GetOrig(addr));

if (origAddr != addr) {
    // Locate the existing tracked root.
    itr->second.front().orig = (void*) *T::trampoline();
}

// A64HookFunction is reached here and only now fills T::trampoline().
__InstallHook<T, L, false>(logger, origAddr);
```

For AArch64, `__InstallHook` calls `A64HookFunction`, passing
`T::trampoline()` as the location where the original/trampoline pointer will be
written. Therefore, assigning `*T::trampoline()` to the tracked chain before
`__InstallHook` runs records a value that has not yet been created.

The ordering needed for that design is conceptually:

1. Resolve the current chain address.
2. Install the new hook so its trampoline is populated.
3. Update the tracked root to point at that populated trampoline.

There is a second case that needs an explicit policy: if an
`INSTALL_HOOK_ORIG` caller is the first hook for a target, should it establish a
normal tracked root or remain untracked? The local proof of concept establishes
a tracked root, but that behavior should be confirmed with the maintainer
before it is treated as the 6.4.x contract.

## Version context: 6.4.2 versus 8.2.1

Beat Saber Hook's version is the **library API version**, not the Beat Saber game
version. A mod for Beat Saber 1.40.8 can therefore depend on Beat Saber Hook
6.4.2 even though newer Beat Saber Hook releases exist.

As of September 7, 2026, the official
[`releases/latest`](https://github.com/QuestPackageManager/beatsaber-hook/releases/latest)
link resolves to [v8.2.1](https://github.com/QuestPackageManager/beatsaber-hook/releases/tag/v8.2.1),
released September 4, 2026.

The current 8.2.1 implementation uses
[`flamingo::Install`](https://github.com/QuestPackageManager/beatsaber-hook/blob/v8.2.1/shared/hooking.hpp#L884-L960).
Its original-hook form marks the hook priority as final and delegates chain
management to Flamingo. This is structurally different from 6.4.2's mutable
`HookTracker` list.

The later code has also received explicit original-hook work, including commit
[`fa57486a` ("Fix orig hooks, bump flamingo, fix copy script")](https://github.com/QuestPackageManager/beatsaber-hook/commit/fa57486a527289d8a8ddbaec150c75317fd052c6).
That supports treating the newer hook path as materially changed; it does not by
itself prove that current 1.40.8 binaries can safely adopt it without rebuilds.

## Why replacing the shared library is not sufficient

`InstallOrigHook` is a template defined in a header. When a mod is compiled,
the call sequence is emitted into that mod's own native binary. An already-built
Chroma, Tracks, or other mod therefore retains the 6.4.2 ordering even if the
shared `libbeatsaber-hook.so` file on the headset is replaced.

This creates two separate compatibility questions:

1. **Source compatibility:** Can a mod be rebuilt with a corrected 6.4.x header
   without changing its hook declarations?
2. **Runtime compatibility:** Can rebuilt callers safely coexist with legacy
   6.4.2 callers that still contain the old inline implementation?

A source-compatible 6.4.3-style fix may be straightforward. Protecting every
already-released binary centrally is harder because those binaries directly
read and mutate the `HookTracker` map. A runtime compatibility shim might be
possible, but it is not established and would be higher risk than a normal
header fix.

## Scope of ecosystem exposure

An inventory of the installed 1.40.8 mod set found 40 packages, excluding Beat
Saber Hook itself, declaring a Beat Saber Hook dependency. Seventeen distinct
local source repositories used `INSTALL_HOOK_ORIG` somewhere:

- BeatLeader
- BetterSongSearch
- BSNightcore
- Chroma
- ClockMod
- CustomJSONData
- GraphicsTweaks
- MoreButtons
- NoodleExtensions
- ParticleTuner
- PlaylistCore
- QuestJDFixer
- QuestSounds
- RecentlyPlayed
- ScorePercentage
- Tracks
- Unicode

This is an **exposure inventory**, not a crash attribution list:

- It does not mean all 17 mods participated in this crash.
- It does not mean all 17 hook the same target.
- It does not prove every use can trigger the defective multi-hook path.
- It does not mean all 17 must be rebuilt simultaneously before any targeted
  mitigation can be tested.

For this captured crash, the confirmed same-target overlap is Big Screen,
Tracks, and Chroma. Noodle Extensions uses original hooks on other targets and
therefore matters to broader compatibility testing, but not to the exact
`BeatmapObjectSpawnController::Start` chain established here.

## Remediation options

### 1. Targeted rebuild of the confirmed collision group

Rebuild Big Screen, Tracks, and Chroma against an agreed safe 6.4.x original-hook
implementation, then test all loader-order permutations.

**Advantage:** Smallest set that directly addresses the captured crash.

**Limitation:** Other multi-mod hook collisions may remain elsewhere in the
ecosystem, and the rebuilt mods still need to coexist with legacy callers.

### 2. ABI-compatible 6.4.x backport

Publish a new 6.4.x release containing a corrected header and rebuild affected
mods over time. Do not modify or silently replace the already-published 6.4.2
assets.

**Advantage:** Preserves the familiar 6.4.x source API and offers a controlled
migration for the current 1.40.8 ecosystem.

**Limitation:** Old binaries retain the old template. The backport only fixes a
mod after that mod is rebuilt, unless a separate runtime guard is also proven.

### 3. Legacy runtime compatibility layer

Investigate whether `HookTracker::GetOrig`, `GetHooks`, or hook-installation
entry points can detect and repair invalid legacy chain state before it reaches
the native hook engine.

**Advantage:** The only approach that might protect old, unrecompiled mods.

**Limitation:** High risk. The old template mutates the returned container
directly, so a shared library has limited control over operation ordering. This
must not be promised until demonstrated with mixed legacy and rebuilt binaries.

### 4. Migrate the 1.40.8 ecosystem to Beat Saber Hook 8.x

Port and rebuild dependent mods against the Flamingo-based API.

**Advantage:** Moves away from the old HookTracker design and aligns with the
current upstream hook system.

**Limitation:** This is a coordinated API/ABI migration, not a drop-in library
update. It may require changes across many independently maintained mods and is
not a practical immediate response to one startup crash.

## Local proof-of-concept correction

A local, uncommitted audit branch currently changes the 6.4.2 flow to:

- validate the destination;
- establish a tracked root if no tracked hook exists yet;
- install the new original hook first;
- update the tracked root only after the new trampoline has been populated; and
- expose a matching direct-address form for controlled tests.

This patch is evidence that the ordering can be corrected without immediately
rewriting every mod's hook declaration. It is **not** an upstream proposal or a
production-ready release. It still needs maintainer review and a deliberate
mixed-version/load-order test matrix.

## Required validation matrix

Any proposed fix should be tested with instrumented hooks that record call order
and reject null trampolines. At minimum:

- one normal tracked hook;
- one normal hook followed by one original hook;
- one normal hook followed by two original hooks;
- three original hooks when none explicitly establishes a normal root;
- old 6.4.2 callers mixed with rebuilt callers;
- every install/load-order permutation for a three-mod chain;
- hooks in one module and across separate `.so` modules;
- startup, `late_load`, scene changes, and repeated launches;
- calls verified to reach each hook exactly once and then reach the real game
  method exactly once;
- failure behavior verified to stop safely with a useful diagnostic instead of
  passing a null/stale address to the native hook engine.

The real Big Screen/Tracks/Chroma chain should then be tested through repeated
cold starts, menu entry, map starts, restarts, exits, and maps that activate
Chroma/Tracks features.

## Questions for Fern and the maintainers

1. Is Beat Saber Hook 6.4.x still intended to be a supported base for the
   current Beat Saber 1.40.8 Quest ecosystem?
2. Is the pre-install assignment in 6.4.2 `InstallOrigHook` a known defect, and
   are the intended first-, second-, and third-hook semantics documented
   elsewhere?
3. Does the Flamingo/original-hook work in the 7.x/8.x line address this exact
   class of multi-hook chain failure, or only a related issue?
4. Would maintainers accept an ABI-compatible 6.4.x backport so current mods can
   be rebuilt without an immediate ecosystem-wide 8.x migration?
5. Can legacy 6.4.2 binaries be protected through the shared runtime, or does
   the inline container mutation mean each affected original-hook caller must
   be rebuilt?
6. If corrected and legacy callers are mixed, what chain invariants must the
   corrected implementation preserve?
7. How does MBF currently resolve mods requiring different Beat Saber Hook
   major versions, and is side-by-side compatibility intended or unsupported?
8. Is there an existing hook-chain test harness, or would a small three-module
   permutation test be useful as an upstream regression test?
9. For an immediate field mitigation, is rebuilding only the confirmed
   Big Screen/Tracks/Chroma collision group reasonable while the general
   migration path is decided?

## Short Discord-ready technical summary

> I captured a Beat Saber 1.40.8 startup SIGSEGV while Chroma 2.10.3 was
> installing its `BeatmapObjectSpawnController::Start` hook. Big Screen hooks
> that method with `INSTALL_HOOK`, while Tracks and Chroma both use
> `INSTALL_HOOK_ORIG`. In Beat Saber Hook 6.4.2, `InstallOrigHook` writes the new
> hook's trampoline into `HookTracker` before `A64HookFunction` has populated
> that trampoline, so a later hook on the same target can receive a null/stale
> address. The tombstone is consistent with that failure and has registers
> pointing into Tracks while the fatal installation occurs in Chroma. The bad
> sequence is a header template compiled into each mod, so replacing only the
> shared 6.4.2 `.so` cannot fix existing binaries. Current Beat Saber Hook is
> 8.2.1 and uses Flamingo, but that is an API/ABI migration rather than a drop-in
> fix for current 1.40.8 mods. I would like guidance on whether an ABI-compatible
> 6.4.x backport is appropriate and whether legacy binaries can be guarded
> centrally, or if affected hook callers must be rebuilt.

## Conclusion

The captured failure is best described as a **shared hook-chain integrity
problem exposed during Chroma hook installation**, with Big Screen, Tracks, and
Chroma forming the confirmed same-target collision group. It should not be
presented as proof that Chroma alone is defective, nor as proof that every mod
using `INSTALL_HOOK_ORIG` is currently crashing.

The safest next action is maintainer review of the 6.4.2 ordering and intended
chain semantics, followed by a small reproducible three-hook test. A targeted
6.4.x source-compatible correction appears feasible, but the mixed-binary and
ecosystem deployment questions need to be answered before distributing it.
