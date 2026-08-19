# Codex Follow-Up Discussion — Resolve Logging Writer Design + Reproduce Source-Deploy/MBF Conflict

## Objective

This is a **follow-up analysis task only**.

Do not implement the logging system, uninstall workflow, or deployment changes yet.

The previous analysis was generally good, but there are two areas that need to be resolved before implementation:

1. Whether `DiagnosticSessionLogger` should really use a **background writer thread + bounded queue**, or whether a simpler synchronized append model would be safer and more appropriate for crash reconstruction.
2. The exact cause of the observed conflict where a **source-deployed Big Screen install remains active or interferes after installing Big Screen through ModsBeforeFriday (MBF)**.

The goal of this follow-up is to produce evidence-backed recommendations for those two points.

Do not change unrelated code.

---

# 1. Logging writer architecture — compare the actual options

The previous recommendation was:

```text
DiagnosticSessionLogger
→ bounded queue
→ one background writer thread
→ JSON Lines output
```

Before implementing that, compare it against a simpler model.

Evaluate at least these approaches:

## Option A — synchronous append

```text
event
↓
serialize JSONL line
↓
lock
↓
append
↓
flush
↓
unlock
```

## Option B — bounded queue + background writer

```text
event
↓
serialize/enqueue
↓
writer thread
↓
append/flush
```

## Option C — hybrid

For example:

```text
normal UI events
→ immediate synchronized append

high-volume yt-dlp messages
→ buffered/queued writer
```

or another hybrid if the current implementation suggests something better.

---

# 2. Base the logging decision on the actual expected event rate

Estimate the real expected logging volume after the previously recommended throttling/coalescing.

Consider:

- menu entry/exit;
- tab navigation;
- toggle changes;
- dropdowns;
- dialog actions;
- video selection;
- preview start/stop/seek;
- assignment changes;
- slider start/final values only;
- download stages;
- download progress at approximately 5% boundaries;
- yt-dlp operational messages;
- error correlation events.

Do not assume logging is high-volume merely because it could theoretically receive many callbacks.

Estimate approximate events per second during:

- normal menu navigation;
- rapid slider interaction;
- active download;
- heavy yt-dlp output.

Then determine whether a background writer is actually necessary.

---

# 3. Crash-reconstruction priority

The primary purpose of these logs is:

> reconstruct exactly what the user did immediately before a failure.

That means the architecture should favor preserving the **most recent actions before a hard Beat Saber crash**.

Compare the failure behavior of the approaches.

For example:

## Synchronous append

If this occurs:

```text
user changes setting
↓
line written + flushed
↓
Beat Saber SIGSEGV 5 ms later
```

how likely is the setting event to exist on disk?

## Queue-based writer

If this occurs:

```text
user changes setting
↓
event placed in queue
↓
Beat Saber SIGSEGV before writer drains it
```

how much data might be lost?

Quantify or estimate:

- maximum queued events;
- maximum time an event can remain unwritten;
- what happens during native process termination;
- whether destructors or normal shutdown handlers can be relied upon after SIGSEGV.

Do not assume graceful shutdown.

---

# 4. Quest storage/write cost

Inspect where these session logs will live and what filesystem/storage path is involved.

Determine whether:

```text
append one small JSONL line
+
flush
```

for relatively low-frequency events creates meaningful blocking on the Unity/game thread.

If possible, use existing Quest/Android filesystem behavior or measurements from the project rather than assumptions.

Estimate the likely cost of:

- opening the file once per session;
- appending ~100–500 byte lines;
- `flush()` after meaningful events;
- any actual `fsync()` behavior if relevant.

Distinguish:

```text
C/C++ stream flush
```

from:

```text
forcing storage media synchronization
```

Do not recommend `fsync()` on every event unless there is a compelling reason.

---

# 5. Deadlock/reentrancy analysis

For synchronous logging, determine whether there are code paths where logging while holding another Big Screen lock could introduce:

- lock-order inversion;
- recursive logger entry;
- deadlock;
- UI stalls.

Inspect the current locking patterns in:

- settings;
- downloader;
- error manager;
- video library;
- menu coordination;
- yt-dlp callbacks.

If synchronous append is chosen, define the rule for call sites.

For example:

> Collect/format event data while holding the subsystem lock if necessary, release subsystem lock, then submit the diagnostic event.

or whatever is safest.

The diagnostic logger must never become part of critical subsystem lock ordering.

---

# 6. Background writer lifecycle analysis

If retaining a background writer, explicitly address:

- who owns the thread;
- when it starts;
- when it stops;
- how it is joined;
- how shutdown drains the queue;
- what happens if the session closes while events are still queued;
- what happens if the writer itself errors;
- what happens if a hard process crash occurs;
- whether there is one writer globally or one writer per session;
- whether menu and download sessions can overlap.

The previous design should not be accepted just because “background logging is common.”

Demonstrate why the extra worker is justified.

---

# 7. yt-dlp may need different treatment

yt-dlp is the one likely source of relatively high-volume diagnostic messages.

Determine whether it should use:

- the same immediate writer;
- a bounded queue;
- line aggregation;
- severity filtering;
- its own buffering;
- another mechanism.

