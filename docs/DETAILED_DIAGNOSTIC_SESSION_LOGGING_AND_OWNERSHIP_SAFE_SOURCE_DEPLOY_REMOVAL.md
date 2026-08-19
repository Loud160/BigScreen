# Codex Implementation Task — Detailed Diagnostic Session Logging + Ownership-Safe Source Deploy/Removal

## Objective

Implement the previously analyzed and approved Big Screen changes for:

1. Detailed diagnostic logging of Big Screen menu interactions.
2. Detailed diagnostic logging of song-selection video download interactions.
3. Error correlation between session logs and the existing persistent error history.
4. Sanitized yt-dlp operational logging.
5. Automatic retention of the most recent diagnostic sessions.
6. Support-log collection of the new diagnostic logs.
7. Source-deployment ownership receipts.
8. Safe classification of source/MBF/legacy/mixed Big Screen installations.
9. Ownership-aware Build & Deploy behavior.
10. A one-click Big Screen source-removal workflow.
11. Safe one-time migration from existing pre-receipt source deployments.

The architecture for these features has already been discussed and accepted.

**Implement it now.**

Do not reopen broad architectural discussion unless the current repository has materially changed in a way that makes an approved design unsafe or impossible.

Do not perform unrelated refactors or feature work.

---

# 0. Preserve current work first

Before modifying anything:

```text
git status
git diff
```

Inspect the current branch and preserve all unrelated existing working-tree changes.

Do not overwrite or revert user work.

Review the existing design documents created during the prior discussion, including the documents covering:

- diagnostic logging architecture;
- logging writer comparison;
- source-deploy / MBF conflict;
- mixed MBF/source ownership;
- legacy source installations.

Treat the **latest approved conclusions** as authoritative if an earlier design document differs.

---

# 1. Accepted logging architecture

Implement the accepted **hybrid logging model**.

Do **not** add a new general-purpose diagnostic writer thread.

## Important user/session events

These must be written synchronously to an already-open session file:

```text
event occurs
↓
collect required values
↓
release subsystem locks
↓
serialize JSONL record
↓
lock session sink
↓
append
↓
ordinary C++ flush()
↓
unlock
```

Do **not** `fsync()` after every event.

Important events include:

- menu entry/exit;
- tab/navigation actions;
- setting changes;
- dialogs;
- previews;
- assignments;
- downloads;
- resolution decisions;
- cancellation;
- download stage changes;
- errors;
- terminal results.

The design goal is:

> If Beat Saber crashes immediately after a meaningful user action, that action should already be represented in the session log whenever reasonably possible.

---

# 2. Logging must never participate in subsystem lock ordering

This rule is mandatory:

```text
1. Read/copy required state while holding the subsystem lock if necessary.
2. Release the subsystem lock.
3. Submit/write the diagnostic event.
```

Do not call the diagnostic logger while holding important:

- DownloadManager locks;
- VideoLibrary locks;
- settings locks;
- menu-state locks;
- other subsystem mutexes

unless the current implementation proves that doing so is unavoidable and safe.

The diagnostic logger itself may own only its own sink/session mutexes.

While holding a diagnostic sink mutex, it must not call:

- UI code;
- Settings;
- DownloadManager;
- VideoLibrary;
- Paper logging;
- ErrorManager;
- other Big Screen subsystems.

Avoid recursive/error-reporting lock paths.

---

# 3. Detailed logging setting

Add a user-visible toggle to the **Misc** tab.

Use wording consistent with the existing UI, conceptually:

```text
Detailed Diagnostic Logging
```

or:

```text
Detailed Menu Logging
```

Choose the clearest existing style.

Requirements:

- Default: **ON**
- Persisted with Big Screen's normal settings.
- Master/reset-to-default behavior must restore it to **ON**.
- The visible BSML/UI control and stored default must agree.
- When OFF, no menu/download diagnostic session logs should be created.

Do not disable or alter existing:

- Paper logging;
- `error-history.log`;
- performance history;
- crash/tombstone collection.

This is an additional diagnostic facility.

---

# 4. Session log format

Use **JSON Lines / JSONL**.

One structured JSON object per line.

The files should remain readable by humans while also being easy to parse later.

Use separate directories:

```text
BigScreen/Logs/Sessions/Menu/
BigScreen/Logs/Sessions/Download/
```

or the exact equivalent path derived from the existing Big Screen storage abstraction.

Do not hard-code a second unrelated storage root.

Suggested filenames:

```text
BigScreen-Menu-YYYY-MM-DD-HHMMSS-fff.jsonl
BigScreen-Download-YYYY-MM-DD-HHMMSS-fff.jsonl
```

