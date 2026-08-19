# Claude Code Task — Full Professional Code Review of Big Screen

## Objective

Perform a **complete read-only code review** of the current Big Screen repository.

The goal is to determine whether the codebase is:

- technically sound;
- stable enough for continued alpha/beta testing;
- professionally structured;
- consistent in its engineering practices;
- safe with respect to object lifetime, threading, synchronization, and cleanup;
- handling failures consistently and correctly;
- accurately and sufficiently documented;
- presenting accurate technical information in the README and supporting GitHub documentation.

This is a **review only**.

**Do not modify any code, documentation, scripts, tests, or configuration files.**

Do not automatically fix findings.

The output should be a detailed review that can later be compared against a separate review from another model before any changes are authorized.

---

# 1. Review philosophy

This review should focus on **realistic defects and meaningful engineering weaknesses**.

Do **not** manufacture hypothetical problems merely because something is theoretically possible.

A finding should only be reported when:

1. the problematic condition can actually occur with the current architecture/code paths; or
2. there is a realistic external condition that can produce it; or
3. the code violates an important invariant and there is no reliable mechanism preventing the invalid condition.

Do not report extremely rare theoretical situations unless:

- the consequence is catastrophic;
- the triggering condition is realistically possible;
- and the code currently provides no meaningful protection.

For every potential issue, trace the actual call path/state/lifetime and determine whether the condition can really happen before reporting it.

Example of what **not** to do:

> “Object X could be null.”

when the code always constructs X before the only reachable call path and teardown prevents access afterward.

Example of what **should** be reported:

> “Worker B can retain this pointer after owner A begins destruction because Stop() signals the worker but destroys the referenced resource before joining the thread.”

The review must distinguish between:

```text
actual defect
realistic risk
maintainability concern
intentional design tradeoff
purely theoretical concern
```

Do not inflate the review by reporting the last category as a defect.

---

# 2. Inspect repository state first

Before reviewing:

```text
git status
git diff
git diff --stat
```

The working tree may contain valid in-progress changes.

Review the **current working tree**, including those changes.

Do not:

- revert them;
- overwrite them;
- assume uncommitted code is accidental.

Record the branch/commit and whether the tree is dirty so the review identifies exactly what was examined.

---

# 3. Review the entire first-party implementation

Review all relevant first-party code, including where applicable:

```text
src/
include/
tests/
scripts/
CMakeLists.txt
*.cmake
*.ps1
*.bat
*.sh
*.py
.github/
mod.template.json
other project-owned build/package/configuration files
```

Do not waste review effort on vendored/generated third-party implementation unless Big Screen's integration with it is relevant.

For third-party code, review:

- how Big Screen calls it;
- ownership expectations;
- API assumptions;
- error handling;
- version/configuration assumptions.

Do not review the internal quality of FFmpeg, CPython, QuickJS, beatsaber-hook, BSML, etc.

---

# 4. Documentation scope

Review documentation that forms part of the public/professional Big Screen repository.

At minimum review:

```text
README.md
CONTRIBUTING.md
SECURITY.md
PROVENANCE.md
LICENSE-related project documentation
THIRD_PARTY_NOTICES.md
architecture documentation
developer/build documentation
user-facing documentation
documents directly linked from README.md
documents used as GitHub-facing technical references
```

Determine the actual set by following links from the README and repository documentation index.

## Do not review irrelevant internal planning material

Ignore files whose purpose is clearly:

- Codex prompts;
- Claude prompts;
- implementation prompts;
- TODO lists;
- temporary planning notes;
- brainstorming;
- design-conversation transcripts;
- historical AI instructions;
- one-off task specifications.

These may remain in `docs/` and should not be treated as public technical documentation merely because they exist there.

If such a file is directly linked from the README or otherwise presented as official documentation, flag that fact instead of reviewing its prose as authoritative documentation.

---

# 5. Architecture review

Determine whether the current architecture has clear and appropriate responsibilities.

Review:

- subsystem boundaries;
- ownership boundaries;
- initialization order;
- shutdown order;
- gameplay lifecycle;
- menu lifecycle;
- downloader lifecycle;
- decoder lifecycle;
- Unity object ownership;
- shared resources;
- global/singleton state;
- configuration ownership;
- external dependency ownership.

Look for cases where one class/module has accumulated too many unrelated responsibilities.

Do not report file/class size alone as a defect.

Report it only when size/responsibility concentration is creating concrete risks such as:

- unclear ownership;
- difficult cleanup;
- duplicate state;
- lock complexity;
- error-handling inconsistency;
- high regression risk.