The goal is to preserve useful yt-dlp diagnostics without turning the main interaction logger into a throughput-oriented logging system unnecessarily.

Assess whether yt-dlp output can be timestamped and merged chronologically with synchronous UI events even if it is buffered separately.

---

# 8. Recommendation for logging

At the end of this section choose one:

```text
SYNCHRONOUS APPEND RECOMMENDED
BACKGROUND WRITER RECOMMENDED
HYBRID RECOMMENDED
```

Explain why specifically for **Big Screen on Quest**, not generic software.

Include:

- crash data preservation;
- expected performance;
- implementation complexity;
- thread/lifetime complexity;
- risk of missing final events;
- failure isolation.

Do not implement it yet.

---

# 9. Reproduce the source-deploy → MBF install conflict

The previous analysis correctly established that direct source deployment does not create MBF package ownership metadata.

However, it did **not** establish why the source-deployed version appears to remain active after a later MBF install.

This needs to be reproduced rather than inferred.

Perform a controlled test if the connected Quest/environment allows it.

If the test cannot safely be performed automatically, specify the exact commands and observations needed and stop before making destructive changes.

---

# 10. Controlled test procedure

Use two clearly distinguishable Big Screen builds if practical.

For example:

```text
Build A
source-deployed
version/log marker: SOURCE-A

Build B
QMOD/MBF-installed
version/log marker: MBF-B
```

If changing displayed version strings would modify the repo unnecessarily, distinguish them using:

- file hashes;
- build timestamps;
- known binary hashes;
- existing version differences;
- another non-invasive marker.

The test should establish exactly which binary Scotland2 loads.

---

# 11. Baseline source deployment

After source deploying Big Screen:

Record:

- SHA-256 of every Big Screen-owned installed native binary;
- exact Quest path;
- whether it is under:
  - `early_mods`
  - `mods`
  - `libs`
  - app-private Scotland2 copy/cache
  - Big Screen Runtime
- generated/installed QMOD metadata presence or absence;
- MBF package metadata presence or absence.

Start Beat Saber and verify which version/hash is actually running using logs.

Save this state.

---

# 12. Install through MBF

Then install a known Big Screen QMOD through MBF.

Afterward, without running source deployment again, record:

- SHA-256 of the same installed paths;
- any new QMOD/MBF package metadata;
- contents of both Scotland2 load phases;
- duplicate `libbigscreen.so` files;
- private/runtime native libraries;
- timestamps;
- package version MBF reports.

Determine whether MBF actually replaced:

```text
libbigscreen.so
```

at the active load location.

---

# 13. Launch after MBF install

Start Beat Saber.

Determine:

- which `libbigscreen.so` hash/version was loaded;
- whether any stale secondary/native runtime components came from the source deployment;
- whether logs identify the active build;
- whether Scotland2 loaded more than one Big Screen copy;
- whether one exists in both `early_mods` and `mods`;
- whether private copied libraries survive across installs.

Do not infer from MBF's displayed version alone.

Use the actual loaded binary/hash/log evidence.

---

# 14. Specifically check Scotland2 copied/private state

Investigate whether Scotland2 copies mod libraries from shared ModData into an app-private directory and whether stale private copies can survive when MBF changes the external source file.

Determine:

- when Scotland2 refreshes those copies;
- whether timestamps/hashes determine replacement;
- whether a source-deployed binary can remain in a private location;
- whether force-stopping Beat Saber is sufficient;
- whether the private copy is deleted/replaced on every startup.

This may explain a mismatch between the shared-storage file and the binary actually loaded.

Verify it.

---

# 15. Check opposite-phase duplicates

The current deploy script already removes an opposite-phase stale copy.

Verify whether MBF does the same.

Look for:

```text
early_mods/libbigscreen.so
mods/libbigscreen.so
```

or any equivalent duplicate location.

If both exist:

- determine which one Scotland2 loads first;
- determine whether both can load;
- determine which install mechanism created each.

This needs evidence from the actual Quest state.

---

# 16. Check runtime/private dependencies

The source deploy also places Big Screen-private/runtime files outside the main `libbigscreen.so`.

Determine whether MBF's QMOD package declares and replaces the exact same files.

Compare source deployment destinations with QMOD `fileCopies`.

Look especially at:

- private FFmpeg libraries;
- CPython libraries;
- OpenSSL/SQLite;
- QuickJS-related artifacts;
- downloader runtime files;
- yt-dlp runtime;
- certificates;
- status/update files.

Determine whether an MBF install updates all of these or leaves source-deployed files intact.

A new main `.so` combined with stale private runtime files could explain apparent version mismatch or odd behavior.

---

# 17. Package ownership metadata

Document exactly what MBF records for the installed package.

Determine whether MBF:

- tracks only files declared in the QMOD;
- stores hashes;
- stores package/version identity;
- will overwrite an existing file it does not believe it owns;
- refuses replacement under any circumstances;
- ignores unknown pre-existing files at declared destinations;
- removes stale declared files from older package versions.

Do not generalize from the code unless the observed behavior matches.

---

# 18. Determine the real root cause