Use collision-safe naming if multiple sessions begin within the same timestamp resolution.

---

# 5. Event schema

Define a small stable event schema.

At minimum each record should contain useful fields equivalent to:

```json
{
  "timestampUtc": "...",
  "elapsedMs": 1234,
  "sessionType": "menu",
  "event": "setting_changed",
  "source": "SettingsMenu",
  "data": {
  }
}
```

Do not make every event carry huge repeated context.

Put stable session-level context in the initial `session_start` record.

Keep event naming consistent and machine-friendly.

Prefer explicit names such as:

```text
session_start
session_end
tab_changed
song_selected
dialog_opened
dialog_accepted
dialog_cancelled
setting_change_begin
setting_changed
preview_started
preview_paused
preview_seeked
preview_stopped
video_assigned
video_unassigned
download_clicked
resolution_selected
resolution_cancelled
download_started
download_stage
download_progress
download_cancel_requested
download_cancelled
download_completed
download_failed
ytdlp_message
error
```

Refine as needed based on the actual current UI/events.

---

# 6. Menu session lifetime

Begin a menu diagnostic session at the reliable Big Screen menu activation boundary already identified:

```text
MenuFlowCoordinator::DidActivate()
```

Close it at:

```text
MenuFlowCoordinator::DidDeactivate()
```

Verify the current code before relying on these exact functions.

Nested Big Screen pages must remain part of the **same menu session** rather than creating a new log for every sub-page.

Opening Big Screen again later should create a new session file.

If menu teardown occurs unexpectedly:

- flush/close safely when lifecycle callbacks permit;
- do not require a closing record for a log to be considered valid.

A hard crash leaving the last line as an ordinary action/error rather than `session_end` is useful diagnostic evidence.

---

# 7. Menu session context

At `session_start`, record practical diagnostic context available without expensive or fragile probing.

Include where reliable:

- Big Screen version;
- Beat Saber version;
- Quest model;
- game package/version;
- relevant Big Screen mode/context;
- active layout;
- selected song/map if applicable;
- relevant storage information if already available cheaply;
- important current Big Screen settings.

Do not dump secrets or unrelated device/user information.

Do not make session creation fail because an optional context field is unavailable.

Optional fields should simply be omitted or recorded as unavailable.

---

# 8. Menu action coverage

Instrument enough existing callbacks that the session can reconstruct user behavior.

At minimum cover:

- entering/leaving Big Screen;
- switching tabs/pages;
- song/map selection where Big Screen receives it;
- Video Library navigation;
- video selection;
- preview play/start;
- preview pause;
- preview seek;
- preview stop;
- video assignment;
- assignment removal;
- layout changes;
- reset operations;
- toggles;
- dropdown/select values;
- arrow-button value adjustments;
- dialogs opened;
- dialogs confirmed;
- dialogs canceled;
- download-related actions.

Do not log purely programmatic UI refreshes as if the user performed them.

Where necessary mark an event source such as:

```text
User
SettingsMenu
SongSelection
Reset
SystemNormalization
ProgrammaticRefresh
```

Prefer not logging programmatic refreshes at all unless they are diagnostically meaningful.

---

# 9. Slider logging

Do **not** log every slider callback.

Implement the accepted coalescing policy.

For a user slider interaction:

1. Record/capture the initial value when the value first begins changing.
2. Track the latest value.
3. After approximately **300–500 ms without another user-driven slider change**, write the final settled value.

The final diagnostic information must show:

```text
setting/control
initial value
final value
```

It may be represented as one final event containing both values or a begin/end pair.

Do not create a new dedicated worker thread solely for slider coalescing.

Use existing Unity/update/timing facilities or another simple mechanism that fits the current menu architecture.

If the menu closes while a slider value is pending, flush the final pending value before closing the session where practical.

Arrow-button adjustments are discrete user actions and may be logged individually.

---

# 10. Download session lifetime

Download sessions are separate from menu sessions.

A download session begins when the user clicks Big Screen's **Download** action from song selection or another supported Big Screen download UI.

Begin the session **before** the resolution-selection dialog.

The log should therefore preserve cases where the user clicks Download and then cancels without starting a transfer.

A download session remains active through:

```text
download click
↓
probe/resolution dialog
↓
selection or cancellation
↓
transfer
↓
post-processing
↓
completion / cancellation / failure
```

A download session must be able to outlive the menu/session that initiated it.

Menu and download sessions may overlap.

Never require holding both session mutexes simultaneously.

---

# 11. Download session context

The initial download record/session should include where available:

