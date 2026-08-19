# Big Screen — Full Professional Code Review

**Reviewed:** current `main` @ `54bb2a1` ("Add professional code review task document"), working tree examined.
**Method:** read-only review of the entire first-party tree at HEAD (36 `.cpp`, 41 `.hpp`, ~44k lines of C++; 40 build/script files; 35 docs). Seven focused sub-reviews (decoder/media, render/screen, downloader/runtimes, hooks/menu, error-handling/settings, build/deploy/security, docs/tests) with every high- and medium-severity finding independently re-verified against the source before inclusion. Findings that could not be reproduced against HEAD were rejected (see §15).
**Nothing was modified.**

> **Impartiality note.** Portions of the recently-changed code in this tree (the video-shader tier ladder, the Cinema bloom prepass, the support-log collector, and related wiring) were produced with assistance from this same model family. This review was conducted adversarially against the actual source at HEAD and deliberately re-verified its own most load-bearing claims; two initially-reported findings were withdrawn on verification (§15). Readers comparing this against an independent review should still weight that overlap.

---

## 1. Executive summary

Big Screen is a large, **unusually disciplined** alpha-stage Quest mod. The core engineering that matters most for a native IL2CPP mod — object lifetime/ownership, the Unity-main-thread invariant, decoder worker shutdown ordering, and transactional replacement of live resources — is handled correctly and consistently, with accurate in-code comments explaining *why*. Across seven subsystem deep-dives, **no Critical or High-severity confirmed defect was found**. The decoder, render lifecycle, hook layer, downloader runtime isolation, and filesystem-removal safety are all genuinely strong.

The real issues are concentrated in three lower-stakes areas: (a) **feature-state ambiguity** — a substantial feature (the Cinema-style frame-glow bloom prepass) plus the mapper `bloom`/`colorBlending` *effects* are carried in the tree but compiled out with `#if 0`, so those map fields parse without effect; (b) **tooling and process** — the safest source-removal path is dead in production, deploy hash-verification is incomplete, several helper scripts are unreferenced, and the working tree carries whole-file line-ending churn; and (c) **testing and docs hygiene** — a strong behavioral test core sits beside a very large brittle source-substring test layer, several headline invariants (download cancellation, library backup rotation, updater rollback) are asserted only as text presence, and internal AI task-spec/planning documents are linked from the public docs index as authoritative developer documentation.

**Verdict: READY FOR FIRST-ROUND BETA** on code quality, with a short list of recommended (non-blocking) pre-beta polish. Field maturity still requires on-device testing of the hardware/MediaCodec decode paths and a decision on the disabled bloom feature.

---

## 2. Scope / repository state

- **Branch/commit:** `main` @ `54bb2a1`.
- **Working tree:** reported as 61 files "modified", but `git diff --ignore-cr-at-eol --ignore-all-space` is **empty** — the dirtiness is entirely CRLF/LF line-ending churn; there are **no uncommitted content changes** and no untracked files. HEAD is therefore the authoritative content and is what was reviewed. (The EOL churn itself is a Low finding — see L7.)
- **Reviewed:** all of `src/`, `include/`, `tests/`, `scripts/`, `cmake/`, `CMakeLists.txt`, `.github/`, `mod.template.json`, `qpm*.json`, `README.md`, and the `docs/` set. Vendored third-party implementations (FFmpeg, CPython, QuickJS, beatsaber-hook, BSML) were reviewed only at Big Screen's integration boundary.
- **Snapshot caveat:** four long `docs/*.md` workflow files and a couple of tool-project files were not copied into the review snapshot; claims depending on them were verified directly against the device working tree. This produced two initially-reported findings that were withdrawn on verification (§15).

---

## 3. Overall quality assessment (ratings in §17)

The codebase reads as **deliberately engineered, not incrementally patched**, in its runtime core. Ownership is explicit, teardown is idempotent and liveness-checked, and the hard Quest/IL2CPP hazards (fake-null objects, cross-thread engine calls, virtual-base re-entry, GC across frames) are handled with awareness and comments. The weaker areas are peripheral to correctness: build/deploy tooling has accumulated dead paths, the test suite over-relies on source-substring assertions, and the public documentation set mixes authoritative docs with internal planning material. None of the weaknesses is a stability risk to a player.

---

## 4. Critical findings

**No confirmed findings.**

---

## 5. High findings

**No confirmed findings.**

(Two candidates initially raised at High/Medium — "four dead documentation links" and "Unity editor version mismatch" — were **rejected on verification**; see §15.)

---

## 6. Medium findings