Conclude with one of the following or another evidence-backed explanation:

```text
MBF DOES replace source-deployed Big Screen correctly; earlier observation likely came from ______.

MBF replaces the main binary but stale source-deployed runtime files remain.

An opposite-phase duplicate causes the source version to continue loading.

A Scotland2 private copy remains stale.

MBF intentionally does not overwrite the unowned source-deployed file.

The issue could not be reproduced.
```

Do not force the result to match the original assumption.

---

# 19. Source-install receipt design

Assuming direct source deployment remains the recommended development workflow, refine the proposed **source-install receipt**.

The receipt should make source deployment/removal deterministic.

Determine where it should live.

Possible example:

```text
BigScreen/SourceInstall/source-install.json
```

or another location that fits the project.

It should record at minimum:

```json
{
  "schemaVersion": 1,
  "bigScreenVersion": "...",
  "installTimestamp": "...",
  "files": [
    {
      "path": "...",
      "sha256": "...",
      "ownership": "BigScreenSourceDeploy"
    }
  ]
}
```

Consider whether it should also include:

- source commit hash;
- build type;
- QMOD manifest version;
- device Beat Saber version;
- destination category;
- whether each file is safe to remove;
- whether each file is private or shared.

Keep it minimal but sufficient.

---

# 20. Removal behavior when hashes differ

Define exact behavior for:

```text
receipt says file hash = A
installed file hash = B
```

The removal tool should not blindly delete it.

Determine whether it should:

- skip the file;
- warn that another installation likely replaced it;
- offer manual override;
- query MBF package ownership if possible;
- distinguish settings/user data from executable files.

For safety, prefer refusal over destructive guessing.

---

# 21. Mixed MBF/source install handling

Determine how the removal utility should behave when both are present.

Examples:

### Case A

Receipt exists and binary still matches source-deployed hash.

→ safe to remove source copy.

### Case B

Receipt exists but binary matches MBF-installed package.

→ do not remove MBF-owned binary.

### Case C

Duplicate files exist in different Scotland2 phases.

→ identify and remove only verified source-owned copy.

### Case D

Receipt missing.

→ do not guess broadly; use a conservative legacy-detection path or instruct user.

Define the safest policy.

---

# 22. Should deploy write a receipt before or after copy?

The receipt itself must not claim ownership of files that failed to install.

Recommend transactional behavior such as:

```text
copy file
↓
verify remote hash
↓
record receipt entry
```

and only finalize the receipt after the complete deployment succeeds.

If deployment partially fails, determine how removal can still clean up safely.

Perhaps use:

```text
source-install.partial.json
```

during installation and atomically promote to:

```text
source-install.json
```

after success.

Assess whether that complexity is warranted.

---

# 23. Do not change development deployment to MBF yet

The previous recommendation was to keep the direct-copy workflow because it is fast and already works well for development.

Do not change that unless the controlled test proves there is a fundamental compatibility problem that cannot be solved safely with:

- better cleanup;
- receipts;
- uninstall tooling;
- clear transition guidance.

If direct copy remains viable, preserve the one-click developer workflow.

---

# 24. Deliverable

Do not implement.

Return:

## Logging architecture comparison

A concise comparison table:

```text
synchronous append
background writer
hybrid
```

covering:

- crash fidelity;
- performance;
- implementation complexity;
- thread/lifetime complexity;
- event loss risk;
- suitability for yt-dlp.

## Logging recommendation

Choose the preferred design and explain why.

## Expected event rate

Estimate realistic menu/download logging volume.

## Crash behavior

Explain exactly what data each architecture may lose after a hard native crash.

## MBF reproduction result

Describe the actual test performed and what happened.

If it could not be performed, provide the exact test procedure still required.

## Loaded-file evidence

List hashes/paths before and after MBF installation where available.

## Root cause

State the most likely or confirmed cause of the source-install/MBF conflict.

## Source-install receipt recommendation

Define:

- location;
- schema;
- ownership semantics;
- hash behavior.

## Removal behavior

Explain how removal should behave for:

- matching files;
- changed files;
- MBF-owned files;
- duplicates;
- missing receipt.

## Revised implementation recommendation

State whether the previous implementation order should change based on these findings.

---

# Important rules

- **Do not implement the logging feature yet.**
- **Do not create the uninstall script yet.**
- **Do not change deployment behavior yet.**
- Do not introduce another background worker unless the measured/expected workload justifies it.
- Preserve crash-reconstruction fidelity as the primary logging goal.
- Do not assume MBF is at fault until the actual loaded files are compared.
- Do not delete or replace Quest files merely to complete the test without explicit safety checks.
- Use actual hashes and paths wherever possible.
- Treat mixed MBF/source ownership conservatively.
- Prefer refusing to delete an ambiguous file over guessing.
- Preserve the current fast one-click source deployment unless evidence shows it is fundamentally unsafe.

The goal is to resolve the two remaining design uncertainties before implementation begins:

> **What logging write model gives Big Screen the most reliable pre-crash history with the least unnecessary complexity, and what exactly causes a source-deployed Big Screen install to conflict with a later MBF installation?**