- song name;
- song author;
- mapper;
- level ID/hash;
- characteristic/difficulty if relevant;
- original configured/user-facing video URL;
- current Big Screen version.

Record the original user-facing/source URL.

Do **not** record secrets or temporary signed media URLs.

---

# 12. Resolution interactions

Log:

```text
download_clicked
resolution_dialog_opened
resolution_selected
resolution_cancelled
```

For selection, include the actual selection presented to the downloader, such as:

- 720p;
- 1080p;
- source/automatic;
- whatever current options actually exist.

If probing fails before the dialog can be populated, record that outcome.

---

# 13. Download progress

Do not record all existing downloader progress callbacks.

Implement:

- every stage transition;
- progress at approximately **5% boundaries**;
- when total size is unavailable, approximately every **5 seconds**;
- cancellation request immediately;
- cancellation result;
- terminal success;
- terminal failure.

Include useful fields that already exist, where available:

```text
downloaded bytes
estimated/total bytes
percentage
speed
ETA
stage
output filename/path
```

Avoid inventing expensive calculations merely for diagnostics.

---

# 14. yt-dlp logging

Add a yt-dlp logger integration if supported cleanly by the current embedded Python setup.

The existing use of:

```text
quiet = true
no_warnings = true
```

may need to be adjusted internally so operational messages can be captured without dumping them to unwanted destinations.

Do not blindly enable unrestricted debug output.

Capture useful operational information through a custom logger callback.

## Mandatory sanitization

Never record raw sensitive/transient values such as:

- cookies;
- Authorization headers;
- PO tokens;
- signed temporary media URLs;
- `googlevideo.com` query strings/tokens;
- authentication parameters;
- other temporary secrets.

The original user-facing YouTube/source URL may be logged.

Resolved temporary media URLs should be redacted or omitted.

---

# 15. yt-dlp batching policy

Do not route high-frequency yt-dlp chatter through the Unity thread.

Use the **existing downloader worker context**.

Accepted policy:

- immediately write/flush:
  - warnings;
  - errors;
  - selected-format information;
  - important stage transitions;
  - terminal messages;
- filter obvious duplicate/low-value messages;
- ordinary low-value operational chatter may be batched for approximately **0.5–1 second** before being appended.

Do not create a new generic diagnostic writer thread.

The only diagnostic information acceptable to lose during an instantaneous hard crash is the last small batch of **low-value yt-dlp chatter**.

User actions and important downloader state must already be flushed.

Timestamp yt-dlp events so chronological reconstruction with UI/download events remains possible.

---

# 16. Error correlation IDs

Extend the existing `ErrorManager` with a concise correlation identifier for persistent errors.

Requirements:

- Every error written to the persistent error history receives an ID.
- The ID is also returned/exposed to the caller or active diagnostic integration.
- Active menu/download session logs write an `error` event containing:
  - correlation ID;
  - concise error message/category;
  - relevant context;
  - reference/location of the persistent error history.

Do not duplicate enormous stack traces into session logs.

The existing error-history mechanism remains authoritative for full diagnostic details.

Session logging failures must **not** call back into ErrorManager.

Avoid recursion such as:

```text
session logger fails
→ ErrorManager
→ session logger
→ ErrorManager
```

At most emit one safe Paper warning when a session logger becomes unavailable.

---

# 17. Session logger failure behavior

Diagnostic logging is optional instrumentation.

Any failure to:

- create a directory;
- create a file;
- append;
- flush;
- serialize;
- rotate logs;
- process yt-dlp diagnostic output

must fail open.

Required behavior:

```text
disable affected diagnostic session
↓
optionally issue one Paper warning
↓
continue normal Big Screen behavior
```

Never:

- crash Beat Saber;
- cancel a video download;
- block normal UI behavior;
- enter recursive error handling.

---

# 18. Session retention

Keep separately:

```text
10 most recent Menu session logs
10 most recent Download session logs
```

Delete only older diagnostic session files in those exact managed folders.

Never rotate/delete:

- `error-history.log`;
- performance history;
- Paper logs;
- videos;
- settings;
- other logs.

Do not delete the currently active session.

Run retention at a sensible lifecycle point, preferably when a new session begins or after a session is created successfully.

---

# 19. Support/log collector

Extend the existing support-log retrieval workflow.

The current collector must continue retrieving everything it already retrieves.

Add all retained diagnostic sessions to the resulting support package.

Prefer a structure such as:

```text
Sessions/
    Menu/
    Download/
```

inside the collected ZIP/folder.

Do not flatten files if doing so could cause filename collisions.

A failure to retrieve one session file must not prevent collecting the rest of the support package.