---

# 6. Object lifetime and ownership

Perform a detailed lifetime review.

For important objects determine:

```text
who creates it
who owns it
who may reference it
which thread may access it
when destruction begins
when references become invalid
how shutdown is coordinated
```

Look specifically for:

- dangling pointers;
- use-after-free;
- destruction before worker completion;
- callbacks outliving owners;
- Unity objects retained across invalid scenes;
- stale IL2CPP/Unity references;
- raw pointers with ambiguous ownership;
- shared resource destruction ordering;
- static/global teardown problems;
- objects destroyed from the wrong thread;
- duplicate ownership;
- double destruction;
- cleanup paths that depend on normal control flow.

Check RAII usage and whether resources consistently release themselves on all paths.

Pay particular attention to resources such as:

- FFmpeg objects;
- decoder contexts;
- frames;
- packets;
- textures;
- meshes;
- materials;
- GameObjects;
- JNI/Android resources;
- CPython state;
- QuickJS state;
- threads;
- file handles;
- download state;
- callbacks/hooks.

---

# 7. Threading review

Perform a full threading/concurrency review.

Identify every thread or worker used by Big Screen.

For each determine:

```text
creator
owner
lifetime
shutdown mechanism
join/detach behavior
data accessed
locks used
callbacks produced
whether Unity/game objects are touched
```

Check for:

- detached threads whose lifetime is not bounded;
- workers outliving owning objects;
- forgotten joins;
- race conditions;
- cross-thread Unity access;
- cross-thread IL2CPP access;
- non-atomic shared flags;
- condition-variable misuse;
- lost wakeups;
- unsafe shutdown ordering;
- callbacks occurring after teardown;
- concurrent container access;
- race-prone shared state.

One of Big Screen's core invariants should be that Unity/Beat Saber game objects are manipulated only from the appropriate game thread.

Verify that this is true throughout the repository.

Do not merely search for `std::thread`.

Trace where callbacks and state changes actually execute.

---

# 8. Locking and synchronization

Map the important mutexes/locks and how they interact.

Look for:

- inconsistent lock ordering;
- possible lock inversion;
- recursive entry;
- holding locks while performing slow I/O;
- holding subsystem locks while calling external code;
- holding locks while invoking callbacks;
- waiting for threads while holding locks needed by those threads;
- unnecessary coarse locking;
- state accessed without required locking.

Pay special attention to interactions between:

```text
DownloadManager
VideoLibrary
PlaybackSession
FrameDecoder
UI/menu state
Settings
ErrorManager
diagnostic/session logging
background workers
```

If proposing a deadlock finding, demonstrate the actual lock cycle.

Do not report:

> “A and B both use mutexes, therefore deadlock is possible.”

Show the reachable ordering:

```text
Thread 1:
Lock A → attempts B

Thread 2:
Lock B → attempts A
```

or do not report it as a defect.

---

# 9. Error handling

Perform a repository-wide review of error handling.

The objective is not only to find missing error checks.

Also determine whether Big Screen handles errors **consistently and professionally**.

Look for:

- ignored return values;
- unchecked pointers;
- unchecked FFmpeg return codes;
- unchecked file operations;
- exceptions escaping inappropriate boundaries;
- native calls that may fail without handling;
- errors logged but state left inconsistent;
- cleanup omitted on failure;
- partially initialized objects published as valid;
- errors that unnecessarily terminate gameplay;
- fatal/nonfatal errors treated inconsistently.

---

# 10. Error-handling consistency

This is particularly important.

Determine whether equivalent classes of errors are handled in equivalent ways.

For example, check whether different subsystems inconsistently use:

```text
Paper logging only
ErrorManager
return false
exceptions
silent fallback
status enums
error codes
callbacks
UI messages
```

Inconsistency should be reported when it makes the codebase look uncoordinated or produces materially different user/debugging behavior without a good reason.

Do **not** demand that every failure use exactly one mechanism.

Different layers may legitimately need different mechanisms.

Instead determine whether there is a clear policy such as:

```text
internal recoverable error
→ return status/result

user-visible persistent failure
→ ErrorManager + contextual logging

optional feature failure
→ log + disable feature + continue

fatal initialization failure
→ clean abort of Big Screen feature without crashing Beat Saber
```

If no consistent policy exists, identify the specific inconsistencies.

---

# 11. Error recovery and state integrity

For realistic failure points ask:

> If this operation fails halfway through, what state remains?

Review transactional behavior around:

- geometry replacement;
- video assignment;
- decoder initialization;
- seek/restart;
- downloader completion;
- library updates;
- configuration persistence;
- runtime dependency setup;
- file replacement;
- menu/UI initialization.

Look for patterns where code does:

```text
destroy old valid state
↓
attempt new state
↓
failure
↓
nothing valid remains
```

when it could instead preserve the old valid state until replacement succeeds.

Check that cleanup after partial initialization is safe and idempotent where appropriate.

---

# 12. Native crash exposure

Identify locations where Big Screen could realistically produce:

- SIGSEGV;
- use-after-free;
- null dereference;
- invalid Unity/IL2CPP pointer access;
- memory corruption;
- double-free;
- stack corruption;
- invalid cross-thread engine call.

Do not suggest `try/catch` for faults that C++ exceptions cannot catch.

Distinguish:

```text
recoverable C++/library error
```

from:

```text
native memory fault
```

and evaluate whether the latter can be prevented through stronger ownership/state checks.

---

# 13. Hook review

Inspect all Beat Saber/Unity hooks.

For each determine:

- why the hook exists;
- when it can run;
- whether referenced objects are valid at that point;
- whether the hook can run during teardown;
- whether the original method is called correctly;
- whether hooks assume another mod/dependency is initialized;
- whether version changes would make the hook fragile;
- what happens if the expected game state is absent.

Look for realistic hook fragility, not theoretical game-version changes that cannot currently occur.

Where hooks depend strongly on Beat Saber implementation details, ensure:

- the dependency is documented;
- failure is detectable where practical;
- the code fails safely.

---

# 14. Decoder and media pipeline

Review the complete video pipeline.

Trace:

```text
video selection
↓
open
↓
demux
↓
decode
↓
frame selection
↓
conversion
↓
mailbox/publication
↓
Unity upload/presentation
↓
seek/restart/pause
↓
shutdown
```

Check:

- frame ownership;
- packet/frame cleanup;
- FFmpeg error checking;
- timestamp calculations;
- seek state;
- race conditions;
- mailbox synchronization;
- dropped/late frame behavior;
- decoder restart;
- fallback between FFmpeg backends;
- MediaCodec interaction;
- shutdown ordering.

Pay special attention to assumptions that differ between:

- software decoding;
- MediaCodec decoding;
- FFmpeg versions/backends.

---

# 15. Screen/rendering lifecycle

Review:

```text
ScreenSurface
screen groups
multi-screen presentation
geometry creation
curvature
UV transforms
materials
textures
fracture/shatter
deformation/morph effects
temporary presentation state
```

Check:

- mesh/material/texture ownership;
- old/new geometry transition safety;
- resource leaks;
- allocation patterns;
- teardown;
- clone ownership;
- shared texture lifetime;
- screen count/pooling;
- failure during partial screen creation.

Check whether the code remains safe when:

- leaving a song early;
- restarting;
- failing;
- switching maps;
- entering/leaving menus repeatedly;
- Replay changes timing;
- a showcase/movement sequence is interrupted.

---

# 16. Downloader and embedded runtimes

Review:

- DownloadManager;
- yt-dlp integration;
- CPython integration;
- QuickJS integration;
- FFmpeg subprocess/library interaction;
- thumbnail/download workers;
- cancellation;
- status communication;
- update mechanisms.

Look for:

- worker lifetime issues;
- unsafe Python/GIL use;
- callbacks after teardown;
- stale state across downloads;
- partial files;
- unsafe cancellation;
- file ownership ambiguity;
- excessive blocking;
- unsafe filesystem assumptions;
- unhandled failures.

Review the newly added diagnostic logging integration if present.

Verify that logging cannot interfere with download behavior.

---

# 17. Filesystem/storage safety

Review every area that writes, deletes, moves, copies, or replaces files.

Look for realistic risks involving:

- broad recursive deletion;
- user videos;
- library data;
- settings;
- runtime dependencies;
- source-install receipts;
- partial installs;
- support logs;
- cache directories.

Ensure paths are validated before destructive operations.

Look specifically for:

```text
empty path
unexpected root
.. traversal
absolute-path injection
symlink behavior where relevant
wildcards
wrong ModData/package directory
```

Do not invent hostile-input scenarios for paths users cannot control.

Focus on inputs that can actually originate from:

- downloaded metadata;
- map configs;
- user-selected files;
- remote package data;
- script parameters.

---

# 18. Build/deploy/remove scripts

Review the developer experience and safety of:

- bootstrap scripts;
- build scripts;
- deploy scripts;
- log collection;
- source removal;
- ownership receipts;
- MBF detection;
- ADB behavior.

Verify:

- failures propagate through exit codes;
- partial operations are handled;
- paths are portable;
- scripts do not depend on developer-specific directories;
- destructive operations require appropriate confirmation;
- shared dependencies are not removed;
- user media is preserved;
- source/MBF ownership logic is correct;
- repeated source deployment remains safe.

Look for PowerShell/batch quoting issues and assumptions that realistically break on Windows paths containing spaces.

---

# 19. Performance review

Do not attempt speculative micro-optimization.

Look for realistic hot-path problems such as:

- allocation every Unity frame;
- repeated mesh rebuilding;
- unnecessary full-frame copies;
- excessive lock contention;
- blocking filesystem/network work on the game thread;
- expensive logging in per-frame paths;
- repeated lookups that can reasonably be cached;
- work multiplied unnecessarily by screen count.

Distinguish:

```text
actual likely performance problem
```

from:

```text
possible optimization that is not currently needed
```

Big Screen already has real Quest performance measurements, so do not report hypothetical optimization opportunities as defects unless they are plausibly significant.

---

# 20. Memory/resource leak review

Look for resources that can accumulate across:

- songs;
- previews;
- downloads;
- menu entry;
- screen creation;
- repeated configuration;
- restart;
- Replay;
- failure paths.

Check:

- native heap;
- Unity objects;
- textures;
- meshes;
- materials;
- FFmpeg allocations;
- Python objects/references;
- Java/JNI references;
- threads;
- open files;
- downloaded temporary files.

For suspected leaks, identify the specific allocation path and missing release path.

---

# 21. API/interface consistency

Review internal and public interfaces for:

- naming consistency;
- ownership semantics;
- result/error semantics;
- const correctness where relevant;
- duplicated responsibilities;
- ambiguous bool return values;
- undocumented preconditions;
- inconsistent parameter ordering/types.

Do not demand stylistic rewrites merely based on preference.

Report inconsistencies when they harm understanding, safety, or maintainability.

---

# 22. Comments and code documentation

Review comments throughout first-party source.

The objective is a **professionally documented codebase**, not commenting every obvious line.

Ensure important code has comments explaining:

- non-obvious algorithms;
- ownership;
- threading requirements;
- lifetime invariants;
- synchronization decisions;
- unusual Beat Saber/Quest behavior;
- fragile hooks;
- Android workarounds;
- FFmpeg/MediaCodec quirks;
- failure/recovery policy;
- reasons behind apparently unusual implementation choices.

Particularly important functions/classes should make it possible for another experienced engineer to understand **why the implementation is written this way**.

---

# 23. Comment accuracy

Every existing meaningful comment should be treated as potentially stale until verified.

Look for comments that:

- describe behavior the code no longer performs;
- mention obsolete architecture;
- give wrong thread/lifetime assumptions;
- claim an error cannot occur when it can;
- claim something is thread-safe when it isn't;
- mention flags/settings that changed;
- refer to old filenames/paths;
- explain a workaround that is no longer used;
- reference `-Werror`, compiler settings, or dependencies inaccurately.

A misleading comment is worse than no comment.

Report exact file/line and the current behavior that contradicts it.

---

# 24. Missing comments

Identify important areas that are insufficiently explained.

Do **not** report every short/simple method for lacking comments.

Focus on places where understanding the implementation requires knowledge that is not obvious from the code.

Examples:

```text
why this lock ordering is required
why the worker is joined before resource destruction
why a Unity operation must happen on this thread
why a hook uses this particular lifecycle point
why an FFmpeg backend is isolated
why a state transition cannot occur immediately
```

The standard should be:

> Could another experienced programmer maintain this safely without having to rediscover the hidden invariants?

---

# 25. README accuracy

Verify every technical claim in `README.md` against the current implementation.

Examples include:

- supported Beat Saber version;
- Quest models;
- decoder behavior;
- MediaCodec/software decode claims;
- video formats;
- video resolution;
- frame synchronization;
- performance diagnostics;
- download functionality;
- Video Library behavior;
- Cinema compatibility;
- screen effects;
- Chroma/Noodle behavior;
- Replay behavior;
- build/deploy process;
- dependencies;
- storage locations;
- logging/support workflow;
- license;
- development status.

Do not assume documentation is correct because it was recently edited.

Trace claims into code/build/package behavior.

---

# 26. Supporting GitHub documentation

For every official document linked from the README or used as GitHub-facing documentation:

Verify:

- paths;
- filenames;
- commands;
- architecture descriptions;
- dependency versions;
- build steps;
- install steps;
- debugging instructions;
- license statements;
- API/behavior claims.

Report stale or contradictory information.

Do not review internal prompt/planning documents unless they are publicly linked as documentation.

---

# 27. Documentation consistency

Check whether multiple official documents contradict one another.

Examples:

```text
README says A
ARCHITECTURE says B
actual implementation does C
```

When this occurs, identify all relevant sources and state what the code actually does.

---

# 28. Tests

Review test quality and coverage.

Determine whether important invariants are actually tested.

Look for:

- missing tests around realistic failures;
- tests that assert implementation text rather than behavior unnecessarily;
- brittle source-string tests;
- tests that cannot fail even when behavior is broken;
- tests that test mocks rather than actual logic;
- important lifecycle/state behavior with no tests.

Also identify strong areas of the test suite.

Do not recommend tests for every trivial getter/setter.

Focus on high-value behavior:

- ownership;
- rollback;
- shutdown;
- parser/state machine behavior;
- decoder timing;
- failure recovery;
- deployment/removal safety;
- download cancellation;
- logging redaction;
- choreography boundaries.

---

# 29. CI/repository invariants

Review:

- GitHub Actions;
- build reproducibility;
- pinned actions/dependencies;
- repository invariant tests;
- packaging checks;
- license checks;
- source/deploy symmetry.

Identify realistic weaknesses.

Do not recommend adding third-party CI services unless necessary.

---

# 30. Security review

Perform a practical security review appropriate for a Quest mod.

Focus on realistic attack/input surfaces:

- remote downloads;
- yt-dlp;
- release/update manifests;
- ZIP/archive extraction;
- map-provided config;
- JSON parsing;
- filenames/paths;
- downloaded native/runtime assets;
- shell/process invocation;
- URL handling.

Look for:

- path traversal;
- command injection;
- unsafe archive extraction;
- loading unverified executable code;
- token leakage;
- log leakage;
- overly broad file deletion.

Do not invent adversarial scenarios for completely internal constants that users/remotes cannot influence.

---

# 31. Professional consistency

Look at the codebase as if it were being reviewed for a professional software team.

Check for inconsistent patterns such as:

```text
five different approaches to errors
multiple competing logging styles
different ownership conventions
different naming conventions for equivalent operations
duplicate helper logic
one subsystem using safe transactional replacement while another destroys-first
```

The goal is not stylistic uniformity for its own sake.

The goal is that the repository feels deliberately engineered rather than incrementally patched.

---

# 32. Do not suggest rewrites without justification

Do not recommend:

- rewriting in Rust;
- rewriting C++ subsystems;
- replacing libraries;
- switching frameworks;
- massive architectural refactors

unless a real defect cannot reasonably be corrected within the current architecture.

A mature-looking review should prefer the **smallest correct fix**.

---

# 33. Validate every finding before reporting it

Before adding a finding:

1. Identify the exact code.
2. Trace its callers.
3. Trace relevant ownership/lifetime.
4. Check guards/invariants.
5. Check whether another subsystem already prevents the condition.
6. Check tests.
7. Determine realistic trigger.
8. Determine actual consequence.

If the condition cannot actually occur, **do not report it as a defect**.

If uncertain, label it:

```text
NEEDS VERIFICATION
```

rather than presenting speculation as fact.

---

# 34. Severity definitions

Use:

## Critical

Realistic potential for:

- widespread crashes;
- data loss;
- corruption;
- arbitrary code execution;
- catastrophic user impact.

Requires correction before wider beta/release.

## High

Realistic defect capable of:

- native crash;
- serious resource/lifetime failure;
- deadlock;
- broken download/storage;
- incorrect destructive behavior;
- major user-visible malfunction.

Should be fixed before wider beta where practical.

## Medium

Realistic issue affecting:

- reliability;
- maintainability;
- consistency;
- diagnostics;
- unusual but plausible workflows.

Should be addressed, but not necessarily a beta blocker.

## Low

Real but minor:

- documentation mismatch;
- stale comment;
- small inconsistency;
- minor maintainability problem.

Do not use Low as a bucket for speculative observations.

## Informational

Not a defect.

Use for:

- architectural strengths;
- future considerations;
- intentional tradeoffs.

---

# 35. Confidence rating

Every reported defect should include:

```text
Confidence: High / Medium / Low
```

Prefer reporting **fewer high-confidence findings** over dozens of questionable ones.