### M1 — Cinema frame-glow feature and mapper `bloom`/`colorBlending` effects are compiled out (`#if 0`); those map fields parse without effect
- **Severity/Confidence:** Medium / High (verified directly)
- **Files:** `src/CinemaBloomRenderer.cpp` (`#if 0` at line 11 … `#endif` at 735 — the *entire* translation unit), `src/ScreenSurface.cpp` (`#include "CinemaBloomRenderer.hpp"` under `#if 0` at 21; `RegisterSource`/`UpdateSource`/`UnregisterSource` sites under `#if 0` at 862, 1321, 2421 with `bloomRegistered_` hard-set `false`; `colorBlending_ = config.colorBlending.value_or(false)` disabled at 1542–1543 and replaced by `colorBlending_ = false;` at 1545; `(void)colorBlending;` at 373), `src/main.cpp` (camera hook block `#if 0` at 1014; **no** `INSTALL_HOOK(..., Camera_FireOnPreRender)`), `src/MapVideoConfig.cpp` (still parses `bloom`/`colorBlending`).
- **Category:** Feature-state / maintainability / correctness-of-expectations.
- **What/why:** The screen material deliberately clears Beat Saber's per-pixel bloom-emission alpha (the fix that stops bloom-heavy maps whiting the screen out), so the *only* mechanism that could produce a video-driven frame glow — `CinemaBloomRenderer` drawing the screen into the bloom pre-pass from a `Camera.FireOnPreRender` hook — is entirely disabled. Simultaneously `colorBlending_` is forced to `false`, so Cinema soft-additive blending never applies regardless of a map's `colorBlending:true`. `MapVideoConfig` still parses both `bloom` and `colorBlending`, so a mapper's values are accepted and silently ignored.
- **Consequence:** No crash. But ~730 lines of feature code plus wiring sit inert in the tree, two documented mapper fields have no runtime effect, and a maintainer cannot tell from the code alone whether this is a temporary hold or an abandoned path. `docs/KNOWN_ISSUES.md`/`MAPPER_FORMAT.md` do currently state bloom is compiled out (docs are accurate here — a strength), but the divided state (parse-yes / apply-no / dead-code-retained) is the kind of ambiguity a professional review is meant to surface.
- **Smallest correction:** Decide the feature's disposition and make the tree reflect it: either (a) finish and enable the prepass + `colorBlending`, or (b) remove the `#if 0` blocks and the now-unused parse-only fields (or explicitly gate them behind a clearly-named "experimental" setting). Whichever is chosen, keep the mapper-format doc and the parser in agreement.
- **Test:** A `MapVideoConfigTests` case asserting the documented behavior of `bloom`/`colorBlending` (either "applied" or "accepted-but-ignored-by-design") so the intent is pinned.

### M2 — `ErrorManager::dialogVisible_` can latch `true` with no reset path, silently muting the user-visible error channel for the rest of a session
- **Severity/Confidence:** Medium / Medium (structure verified; external-dismissal behavior is **NEEDS VERIFICATION**)
- **File:** `src/ErrorManager.cpp` — `TickMainThread` (356, 361), `ResetCircuitBreaker` (447+).
- **Category:** Error-handling state inconsistency.
- **What/why:** `dialogVisible_` is set `true` when a prompt is presented (361) and cleared only in the OK-button delegate (397), the in-transition re-queue branch (377), and the two presentation-failure paths (421, 440). If Beat Saber dismisses the presented `SimpleDialogPromptViewController` through its own flow (scene/menu transition) without invoking the delegate, nothing clears the flag. `TickMainThread` then early-returns at 356 (`... || dialogVisible_ || ...`) for the remainder of the session, and — verified — `ResetCircuitBreaker` (the documented user recovery: toggle Big Screen off/on) resets six fields but **not** `dialogVisible_`.
- **Consequence:** Subsequent user-visible errors are still logged and recorded to history, but never shown as a dialog for the rest of the session. Silent degradation of one error channel; file/history logging is unaffected.
- **Smallest correction:** Clear `dialogVisible_` in `ResetCircuitBreaker`, and/or, before presenting, detect that a previously-presented prompt has left the hierarchy and reset the flag then.
- **Test:** Present a dialog, simulate external dismissal without the delegate firing, enqueue a second `ReportUserVisible`, tick, assert the second dialog presents.