---

# 20. Source-install ownership receipt

Implement an ownership receipt for direct source deployment.

Use:

```text
/sdcard/ModData/com.beatgames.beatsaber/BigScreen/SourceInstall/
```

with:

```text
source-install.partial.json
source-install.json
```

The final schema may be refined, but must support at minimum:

```json
{
  "schemaVersion": 1,
  "state": "complete",
  "modId": "bigscreen",
  "bigScreenVersion": "...",
  "sourceCommit": "...",
  "buildType": "Release",
  "gameVersion": "...",
  "installedAtUtc": "...",
  "files": [
    {
      "path": "...",
      "category": "...",
      "ownership": "BigScreenExclusive",
      "previousState": "absent",
      "previousSha256": null,
      "installedSha256": "..."
    }
  ]
}
```

Use exact Quest destination paths.

Do not record vague directory ownership where individual files can be tracked.

---

# 21. Preserve baseline across repeated source builds

This is mandatory.

The receipt must distinguish:

```text
baseline state before source-development mode began
```

from:

```text
current source-deployed hash
```

Example:

```text
Before development:
file absent

Deploy A:
previousState = absent
installedHash = A

Deploy B:
previousState MUST REMAIN absent
installedHash = B

Deploy C:
previousState MUST REMAIN absent
installedHash = C

Remove:
delete file
```

Do not let each Build & Deploy redefine the previous source build as the baseline.

For paths first introduced in a later version, capture their baseline when they first become source-managed.

For retired paths, remove them only when ownership is proven and their current hash still matches the last source receipt.

---

# 22. Partial deployment receipt

Before changing deployment destinations, write:

```text
source-install.partial.json
```

with the complete planned operation.

For each file record:

- destination;
- intended source hash;
- previous presence;
- previous hash if present;
- ownership classification;
- category.

Then perform copies.

After each remote copy:

- verify the remote hash.

After the entire intended deployment verifies:

- atomically promote/replace the partial receipt with `source-install.json`.

If deployment aborts, preserve enough information in the partial receipt for the next deployment/removal run to determine what was and was not changed.

---

# 23. Partial recovery algorithm

When `source-install.partial.json` exists:

For each planned file:

### Current hash == intended source hash

The source copy completed.

### Current hash == previous hash

The copy did not occur or the old state was restored.

### File absent and previous state == absent

Nothing to clean.

### Anything else

Ownership/state is ambiguous.

Preserve the file and report it.

The tool may offer/perform:

```text
Resume deployment
```

or:

```text
Undo proven source changes
```

depending on which operation was requested.

Never guess when hashes do not establish ownership.

---

# 24. MBF package detection

Source deployment must detect Big Screen package registration in MBF.

Scan the actual MBF package metadata location for the active Beat Saber version, conceptually:

```text
/sdcard/ModData/com.beatgames.beatsaber/Packages/<game-version>/*/mod.json
```

Match using the manifest's **mod ID**:

```text
bigscreen
```

Do not trust package directory names.

Classification must concern executable/runtime/package metadata only.

User data must never make Big Screen appear installed.

---

# 25. Fixed policy: REFUSE OVER MBF

This policy is mandatory.

If **any Big Screen MBF package metadata exists** for the active Beat Saber version:

```text
MBF_MANAGED
```

or:

```text
MBF_REGISTERED_NOT_INSTALLED
```

source Build & Deploy must refuse to overwrite it.

This remains true even if MBF currently reports the package as disabled or its payload is incomplete.

Display a clear explanation similar to:

```text
Big Screen is currently registered with ModsBeforeFriday.

Source deployment would overwrite files managed by MBF without updating
MBF's package ownership information.

Remove Big Screen from MBF's package list before deploying a source build.

No files were changed.
```

Do not merely tell the user to disable the mod.

The MBF package metadata must be removed first.

---

# 26. Installation-state classifier

Implement a deterministic classifier equivalent to:

## `NOT_INSTALLED`

- no complete receipt;
- no partial receipt;
- no Big Screen MBF package;
- no Big Screen executable/runtime payload.

User videos/data may still exist.

## `SOURCE_MANAGED`

- valid complete source receipt;
- no MBF metadata;
- tracked files are consistent with receipt ownership/current hashes.

## `SOURCE_PARTIAL`

- valid partial receipt exists;
- no MBF metadata.

## `MBF_MANAGED`

- Big Screen MBF package metadata exists;
- no source receipt;
- package-exclusive installed files correspond to the MBF package payload.

## `MBF_REGISTERED_NOT_INSTALLED`