Low-confidence findings should normally be excluded unless the potential consequence is severe and verification is difficult.

---

# 36. Required evidence for each finding

Each finding must contain:

```text
Severity
Confidence
File(s)
Function/class
Relevant line(s)
Category
What is wrong
Why it can actually happen
Realistic trigger/path
Likely consequence
Existing protections examined
Recommended smallest correction
Tests that should verify the correction
```

Do not provide generic advice detached from specific code.

---

# 37. False-positive section

Include a section:

# Potential Issues Investigated and Rejected

This is important.

List meaningful-looking concerns that were investigated but **not reported as defects** because existing code prevents them.

Example:

```text
Investigated possible decoder worker use-after-free.

Rejected because Stop() sets the stop state under the predicate mutex,
notifies the worker, joins it, and only then destroys FFmpeg resources.
```

This demonstrates that the review traced the architecture instead of merely pattern-matching suspicious code.

Do not list trivial things here.

Include only concerns another reviewer might reasonably raise.

---

# 38. Strengths

Include a section describing areas that are particularly well implemented.

Examples might include:

- lifecycle management;
- transactional replacement;
- testing;
- build/deploy safety;
- dependency isolation;
- diagnostics;
- documentation.

Only state strengths actually supported by the code.

The purpose is to establish an accurate quality assessment, not simply find criticism.

---

# 39. Overall quality assessment

Provide ratings from 1–10 for:

```text
Architecture
Object ownership/lifetimes
Threading/concurrency
Error handling
Error-handling consistency
Resource cleanup
Decoder/media pipeline
Unity/render lifecycle
Downloader/runtime integration
Filesystem safety
Build/deploy/remove tooling
Testing
Documentation/comments
README/GitHub documentation accuracy
Maintainability
Overall professional polish
```

Explain any category below 8.

Do not inflate scores merely because the software works.

Do not artificially lower scores to make the review look rigorous.

---

# 40. Beta readiness

Finish with one of:

```text
READY FOR FIRST-ROUND BETA
READY FOR BETA AFTER HIGH-SEVERITY FIXES
NOT READY FOR BETA
```

Explain why.

Separate:

```text
code-quality readiness
```

from:

```text
real-world field maturity
```

A new mod may have excellent code but still need beta testing for ecosystem/mod-stack interactions.

---

# 41. Prioritized remediation plan

If findings exist, provide a recommended order:

```text
P0 — fix before beta
P1 — fix during early beta
P2 — polish before stable release
P3 — future maintainability
```

Group related findings where one architectural correction solves several symptoms.

Do not propose implementation yet.

---

# 42. Review output format

Produce one comprehensive review with:

1. **Executive summary**
2. **Scope/repository state**
3. **Overall quality assessment**
4. **Critical findings**
5. **High findings**
6. **Medium findings**
7. **Low findings**
8. **Threading/lifetime assessment**
9. **Error-handling consistency assessment**
10. **Comments/documentation assessment**
11. **README/GitHub docs accuracy**
12. **Testing/CI assessment**
13. **Build/deploy/remove assessment**
14. **Security assessment**
15. **Potential issues investigated and rejected**
16. **Engineering strengths**
17. **Quality ratings**
18. **Beta-readiness verdict**
19. **Prioritized remediation plan**

If no findings exist in a severity category, explicitly say:

```text
No confirmed findings.
```

Do not invent one merely to populate the section.

---

# 43. Important review rules

- **Do not modify anything.**
- Do not fix findings.
- Do not run automatic formatters.
- Do not rewrite documentation.
- Do not delete files.
- Do not refactor.
- Do not treat prompt/TODO/planning files as public documentation.
- Do not report theoretical impossibilities as bugs.
- Do not reward complexity with criticism simply because simpler code could theoretically exist.
- Do not recommend changing code that is already safe, clear, and correct.
- Do not treat every raw pointer as a defect.
- Do not treat every mutex as a potential deadlock.
- Do not treat every unchecked hypothetical external state as realistically reachable.
- Trace actual execution paths.
- Prefer evidence over suspicion.
- Prefer high-confidence findings over quantity.
- Verify comments against implementation.
- Verify public documentation against implementation.
- Judge the repository at a professional engineering standard even though Big Screen is currently alpha.

---

# Primary review question

The final review should answer:

> **If an experienced software engineering team inherited this repository today, what real defects or inconsistencies would they want corrected before considering the codebase professionally engineered and ready for broader beta use—and which apparent concerns are already safely handled by the existing architecture?**