### M3 — Internal AI task-spec / workflow documents are linked from `docs/README.md` as official developer documentation
- **Severity/Confidence:** Medium / High (links verified at `docs/README.md:29–32`; target files confirmed present on HEAD)
- **Files:** `docs/README.md:29–32` → `MENU_DOWNLOAD_SESSION_LOGGING_AND_SOURCE_INSTALL_REMOVAL_WORKFLOW.md`, `RESOLVE_LOGGING_WRITER_DESIGN_AND_REPRODUCE_SOURCE_DEPLOY_MBF_CONFLICT.md`, `RESOLVE_MIXED_MBF_SOURCE_OWNERSHIP_AND_LEGACY_SOURCE_INSTALLS.md`, `DETAILED_DIAGNOSTIC_SESSION_LOGGING_AND_OWNERSHIP_SAFE_SOURCE_DEPLOY_REMOVAL.md`. Related: `docs/bigscreen-movement-authoring-planning.md` (internal planning doc sitting in public `docs/`, unlinked), and `docs/Full Professional Code Review of Big Screen.md` (this review's own task prompt, committed to `docs/` in the HEAD commit).
- **Category:** Documentation hygiene / professionalism.
- **What/why:** The linked four are investigation/requirements/"implementation contract" workflow notes — internal design-conversation artifacts — presented in the developer index with authoritative framing ("original requirements…", "the implementation contract…"). Per a professional-repo standard, internal planning/prompt/spec material should not be presented as canonical documentation. A prompt document (the review task spec) is also committed into `docs/`.
- **Consequence:** A developer browsing the docs index treats internal, possibly-stale planning prose as the authoritative design contract; the public docs surface looks like a working scratchpad rather than a curated set.
- **Smallest correction:** Move internal planning/spec/prompt files out of the public `docs/` tree (or into a clearly-labeled `docs/internal/` excluded from the index), and trim `docs/README.md` to the curated, maintained documents (ARCHITECTURE, BUILDING, USER_GUIDE, SETTINGS, MAPPER_FORMAT, TROUBLESHOOTING, DOWNLOADER_SECURITY, DEPENDENCIES, PRIVACY, etc.).

### M4 — The receipt-based (safest) source-deploy/removal path is dead in production, so real installs can only ever be classified `LEGACY_SOURCE`
- **Severity/Confidence:** Medium / High
- **Files:** `scripts/source-install-ownership.ps1` (receipt/plan functions, ~247–413) vs its only runtime consumer `scripts/remove-bigscreen.ps1`; deploy path `scripts/copy.ps1` (196–286) writes no receipt.
- **Category:** Tooling / removal-safety / stale module doc.
- **What/why:** `copy.ps1` deploys with its own `adb push` loop and never writes `source-install.json`, so `Get-BigScreenInstallClassification` can only return `LEGACY_SOURCE`; the hash-proven receipt states (`SOURCE_MANAGED`/`SOURCE_PARTIAL`) are unreachable in normal use. The module header still claims it is "Shared by source deployment, source removal, and isolated policy tests," but no deployment consumer exists — the receipt machinery is exercised only by `RepositoryInvariantTests.py`.
- **Consequence:** The safest removal path (baseline backup/restore, per-file ambiguity preservation) is never used; a large body of removal-safety code is untested against real installs; the module doc misleads maintainers. Practical removal still preserves user media and shared deps via the `LEGACY_SOURCE` branch, so no data-loss today.
- **Smallest correction:** Either wire `copy.ps1` to `Get-BigScreenDeploymentPlan`/`Install-BigScreenSourcePlan` so real installs produce receipts, or update the module header + docs to state the receipt path is presently test-only.

### M5 — `RepositoryInvariantTests.py` is overwhelmingly source-substring assertions; several headline invariants are "tested" only as text presence
- **Severity/Confidence:** Medium / High
- **Files:** `tests/RepositoryInvariantTests.py` (~2000 lines, ~931 bare `assert "<source substring>" in file_text`), `tests/DownloaderScriptTests.py` (cancellation/updater-rollback asserted as text counts), absent `VideoLibraryTests`.
- **Category:** Test quality / coverage.
- **What/why:** The invariant suite mostly asserts that specific source text still appears; it passes with broken behavior and can false-fail on behavior-preserving refactors. Three specifically-important invariants are asserted only as text presence, never exercised: **download cancellation** (marker text only — no test sets the cancel file mid-download and observes a stop), **library backup rotation / corruption recovery** (`VideoLibrary.cpp`'s `.backup1`/`.backup2` and rebuild logic has no dedicated test), and **updater rollback** (rejected-candidate restore checked only by substring). `VideoLibrary.cpp`, the C++ orchestration in `DownloadManager.cpp`, `StorageManager.cpp`, and `ErrorManager.cpp` have no dedicated behavioral tests.
- **Consequence:** Real regressions in exactly the flows the project most advertises can pass CI green.
- **Smallest correction:** Add host-buildable behavioral tests for (1) cancellation (extend the existing `exec()`-based fake-`yt_dlp` harness to set the cancel marker mid-run and assert halt+cleanup), (2) `VideoLibrary` backup rotation / corrupt-primary→backup→rebuild / interrupted-write atomicity, and (3) updater rejected-candidate rollback. Treat the substring invariants as a supplementary guard, not the primary one.

---

## 7. Low findings

- **L1 — `menuScreenPreviewEnabled` legacy key discarded, not migrated.** `src/Settings.cpp:204` reads only `showMenuPreview` (default `true`); `:1039` removes `menuScreenPreviewEnabled` under a "…after migration" comment, but no fallback read exists (every *other* renamed key has one). If that key ever shipped, a user who disabled the menu preview gets it silently re-enabled on upgrade. Fix: `menuPreviewEnabled_ = ReadBool(document, "showMenuPreview", ReadBool(document, "menuScreenPreviewEnabled", true));`. (Confidence Medium on whether the key shipped; the discard itself is verified.)
- **L2 — Deploy hash-verification is incomplete.** `scripts/copy.ps1` calls `Assert-QuestFileMatches` for `modFiles`/`lateModFiles` (221, 233) but **not** for `libraryFiles` (241) or `fileCopies` (270) — so `libpython3.14.so`, the FFmpeg sets, and ~30 runtime files are pushed with only an exit-code check. A silently corrupted library push deploys a mismatched ABI without the loud failure the mod paths would raise. Fix: call `Assert-QuestFileMatches` in those two loops too.
- **L3 — `noexcept` `Guard` calls an allocation-heavy, throw-capable reporter.** `include/BigScreen/ErrorManager.hpp:53–70` `Guard` is `noexcept` (its whole purpose is to shield IL2CPP from mod exceptions), but its catch calls `ReportInternal` (`src/ErrorManager.cpp`), which does `std::string` concatenation / `make_pair` and is not `noexcept`. Under OOM (the exact case where the guarded op most likely threw), a `std::bad_alloc` from the reporter escapes a `noexcept` function → `std::terminate` → the crash Guard exists to prevent. Fix: wrap `ReportInternal`/`ReportUserVisible` bodies in `try{}catch(...){}` so Guard degrades to log-only.
- **L4 — Hardware-decoder `Open()` is unbounded on the Unity main thread.** `src/FrameDecoder.cpp` arms the interrupt/deadline only around `avformat_open_input`/`find_stream_info` (cleared ~288); `avcodec_open2` (~421), which configures/starts MediaCodec for `*_mediacodec`, runs with no deadline, synchronously on the thread that called `PlaybackSession::Start`/`PrewarmGameplay`. A slow vendor MediaCodec init hitches/handshakes the scene-transition thread. No crash. Fix: bound `Open` on a task and gate `Start` on completion, or document the unbounded main-thread cost.
- **L5 — Single serial retirement reaper.** `src/FrameDecoderFacade.cpp:76–104` — one detached thread drains retired backends and calls `Close()` (join). Retirement is used precisely for workers that didn't stop within the 4 ms UI budget; if one is genuinely wedged inside a blocking avcodec call (where `stopWorker_` can't be observed), the reaper blocks forever and later-retired backends accumulate (a few MB each). Bounded by user restart count; no crash. Fix: detach-per-wedged-backend or bound+log the queue.
- **L6 — `SelectionVideoToggle` stores scene-root UI as raw IL2CPP pointers.** `controlsScreen_` (a scene-root `FloatingScreen`) and the download-row objects are raw `T*`, nulled only via the detail-view `StandardLevelDetailView_OnDestroy` hook (`main.cpp:1146`), and dereferenced from `TickDownloadUi` every menu frame with raw `if(ptr)` checks (not liveness checks). Safe under normal scene-unload; the residual risk is exactly the "MenuCore soft restart frees scene objects without nulling native singletons" hazard the code itself warns about elsewhere. Fix: store as `UnityW<...>` and test with `isAlive`, matching the discipline used in `MenuFlowCoordinator`.
- **L7 — Whole-tree line-ending churn / no effective EOL normalization.** The working tree shows 61 files "modified" that are byte-identical to HEAD except for CRLF/LF. Mixed-EOL writes from different tools plus an absent/ineffective `.gitattributes` `text=auto eol=` policy will keep producing noisy diffs and can mask real changes in PRs. Fix: add a `.gitattributes` normalization policy and renormalize once.
- **L8 — `pull-tombstone.ps1` aborts if any tombstone slot is missing.** Lines 33–39 `exit 1` on the first absent `tombstone_0N` even if an earlier slot was found, effectively requiring all three. Legacy helper superseded by the robust `collect-crash-logs.ps1`. Fix: `continue` on empty; fail only if none found (or retire the script).
- **L9 — `Build-And-Deploy.bat` advertises the wrong downloader version.** Line 33 shows `yt-dlp 2026.07.04`, but the build pins/downloads nightly `2026.08.18.122307`; the fetchers explicitly warn that `2026.07.04` reintroduces the Android-VR 403 regression. A provenance-transparency string contradicting what is actually fetched. Fix: echo the pinned version (or read it from the manifest).
- **L10 — Narrow data race on diagnostic-throttle members.** `DownloadManager::Start` resets `lastDownloaderDiagnostic_` (a `std::string`) et al. **without** `statusReadMutex_` (2506–2509), which the header documents as serialized by that lock; a lingering status-poll from a just-finished operation can write the same members concurrently (rapid finish→start). Concurrent `std::string` write/write is UB. Narrow window (gated by `operationBusy_` + `EndDownloadSession` ordering) → Low. Fix: take `statusReadMutex_` around the reset, or move it into the queued op body.
- **L11 — Zero-frame EOF can leave menu-preview audio gated.** `src/FrameDecoder.cpp` treats `AVERROR_EOF` before any frame as normal (no error, `firstFrameUploaded_` stays false); `PlaybackSession::SynchronizedAudioReady` gates preview audio on `firstFrameUploaded_`. A truncated-but-parseable MP4 could keep a song's preview silent. Gameplay unaffected; no crash. Fix: treat "EOF with zero frames ever decoded" as a soft failure that resolves readiness or records a note.
- **L12 — `DEPENDENCIES.md` FFmpeg table wording contradicts the shipped default.** The 4.4.8 row reads as primary/"private", the 9.0.1 row as "the comparison" runtime, but `Settings` defaults `useFfmpeg9_ = true` (9.0.1 is default) — as the same doc's later prose (174–176) and the README correctly state. Fix: reword the table rows to match.
- **L13 — Dead ADB-session helper; ADB left running after deploy.** `scripts/complete-adb-session.ps1` (graceful, ownership-aware shutdown) has no runtime caller (only the invariant test); `copy.ps1`/`remove-bigscreen.ps1` leave the server up, which the scripts' own messaging says can hinder MBF connections. Fix: call it from those paths (it already accepts `-WasRunningAtStart`), or retire it.

---

## 8. Threading / lifetime assessment

**Strong.** The Unity-main-thread invariant holds throughout the code paths traced: the decoder `WorkerLoop` and all callees do only FFmpeg + CPU RGBA work (no Unity/Paper/IL2CPP), and every Unity mutation traces to a Unity callback or the SongCore selection event on the main thread. Decoder shutdown is correctly ordered — `RequestStop()` → `join()` **before** freeing FFmpeg contexts on every path, with an off-thread retirement queue for workers that exceed a 4 ms main-thread stop budget. Mailbox condition-variable discipline is textbook (predicate state mutated under its mutex before every notify; drop-old single-frame mailbox). Cross-frame Unity references in the menu/showcase/capture-restore helpers use `UnityW` + `isAlive` gating consistent with the documented IL2CPP fake-null teardown hazard. The only concurrency defect found is the narrow, gated `std::string` race in L10. One item worth verifying, not a confirmed defect: the showcase's reused `ArrayW<T>` vertex/UV buffers are held across frames in C++ members with no explicit GC root (rendering reviewer F1) — this ships working on Android IL2CPP because Boehm conservatively scans loaded `.so` data segments, but a `SafePtr`/rooted handle (or a documented assumption) would remove the latent dependence on that behavior.

## 9. Error-handling consistency assessment

**Coherent and largely well-executed.** There is a real, applied four-way policy: internal fault → `ReportInternal` (counts toward a circuit breaker); handled/expected → `RecordError` (log-only); optional-feature failure → `RecordError` + disable + continue; fatal init → `Guard` around `Load`/`VideoLibrary`/`DownloadManager` so Big Screen disables cleanly without crashing Beat Saber. Sampling 25 files showed this held. Every Unity hook body is wrapped in a `noexcept` `Guard` that logs/queues rather than leaking into IL2CPP. Settings persistence is a highlight (full load/save symmetry across ~40 scalars and 27 layout fields, defensive clamping, corrupt-file quarantine, transactional preview editing). The residual issues are edge-triggered, not structural: the latchable `dialogVisible_` (M2), the `Guard`→throwing-reporter gap (L3), the single missed key-migration (L1), and unbounded intra-session growth of `error-history.log` (rotation only checked at startup — maintainability).

## 10. Comments / documentation-in-code assessment

**Above professional bar.** Non-obvious decisions carry accurate rationale: the IL2CPP fake-null → `GetComponent` → BSML-abort chain, the deliberate cleanup-before-original ordering in teardown hooks, the virtual-base re-entry avoidance in the flow coordinator, the FFmpeg EAGAIN/EOF distinctions, and the `CT_DISABLE_LIVENESS_CHECKS` and disabled-hook workarounds. Stale comments were rare; the notable one is the `menuScreenPreviewEnabled` "…after migration" comment (L1) describing a migration that does not happen, and the `source-install-ownership.ps1` header claiming a deployment consumer that does not exist (M4).

## 11. README / GitHub docs accuracy

**High for concrete claims; a hygiene problem for doc curation.** Every spot-checked numeric/behavioral claim matched the code exactly: Beat Saber `1.40.8_7379`; the software-decode ceiling (HEVC blocked, >1080p short-edge blocked from software fallback); the 1440p/VP9-WebM download logic; parser limits (1 MiB config, 256 environment entries, 32 additional screens, filename priority); storage path; diagnostic-logging default-on and 10-session retention; the cookie/authorization/PO-token/signed-URL redaction; the QuickJS sandbox numbers (128 MiB / 512 KiB stack / 16 MiB in / 8 MiB out / 30 s); the 32 MiB updater cap; two rotating library backups; and — accurately — that bloom is compiled out. The problems are curation, not accuracy: internal AI workflow/spec docs linked as authoritative (M3) and the `DEPENDENCIES.md` FFmpeg-table wording (L12). The Unity editor version is **correct** (`ProjectVersion.txt` = `2022.3.33f1`, matching the docs and the build script's pin — see §15).

## 12. Testing / CI assessment

**Strong behavioral core, over-large brittle layer.** Genuinely good, behavior-exercising tests exist: `FrameDecoderTests` runs the real decoder against generated fixtures and checks pixel-level vignette/blackout output, EOF-then-seek loop restart, and the RGBA buffer-pool ceiling; `CoreLogicTests` and `MapVideoConfigTests` call real logic/parsers with real assertions; and `DownloaderScriptTests`' HTTP-403 recovery and release-channel tests `exec()` the actual embedded Python against a fake `yt_dlp` and assert real on-disk state. Against that, `RepositoryInvariantTests.py` is ~931 substring assertions that can pass while behavior is broken (M5), and the three headline invariants (cancellation, backup rotation/recovery, updater rollback) plus `VideoLibrary.cpp`/`StorageManager.cpp`/`ErrorManager.cpp` lack behavioral tests. CI (`core-tests.yml`) genuinely runs `ctest` + the Python tests on every push/PR (ffmpeg-dev and Python are installed in the workflow, so the conditional C++/Python tests do run); the Quest/QMOD `build-ndk.yml` runs independently with no `needs:` on `core-tests`, so whether tests block merges depends on branch-protection "required status checks" (not visible in the repo — **verify in GitHub settings**).

## 13. Build / deploy / remove assessment

**Careful and security-conscious, with accumulated dead paths.** Verification-before-use is universal and correctly ordered across all fetchers (python, yt-dlp, certifi, quickjs, rapidjson-by-commit, ffmpeg, ndk, platform-tools, qmod-schema, QPM-in-CI), including Google-LLC Authenticode on `adb.exe`; extraction is by explicit entry name (no zip-slip); the FFmpeg LGPL/GPL/HEVC/SONAME/symbol-isolation boundary is machine-asserted at build and re-verified at package time; reproducibility is real (`build_downloader_runtime.py` does a byte-for-byte compare against the pinned yt-dlp release); destructive removal is receipt/hash-driven with a preserve-on-doubt bias and layered ModData-prefix/`..`/wildcard guards; packaging is atomic (`[IO.File]::Replace` of a validated temp qmod). The weaknesses are M4 (receipt-deploy path dead in production), L2 (library/fileCopies not hash-verified on deploy), L13/L8/L9 (dead or wrong helper scripts), and a minor device-shell quoting inconsistency in `copy.ps1` vs the hardened ownership module (trusted input today).

## 14. Security assessment

**Appropriate and practical for a Quest mod.** No path traversal, command injection, unsafe extraction, or unverified-executable execution was found on the realistic input surfaces (remote downloads, yt-dlp, release/update manifests, map-provided config, filenames/paths, shell invocation). URLs are built from pinned version constants; remote `mod.json` values used by the MBF-registration reader all pass `Assert-BigScreenRemotePath` before any shell use; log redaction covers cookies/authorization/PO tokens/signed URLs; the QuickJS challenge engine runs in a memory/stack/time/output-bounded sandbox with a fresh runtime per job and the GIL released across the bridge. No security defect rises above the tooling/robustness items already listed.

---

## 15. Potential issues investigated and rejected

**Two initially-reported findings were withdrawn on verification (both were snapshot artifacts, not defects):**
- **"`docs/README.md` links to four non-existent documents (High)."** Rejected — all four files (`MENU_DOWNLOAD_SESSION_LOGGING_…`, `RESOLVE_LOGGING_WRITER_…`, `RESOLVE_MIXED_MBF_…`, `DETAILED_DIAGNOSTIC_SESSION_…`) **exist and are git-tracked on HEAD**; they simply weren't in the review snapshot. The real, verified issue is that they are *internal workflow/spec* docs linked as authoritative developer documentation (reclassified as M3).
- **"Unity editor version mismatch: project is `2022.3.50f1` but docs mandate `2022.3.33f1` (Medium)."** Rejected — the tracked `tools/video-shader/ProjectSettings/ProjectVersion.txt` on HEAD is `m_EditorVersion: 2022.3.33f1` (revision `b2c853adf198`), matching the docs and the `build-video-shader.ps1` pin. The reviewer read a stale out-of-snapshot copy.

**Concerns traced to a real guard and correctly rejected:**
- **Decoder worker use-after-free / Unity access off-thread** — `Close()` joins the worker before freeing any FFmpeg object; the worker never touches Unity/IL2CPP.
- **AVPacket dropped on EAGAIN backpressure** — retained in `compressedPacketPending_` and re-submitted; unref'd on seek/close.
- **Double-free/leak on `Open` error paths** — every failure routes through the idempotent, null-guarded `Close()`.
- **Software fallback into a prohibited decoder (HEVC / >1080p)** — gated at startup and mid-stream via `SoftwareFallbackBlockedReason`.
- **Camera bloom hook touching a dying surface; per-frame Kawase cost; clone owning the shared texture** — the entire bloom path is `#if 0` (M1); clones set `ownsTexture_=false` and null (not destroy) the shared texture; owner is destroyed last.
- **Destroy-first geometry replacement** — `UpdateGeometry` builds new meshes into temporaries and swaps only on full success, restoring the old meshes on failure (transactional; no gray flash).
- **Downloader callbacks into a torn-down menu** — there is no callback model; menus poll a mutex-protected `Snapshot()`; workers touch only files/atomics.
- **GIL / QuickJS lifetime** — `PyEval_SaveThread` on every exit, `ScopedPythonGil` for worker work, GIL released across the JS↔Python bridge; fresh `JSRuntime`/`JSContext` per call with opaque cleared before free.
- **Partial `.part` files leaking on cancel** — intentionally retained for resume; `StorageManager` reaps abandoned `.part`/`.incoming` > 1 h.
- **Atomic replace destroying the last good video** — `StagedFileReplacement` keeps a backup and restores on any throw.
- **Hook exceptions escaping into IL2CPP** — every hook body is wrapped in `noexcept` `Guard`; the few un-guarded trailing calls are all `noexcept` functions; `DidActivate`/`DidDeactivate` correctly avoid re-entrant virtual-base dispatch.
- **PowerShell `& child.ps1; if(-not $?)` swallowing child `exit 1`** — verified empirically that for *script-file* invocation `$?` is `$False` after a child `exit 1`; the gotcha applies only to `& { … }` scriptblocks, which the repo does not use.
- **Settings corrupt-config truncation / write-only-or-read-only persisted fields** — parse-into-throwaway-then-quarantine on corruption; full load/save symmetry audited across all persisted fields.

---

## 16. Engineering strengths

- **Object ownership & lifetime:** explicit ownership bits, owner-last destruction of a shared decoded texture across many panels, idempotent liveness-gated teardown — applied consistently across `PlaybackSession`, `CinemaScreenGroup`, `ShowcaseSurfaceGroup`.
- **Transactional replacement:** `UpdateGeometry` and `StagedFileReplacement` (and Settings' Begin/Commit/Cancel screen-edit transaction) preserve valid old state until the replacement fully succeeds.
- **Decoder pipeline:** rigorous FFmpeg send/receive/EOF handling, ABI-isolated dual backends, bounded main-thread stop with off-thread retirement, textbook mailbox synchronization.
- **Dependency isolation & provenance:** hash-pinned verify-before-use across every fetched artifact, machine-checked FFmpeg LGPL/HEVC/symbol isolation, byte-for-byte reproducible yt-dlp rebuild, QuickJS sandbox.
- **Filesystem removal safety:** receipt/hash-driven with a preserve-on-doubt bias and layered path guards.
- **In-code documentation:** accurate "why" comments on the genuinely non-obvious Quest/IL2CPP hazards.
- **Concrete-claim doc accuracy:** the technical numbers in README/docs match the implementation.

---

## 17. Quality ratings (1–10)

| Category | Score | Note |
|---|---|---|
| Architecture | 8 | Clear subsystem/ownership boundaries; a few very large files (below). |
| Object ownership / lifetimes | 9 | Exemplary; owner-last, idempotent, liveness-gated. |
| Threading / concurrency | 8 | Invariant holds; one narrow gated race (L10). |
| Error handling | 8 | Coherent policy; edge gaps M2/L3. |
| Error-handling consistency | 8 | Four-way policy applied consistently; minor exceptions. |
| Resource cleanup | 9 | RAII + idempotent Destroy across all paths. |
| Decoder / media pipeline | 9 | Careful ownership, fallback, and synchronization. |
| Unity / render lifecycle | 9 | Transactional, fake-null-safe. |
| Downloader / runtime integration | 8 | Strong isolation/rollback; narrow race L10. |
| Filesystem safety | 9 | Preserve-on-doubt, validated paths. |
| Build / deploy / remove tooling | 7 | Dead receipt-deploy path (M4), partial hash-verify (L2), dead/wrong helpers (L8/L9/L13), EOL churn (L7). |
| Testing | 6 | Strong behavioral core; large brittle substring layer; key invariants untested (M5). |
| Documentation / comments | 8 | Excellent in-code; a stale comment or two. |
| README / GitHub docs accuracy | 7 | Concrete claims accurate; internal-docs-as-authoritative (M3), table wording (L12). |
| Maintainability | 7 | `#if 0` feature blocks (M1), 170–212 KB files, dead scripts. |
| Overall professional polish | 8 | Runtime core is professional-grade; periphery needs a cleanup pass. |

*Categories below 8 are explained in their rows and the corresponding findings.* On file size specifically: `VideoLibraryMenu.cpp` (~212 KB), `DownloadManager.cpp` (~195 KB), and `SettingsMenu.cpp` (~169 KB) are not defects in themselves and their ownership is clear, but they are approaching the point where decomposition would reduce regression risk — a P3 maintainability item, reported per the review's "size only when it creates concrete risk" rule.

---

## 18. Beta-readiness verdict

**READY FOR FIRST-ROUND BETA.**

- **Code-quality readiness:** met. No Critical or High confirmed defect; ownership, threading, decoder, render, hooks, downloader isolation, and removal safety are all sound. The Medium/Low items are edge-triggered (M2, L3, L4, L10, L11), tooling/process (M3, M4, L2, L7–L9, L13), feature-state (M1), or test/doc hygiene (M5, L1, L12) — none blocks a first beta.
- **Real-world field maturity:** not yet established. The hardware/MediaCodec decode path, backend fallback, and the whole render stack need on-device testing across the mod ecosystem (Chroma/Noodle/Replay, repeated menu entry, map restart/exit, decoder fallback). The disabled bloom feature (M1) should be resolved so the shipped feature set is unambiguous before wider beta.

---

## 19. Prioritized remediation plan

**P0 — before beta (cheap, removes ambiguity/silent degradation):**
- M1: decide the bloom/`colorBlending` feature's disposition and make code + parser + docs agree (finish-and-enable, or remove the `#if 0` blocks and the parse-only fields).
- M2: reset `dialogVisible_` in `ResetCircuitBreaker` (one line) so the user-visible error channel can't stay muted.

**P1 — during early beta:**
- L2: hash-verify `libraryFiles`/`fileCopies` in `copy.ps1`.
- L1: restore the `menuScreenPreviewEnabled` fallback read.
- L3: make `ReportInternal`/`ReportUserVisible` non-throwing so `Guard` truly can't `terminate`.
- L4: bound (or explicitly document) the hardware `Open()` main-thread cost.

**P2 — polish before stable:**
- M5: behavioral tests for cancellation, `VideoLibrary` backup rotation/recovery, and updater rollback.
- M4: wire the receipt-deploy path into `copy.ps1` (or re-label it test-only and fix the module header).
- M3: move internal planning/spec/prompt docs out of the public `docs/` index; curate `docs/README.md`.
- L12 (DEPENDENCIES wording), L9 (bat version string), L8/L13 (retire or wire the legacy ADB/tombstone helpers), L7 (add `.gitattributes` and renormalize), L10 (lock the diagnostic-throttle reset), L11 (zero-frame-EOF readiness).

**P3 — future maintainability:**
- L6: migrate `SelectionVideoToggle` scene-root UI to `UnityW`.
- L5: harden the retirement reaper against a wedged `Close()`.
- Rendering F1: root or `SafePtr` the cross-frame showcase `ArrayW` buffers (or document the GC assumption).
- Consider decomposing the three largest `.cpp` files if they begin to impede maintenance.

*Grouping note:* M4 + L2 + L13 + L8 are one "deploy/remove tooling cleanup" workstream; M3 + L12 + the docs-index trim are one "documentation curation" pass; M5 + the missing behavioral tests are one "test-hardening" pass.