- Big Screen MBF package metadata exists;
- one or more package payload files are absent/not currently installed.

## `LEGACY_SOURCE`

- Big Screen executable/runtime payload exists;
- no MBF metadata;
- no source receipt.

## `MIXED_OR_AMBIGUOUS`

Examples:

- MBF metadata + source receipt;
- contradictory hashes;
- multiple Big Screen package registrations;
- opposite Scotland2 phase duplicates with uncertain ownership;
- tracked source path contains unknown content.

Refine detection as required by the actual repository/MBF format.

---

# 27. Build & Deploy behavior matrix

Implement:

## NOT_INSTALLED

Deploy normally:

```text
create partial receipt
copy + verify
promote receipt
```

## SOURCE_MANAGED

Update normally.

Preserve original baseline ownership information.

Update current source hashes/version/commit/timestamp.

## SOURCE_PARTIAL

Detect the partial deployment and provide/perform safe recovery according to the existing script UX.

Prefer resuming if the state is clean and expected.

Do not overwrite ambiguous files.

## MBF_MANAGED

Refuse.

No files changed.

Tell user to remove Big Screen through MBF first.

## MBF_REGISTERED_NOT_INSTALLED

Refuse.

No files changed.

Tell user to remove the dormant Big Screen package from MBF.

## LEGACY_SOURCE

Perform a **guided one-time clean migration** before fresh deployment.

Do not silently adopt unknown historical ownership.

## MIXED_OR_AMBIGUOUS

Refuse normal deployment.

Generate a clear ownership diagnostic report.

Do not modify ambiguous files automatically.

---

# 28. Legacy source migration

Existing source deployments created before receipts cannot truthfully claim their prior baseline was absent.

Do not manufacture a fake baseline.

For `LEGACY_SOURCE`:

1. Confirm no Big Screen MBF metadata exists.
2. Enumerate only the exact executable/runtime destinations historically used by Big Screen source deployment.
3. Determine which files are confidently Big Screen-exclusive.
4. Preserve all shared dependencies.
5. Preserve all user data.
6. Remove only confidently source/Big-Screen-owned legacy payload.
7. Verify source payload removal.
8. Deploy current source build fresh.
9. Create a normal receipt from the newly established baseline.

Require a clear user confirmation before performing the clean-migration removal.

Explain that:

- Big Screen executable/runtime files will be refreshed;
- settings are preserved;
- videos are preserved;
- library data is preserved;
- logs are preserved.

If any file is ambiguous, preserve it and abort or report the remaining ambiguity rather than guessing.

---

# 29. Legacy cleanup safety

Legacy cleanup may remove only verified Big Screen-exclusive items such as:

- `libbigscreen.so` at the verified Scotland2 phase;
- uniquely named Big Screen-private FFmpeg/backend libraries;
- exact Big Screen Runtime files declared/installed by the repository;
- other uniquely owned paths proven by current manifests/scripts.

Never remove:

```text
libbeatsaber-hook.so
Scotland2/Paper
BSML
SongCore
CustomTypes
shared dependencies
```

or any library claimed by another installed package.

Never recursively delete:

```text
BigScreen/
```

---

# 30. Source removal files

Create:

```text
Remove-BigScreen.bat
scripts/remove-bigscreen.ps1
```

Follow the defensive UX/style of the existing:

- Build & Deploy;
- support-log retrieval

scripts.

The BAT should be safe for double-click use and leave the result visible.

---

# 31. Removal confirmation

Before removing any source-managed mod files, display a clear confirmation.

Conceptually:

```text
Big Screen source installation will be removed from the connected Quest.

Downloaded videos, the Video Library, logs, and other user media will NOT
be removed.

Continue? [Y/N]
```

If declined:

```text
No files were changed.
```

---

# 32. Settings confirmation

After the user confirms mod removal, ask separately:

```text
Also remove Big Screen settings? [Y/N]
```

Default should be **No / preserve settings**.

Make it clear:

```text
Removing the settings file will NOT remove downloaded videos.
```

Only delete the exact Big Screen settings/config file after explicit confirmation.

---

# 33. User data must always be preserved

Unless explicitly and separately requested in some future feature, source uninstall must preserve:

```text
BigScreen/Videos
BigScreen/Thumbnails
BigScreen/Video Import
BigScreen/library.json
library backups
BigScreen/Logs
map-local videos
movement/choreography files
demo/custom maps
other user-created/downloaded content
```

The uninstall workflow being implemented now does not offer video/library deletion.

---

# 34. Removal behavior by state

## NOT_INSTALLED

Report:

```text
No source-managed Big Screen installation was found.
```

Preserve user data.

## SOURCE_MANAGED

For each receipt-owned file:

### Current hash == source receipt installed hash

Remove/restore according to baseline.

For the normal baseline-absent source case:

```text
delete exact file
```

### Current hash != receipt installed hash

Preserve it.

Warn that another installer/user may have replaced it.

Do not delete.

After all safe removals:

- remove source receipt only if source ownership has been successfully reconciled;
- otherwise retain diagnostic information as appropriate.

## SOURCE_PARTIAL

Use baseline/intended/current hashes.

Undo only proven source writes.

Preserve ambiguous files.

## MBF_MANAGED

Make no changes.

Tell user:

```text
This Big Screen installation is managed by ModsBeforeFriday.
Remove it through MBF.
```

## MBF_REGISTERED_NOT_INSTALLED

Make no changes.

Tell user to remove the Big Screen package metadata through MBF.

## LEGACY_SOURCE

Offer the guided legacy cleanup.

Do not perform broad automatic deletion without confirmation.

## MIXED_OR_AMBIGUOUS

Reconcile only when individual file ownership is proven.

Rules:

- current hash == source receipt hash:
  - source overlay is proven;
  - remove only if doing so does not delete a file simultaneously required/owned by MBF;
- current hash == extracted MBF package hash and differs from source receipt:
  - preserve;
- current hash matches both:
  - preserve and let MBF reconcile ownership;
- current hash matches neither:
  - preserve and report ambiguity.

If MBF registration exists after any source-overlay cleanup:

```text
Reinstall/repair Big Screen through ModsBeforeFriday.
```

Do not pretend the source tool can restore MBF's package state.

---

# 35. Source → MBF transition guarantee

After successful source removal, verify no **source-owned Big Screen executable/runtime payload** remains at destinations that would cause MBF's installed-file existence check to consider the QMOD already installed.

Clean every verified source-owned:

- main early/late mod file;
- private Big Screen libraries;
- exact runtime fileCopies;
- opposite-phase duplicates;
- source receipt/partial receipt files.

Do not rely merely on deleting one file even though one missing required file is enough to make MBF's all-files-exist check false.

Clean all proven source payload.

Then print a clear result such as:

```text
Big Screen source installation removed.

Downloaded videos and user data were preserved.

You may now install Big Screen normally through ModsBeforeFriday.
```

---

# 36. ADB/device safety

Build/deploy/removal scripts must:

- locate the existing ADB strategy used by the repo;
- require a valid authorized Quest;
- safely handle no device;
- safely handle unauthorized device;
- safely handle multiple devices;
- never operate on an empty/unverified path;
- avoid broad wildcard deletion;
- report meaningful errors;
- use non-zero exit codes on failure;
- preserve existing ADB shutdown convention where appropriate.

Removal should force-stop Beat Saber before deleting loaded mod files.

---

# 37. Source deploy symmetry

The deployment receipt should cover every Big Screen-owned payload written by source deployment.

Add tests/invariants ensuring:

> Every Big Screen-exclusive file copied by source deployment is either tracked by the source receipt/removal system or explicitly classified as preserved/shared with justification.

Do not allow deployment/removal behavior to drift independently.

---

# 38. Shared dependency classification

Do not claim source ownership over shared mod ecosystem dependencies.

Examples include:

```text
beatsaber-hook
Paper/Scotland2
BSML
SongCore
CustomTypes
```

If Build & Deploy needs to copy/ensure one of these, mark it appropriately as shared and **never remove it** merely because Big Screen used it.

Private uniquely named Big Screen runtime libraries may be source-owned.

Derive the authoritative list from the current QMOD/build/deploy manifests.

---

# 39. Opposite-phase duplicate handling

Continue the existing protection against stale:

```text
early_mods/libbigscreen.so
mods/libbigscreen.so
```

copies.

Integrate this with ownership receipts.

Do not delete an opposite-phase copy merely by filename if its ownership is ambiguous.

If it is a previously known source-deployed duplicate and its hash proves ownership, remove it.

Otherwise report it as ambiguous and refuse unsafe deployment/removal.

---

# 40. Tests — diagnostic logging

Add focused tests/invariants where practical for:

- diagnostic setting defaults ON;
- reset returns setting to ON;
- setting OFF prevents session creation;
- menu activation creates one menu session;
- nested pages do not create separate menu sessions;
- menu exit closes session;
- JSONL records parse correctly;
- meaningful UI events are logged;
- programmatic refresh does not masquerade as user action;
- sliders coalesce to initial/final values;
- errors receive correlation IDs;
- session error references match error history IDs;
- 10 menu sessions retained;
- 10 download sessions retained;
- active session not deleted by rotation;
- logging failure does not affect normal behavior;
- concurrent menu/download writes do not corrupt either file;
- logger does not acquire subsystem locks recursively.

Do not build brittle tests around irrelevant exact timestamps.

---

# 41. Tests — downloader diagnostics

Add focused coverage for:

- Download click creates session before resolution selection.
- Resolution selection logged.
- Resolution cancellation logged.
- Download cancellation request logged.
- Download terminal cancellation logged.
- Stage transitions logged.
- Progress throttled/coalesced as designed.
- Unknown-size download uses time-based reporting.
- Original source URL logged.
- Signed/resolved media URLs are not leaked.
- yt-dlp warnings/errors logged immediately.
- low-value duplicate yt-dlp chatter filtered/batched.
- terminal yt-dlp/download result flushed.

Add explicit redaction tests containing representative sensitive URL/query/token patterns.

---

# 42. Tests — ownership classifier

Create script/unit/invariant tests for every classifier state using isolated fixtures rather than requiring the physical Quest when possible:

```text
NOT_INSTALLED
SOURCE_MANAGED
SOURCE_PARTIAL
MBF_MANAGED
MBF_REGISTERED_NOT_INSTALLED
LEGACY_SOURCE
MIXED_OR_AMBIGUOUS
```

Test package discovery by manifest ID rather than directory name.

Test duplicate/opposite-phase detection.

Test contradictory hashes.

---

# 43. Tests — Build & Deploy policy

Verify:

- clean source install creates a partial receipt then complete receipt;
- remote hash must verify before successful completion;
- repeated source deploy preserves original baseline;
- current installed hash updates;
- newly introduced path gets a new baseline;
- retired source path is cleaned only when ownership matches;
- MBF-managed state refuses deployment;
- MBF-registered-not-installed state also refuses deployment;
- mixed/ambiguous state refuses deployment;
- no refusal path modifies Quest files.

Where actual ADB behavior cannot be unit tested, split decision logic from transport sufficiently to test policy independently.

Do not over-refactor the deployment scripts just for theoretical purity.

---

# 44. Tests — removal

Verify:

- confirmation No changes nothing;
- settings are preserved by default;
- settings removed only after separate confirmation;
- videos never appear in removal target list;
- library data never appears in removal target list;
- logs never appear in removal target list;
- matching source-owned file is removable;
- changed/unknown file is preserved;
- MBF-owned file is preserved;
- shared dependencies are never removed;
- source partial undo removes only proven writes;
- missing receipt does not trigger broad filename deletion;
- legacy cleanup uses only exact verified paths;
- unsafe wildcards are absent;
- source receipt is removed after successful cleanup;
- MBF transition leaves source-owned package payload absent.

---

# 45. Support collector tests

Update repository invariants/tests to verify:

- existing support artifacts are still collected;
- retained Menu sessions are included;
- retained Download sessions are included;
- output subdirectories do not collide;
- absence of session logs is not an error.

---

# 46. Controlled MBF/source validation

After implementation and automated tests pass, perform the safest practical controlled verification using the connected Quest **only if the current authorization/workflow allows it**.

Do not casually destroy the developer's current environment.

At minimum verify source-managed behavior first.

The eventual installation matrix should cover:

```text
clean → source
source → source
source → remove
remove → MBF
MBF → source refusal
MBF registered/incomplete → source refusal
legacy source → migration → source
```

For destructive MBF package operations that cannot safely be automated within current authorization, report the exact remaining manual test rather than guessing.

---

# 47. Build and validation

Run the project's normal validation after implementation.

At minimum use the repository-authoritative equivalents of:

```powershell
scripts\test.ps1
scripts\build.ps1
```

Run repository invariant tests.

Run PowerShell/script syntax validation where available.

Run:

```text
git diff --check
```

Inspect the final diff.

If deployment to the connected Quest is part of the repository's already-authorized normal validation workflow, deploy and verify appropriate functionality.

Do not claim visual/UI success from compilation alone.

---

# 48. Manual Quest validation checklist

If deployed for testing, verify at minimum:

## Menu logging

1. Enter Big Screen.
2. Confirm one new menu log.
3. Change toggles.
4. Change dropdowns.
5. Drag slider repeatedly.
6. Confirm coalesced initial/final values.
7. Navigate pages.
8. Preview video.
9. Leave menu.
10. Confirm session ends/flushed.

## Download logging

1. Click Download.
2. Cancel resolution dialog.
3. Verify canceled session.
4. Start another download.
5. Select resolution.
6. Observe progress.
7. Cancel transfer.
8. Verify progress/cancel sequence.
9. Complete another download.
10. Verify terminal success.

## Retention

Create enough sessions to verify:

```text
10 Menu
10 Download
```

remain.

## Support collector

Run the support/log retrieval BAT and verify both session folders are collected.

## Logging disabled

Disable detailed diagnostic logging and confirm no new session files are created.

---

# 49. Manual ownership/removal validation

Where safe:

1. Detect current legacy/source state correctly.
2. Perform the one-time clean migration if required.
3. Verify new receipt.
4. Run source deploy again.
5. Verify original baseline remains unchanged.
6. Verify current installed hashes change appropriately.
7. Run Remove Big Screen.
8. Decline first confirmation once and verify no changes.
9. Confirm removal.
10. Preserve settings.
11. Verify executable/runtime payload removed.
12. Verify videos/library/logs remain.
13. Verify MBF would no longer consider the QMOD installed merely from source payload existence.

Do not remove actual user videos or library data for testing.

---

# 50. Documentation

Update the README/developer documentation where appropriate.

Document:

## Detailed logging

- toggle location;
- default ON;
- session log locations;
- 10 + 10 retention;
- support collector behavior;
- privacy/sanitization of yt-dlp output.

## Source deployment

Explain that source Build & Deploy creates an ownership receipt.

## MBF interaction

Explicitly state:

> Do not source-deploy Big Screen over an MBF-registered Big Screen package.

If MBF has Big Screen registered:

1. Remove Big Screen from MBF's package list.
2. Then use Build & Deploy.

## Switching back to MBF

Document:

```text
Run Remove-BigScreen.bat
↓
preserve user videos/settings as chosen
↓
install Big Screen through MBF
```

## Removal

Make clear:

- source remover is for repository/source deployments;
- MBF-managed installs should be removed through MBF;
- videos are preserved;
- settings are optional.

Keep documentation concise and user-focused.

---

# 51. Completion report

Return a detailed but concise implementation summary.

Include:

## Files changed

List files and purpose.

## Diagnostic logging

Report:

- logger architecture;
- menu lifecycle;
- download lifecycle;
- slider coalescing;
- yt-dlp batching/sanitization;
- retention;
- correlation IDs.

## Source ownership

Report:

- receipt schema/path;
- classifier states;
- repeated deployment behavior;
- partial recovery.

## MBF protection

Confirm that source deployment refuses when Big Screen MBF metadata exists.

## Legacy migration

Explain exactly how pre-receipt source installs are handled.

## Removal

Report:

- files/categories removed;
- files/categories always preserved;
- settings confirmation behavior.

## Tests

Report all automated test results.

## Build

Report build result.

## Quest validation

State exactly what was tested on-device and what remains unverified.

## Behavioral changes

Confirm that unrelated Big Screen video playback/rendering/gameplay behavior was not intentionally changed.

---

# Non-goals

Do not:

- redesign the video decoder;
- refactor rendering;
- modify screen choreography;
- change Cinema compatibility;
- change public mapping APIs;
- change video storage layout unnecessarily;
- remove user media;
- make source deployment install through MBF;
- attempt to repair MBF package metadata yourself;
- back up and restore whole MBF packages;
- add another general-purpose logging writer thread;
- log raw sensitive yt-dlp media URLs/tokens;
- recursively delete the Big Screen data directory;
- remove shared dependencies;
- silently adopt ambiguous legacy ownership;
- perform unrelated cleanup.

---

# Final fixed design principles

The implementation should preserve these accepted rules:

```text
Important diagnostic action
    ↓
synchronous JSONL append + flush
    ↓
best practical pre-crash fidelity
```

```text
yt-dlp low-value chatter
    ↓
existing downloader worker
    ↓
filter / brief batch
```

```text
MBF metadata exists
    ↓
SOURCE DEPLOYMENT REFUSED
```

```text
Source development begins
    ↓
capture original baseline once
    ↓
Deploy A
    ↓
Deploy B
    ↓
Deploy C
    ↓
baseline remains unchanged
```

```text
Source removal
    ↓
delete only hash-proven source-owned payload
    ↓
preserve settings unless explicitly requested
    ↓
always preserve videos/library/logs
    ↓
MBF can install normally afterward
```

```text
Unknown ownership
    ↓
PRESERVE + REPORT
```

The goal is not merely to produce more logs and an uninstall BAT.

The goal is to make Big Screen's **user-action diagnostics reliably reconstructable after crashes** while making the repository's **one-click source deployment just as deterministic and safe to reverse as it is to install**.
