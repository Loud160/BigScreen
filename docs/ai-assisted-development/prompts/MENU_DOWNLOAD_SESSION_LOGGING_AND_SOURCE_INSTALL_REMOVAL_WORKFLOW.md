# Codex Discussion Task — Menu/Download Session Logging + Source-Install Removal Workflow

## Objective

This is a **design discussion and repository analysis task only**.

I want to make several changes to Big Screen, but **do not modify any code yet**. First inspect the current implementation and discuss how these features should work, what existing systems they should integrate with, and what edge cases or problems need to be addressed.

The areas to evaluate are:

1. Detailed user-interaction logging while inside Big Screen menus.
2. Detailed download-session logging from the Beat Saber song-selection menu.
3. Retention and lifecycle of these new diagnostic logs.
4. Capturing the relevant yt-dlp output inside those logs.
5. Updating the existing log-retrieval BAT/script so it retrieves these logs.
6. Adding a one-click Big Screen removal BAT/script for installs performed by this repository's build/deploy workflow.
7. Understanding and documenting the conflict between source-deployed Big Screen installs and ModsBeforeFriday/MBF installs.

Do not implement anything until we have discussed the proposed architecture and I explicitly authorize the changes.

---

# 1. Inspect the current implementation first

Before proposing a design, inspect the current repository and determine how all of the relevant systems work today.

At minimum inspect:

- Big Screen's current logging system.
- Error logging and error-reporting paths.
- Existing log filenames and storage locations.
- Current log retrieval BAT/PowerShell scripts.
- Misc settings tab and toggle implementation.
- Main Big Screen menu lifecycle.
- Video Library/menu navigation.
- Slider, toggle, and settings-change handlers.
- Song-selection integration.
- Download button behavior.
- Resolution-selection dialog.
- Download progress reporting.
- Download cancellation.
- Download errors.
- yt-dlp integration.
- Any existing ability to capture yt-dlp stdout/stderr or plugin logging.
- Build/deploy BAT and PowerShell scripts.
- Exact files installed by the source deployment workflow.
- Exact files MBF/QMOD installs.
- Scotland2 locations used by each installation path.
- Current settings file location.
- Current video/storage locations.
- Existing uninstall or cleanup helpers, if any.

Do not base the discussion on assumptions. Report what the repository actually does today.

---

# 2. New detailed menu-session logging

I want an optional detailed diagnostic log that makes it possible to reconstruct exactly what a user did while interacting with Big Screen.

This should be controlled by a toggle in the **Misc** settings tab.

Desired setting:

```text
Detailed Menu Logging: ON
```

Requirements:

- Default should be **ON**.
- User may disable it.
- This does not replace the existing error logging system.
- Existing error logs must continue working exactly as they do now.
- These logs are intended to provide context around an error or unexpected behavior.

## Session behavior

Every time the user enters the main Big Screen mod/menu area, begin a **new menu-session log**.

The log should make it possible to reconstruct actions in chronological order.

Examples of events that should be captured:

- entering Big Screen menu;
- leaving Big Screen menu;
- opening a tab/page;
- switching between tabs/pages;
- selecting different songs/maps where applicable;
- moving sliders;
- changing toggle switches;
- changing dropdown/selectable settings;
- changing layouts;
- opening dialogs;
- accepting/canceling dialogs;
- opening the Video Library;
- selecting a video;
- previewing/playing a video;
- stopping a preview;
- assigning a video;
- removing an assignment;
- starting a download;
- canceling a download;
- settings values before/after meaningful changes where useful;
- any errors encountered during the session.

For changes such as sliders, the log should contain enough information to know:

```text
timestamp
control/setting name
previous value
new value
```

However, consider whether logging every tiny slider movement would create excessive noise.

Discuss whether slider changes should:

- log every event;
- log at a reasonable throttle interval;
- log only the final value when interaction ends;
- or log both beginning/final values.

Recommend the most useful approach for reproducing user behavior without generating useless amounts of data.

---

# 3. Log context

Each menu-session log should begin with useful diagnostic context.

Determine what is practical and valuable to include, such as:

```text
Big Screen version
Beat Saber version
Quest model
Quest OS/version if available
Scotland2 version
relevant dependency versions
current settings
active layout
available storage
selected video/map where applicable
session start timestamp
```

Do not dump excessive or sensitive information unnecessarily.

The purpose is debugging Big Screen.

---

# 4. Error handling inside menu-session logs

If an error occurs while the user is in one of these logged sessions:

- continue using the existing normal Big Screen error-log mechanism;
- also write a concise representation of the error into the active interaction/session log.

The interaction log should provide the sequence that led to the error.

Example:

```text
21:15:03.221 Opened Video Library
21:15:05.102 Selected "Example Song"
21:15:06.444 Clicked Download
21:15:07.015 Resolution dialog opened
21:15:09.320 Selected 1080p
21:15:09.442 Download started
21:15:12.771 yt-dlp: ...
21:15:14.109 ERROR: ...
```

Do not duplicate massive stack traces unnecessarily if the existing error file already contains them.

Consider logging a correlation ID or error-log filename so the detailed session log can point directly to the corresponding full error report.

Discuss whether that would be useful.

---

# 5. Song-selection download-session logging

I also want the same kind of detailed logging when Big Screen offers a video download from Beat Saber's normal song-selection menu.

This is separate from entering Big Screen's settings/menu.

A download-session log should begin when the user clicks Big Screen's **Download** button for a map.

The first entry should record that the user clicked Download.

Then record the entire interaction.

At minimum:

```text
song/map identity
level ID/hash where available
video URL
download button clicked
resolution dialog opened
resolution selected
resolution dialog canceled
download started
download progress
download canceled
download completed
download failed
error details/reference
final output path
```

If the user cancels from the resolution-selection dialog, record that.

If the user starts the download and later cancels it, record that separately.

---

# 6. Download progress logging

Discuss how detailed download progress should be.

I want enough information to understand what happened during a download without creating thousands of redundant log lines.

Potential useful information:

```text
percentage
downloaded bytes
total bytes
current transfer speed
ETA
download stage
post-processing stage
output filename
```

Determine whether progress should be recorded:

- for every progress callback;
- at percentage boundaries;
- every X seconds;
- only when meaningful values change.

Recommend an approach.

---

# 7. Video URL logging

Both menu-session downloads and song-menu downloads should record the complete video URL being acted upon.

Determine whether Big Screen currently distinguishes:

```text
configured/source URL
resolved yt-dlp URL
final media URL
```

and recommend which should be logged.

The user-facing/source URL should definitely be recorded.

Do not log authentication tokens, cookies, secrets, or temporary signed URLs if doing so creates a security/privacy concern.

Discuss this if relevant to the current implementation.

---

# 8. yt-dlp logging

I would like the detailed session/download logs to contain the **full yt-dlp operational output** for that particular download if this can be done cleanly.

Inspect how yt-dlp currently runs.

Determine:

- what logging/output is available;
- whether stdout/stderr are captured;
- whether embedded Python/yt-dlp has a logger callback;
- whether Big Screen already receives progress/events separately;
- whether complete yt-dlp output can be routed into the active session log without interfering with existing behavior.

The ideal result would make it possible to inspect:

```text
Big Screen actions
+
download progress
+
yt-dlp output
+
Big Screen errors
```

in one chronological session file.

If full yt-dlp output would be excessively verbose or unsafe, explain the tradeoff and recommend an alternative.

---

# 9. Log lifecycle

I do **not** want these log files left permanently open.

Discuss the best lifetime model.

For Big Screen menu sessions, likely behavior is:

```text
user enters Big Screen menu
→ create log
→ record interaction
→ user leaves menu
→ flush/close log
```

But inspect the actual menu lifecycle and determine whether there are reliable enter/exit signals.

Potential problems to consider:

- scene changes;
- menu object destruction;
- game shutdown;
- Beat Saber crash;
- mod reload/re-entry;
- multiple menu opens in the same Beat Saber process;
- user entering another nested Big Screen page;
- errors during shutdown.

For song-selection download logs:

```text
Download clicked
→ create download-session log
→ log interaction/download
→ completion/cancel/failure
→ flush/close
```

If the user starts a download and then changes scenes or exits Beat Saber, determine how the log should be safely flushed/closed.

Use RAII/scoped ownership or another reliable lifecycle mechanism rather than scattered manual file-open/file-close calls if appropriate.

Discuss the safest architecture.

---

# 10. Log retention

These detailed session logs should not accumulate indefinitely.

Desired behavior:

**Keep only the most recent 10 menu/download diagnostic session logs.**

Discuss whether this should mean:

### Option A

10 total diagnostic session logs across both types.

### Option B

10 Big Screen menu logs + 10 song download logs.

Recommend whichever makes the most sense.

Rotation should occur automatically.

Do not delete:

- normal Big Screen error logs;
- current diagnostic session;
- unrelated logs;
- user files.

Filename format should make sessions easy to identify chronologically.

For example:

```text
BigScreen-Menu-2026-08-18-215503.log
BigScreen-Download-2026-08-18-220017.log
```

Use whatever naming convention fits the project best.

---

# 11. Concurrency/thread safety

Inspect what threads currently generate:

- menu events;
- downloader events;
- yt-dlp output;
- error events.

The logging design must be thread safe.

Do not allow:

- multiple threads writing unsafely to the same stream;
- log corruption;
- shutdown while another thread is writing;
- deadlocks caused by logging while holding important Big Screen locks;
- logging failures to crash Beat Saber.

Discuss whether the session logger should use:

- a mutex;
- a queue + dedicated writer;
- immediate synchronized writes;
- another existing logging primitive.

Prefer the simplest design that is safe and low overhead.

---

# 12. Logging must fail safely

Detailed diagnostic logging is optional instrumentation.

Failure to:

- create a log;
- write a log;
- rotate logs;
- flush;
- close;
- capture yt-dlp output

must **never break Big Screen or Beat Saber**.

Discuss how failures should be handled.

Ideally:

```text
try logging
↓
if logging fails
    disable session logging for that session
    use existing error mechanism if safe
    continue normal operation
```

Avoid recursive logging failures.

---

# 13. Existing log retrieval BAT/script

Inspect the repository's current log-retrieval workflow.

Modify the future design so that when the user runs the existing log retrieval BAT/script, it will also retrieve these new diagnostic session logs.

Do not implement yet.

Discuss:

- source Quest directory;
- destination structure on PC;
- whether menu and download logs should go into separate folders;
- whether only the latest logs or all retained logs should be retrieved;
- whether filenames already contain enough timestamp/context.

The retrieval process should still retrieve the existing logs exactly as it does now.

---

# 14. Source-installed mod vs ModsBeforeFriday problem

I discovered an installation-management problem.

When Big Screen is installed using the BAT/deploy workflow in this repository, **ModsBeforeFriday does not recognize Big Screen as installed**.

If the user later installs Big Screen using MBF, it does not properly replace/remove the source-deployed copy.

The copy installed using the repository BAT remains the version Beat Saber loads.

Inspect exactly why this occurs.

Determine:

- where `copy.ps1` places Big Screen;
- where MBF places Big Screen;
- whether one is in `early_mods` and another is elsewhere;
- how Scotland2 resolves duplicates;
- why the source-deployed copy wins;
- whether MBF's package tracking only recognizes files it installed;
- whether source deployment can be made MBF-compatible;
- whether installing a proper generated QMOD through MBF/QuestPatcher instead of manually copying would solve this;
- whether there is a safe way for the source deploy script to clean existing MBF versions;
- whether MBF can detect a manually copied mod at all.

Do not guess.

Trace the exact install paths and load behavior.

---

# 15. Determine whether source deployment should change

Discuss whether the repository's one-click Build & Deploy workflow should continue directly copying files or instead do something closer to:

```text
build QMOD
→ install generated QMOD using a supported mod-manager/install mechanism
```

if that would make the resulting installation visible to MBF.

However, do not change the workflow just for theoretical cleanliness.

Compare:

### Current direct-copy deployment

Advantages / disadvantages.

### QMOD-based source deployment

Advantages / disadvantages.

Consider:

- build speed;
- developer iteration speed;
- dependency handling;
- MBF visibility;
- Scotland2 behavior;
- reliability;
- removal;
- compatibility with existing workflow.

---

# 16. Add a source-install removal workflow

Regardless of whether the install workflow eventually changes, I want a simple removal BAT/script for Big Screen builds installed using this repository.

Proposed entry point:

```text
Remove-BigScreen.bat
```

or another name consistent with the existing BAT files.

The removal workflow should be as easy to use as:

- Build & Deploy
- Retrieve Logs
- Remove Big Screen

Do not implement it yet.

Design what it should remove based on the actual current installation layout.

---

# 17. Removal confirmation

The removal script must ask for explicit confirmation before deleting the mod.

Example:

```text
Big Screen will be removed from the connected Quest.

Continue? [Y/N]
```

If the user declines:

```text
No files are changed.
```

---

# 18. Settings-file removal

After confirming removal of the mod itself, ask separately whether the user wants to remove Big Screen's settings/configuration file.

Example:

```text
Remove Big Screen settings as well? [Y/N]
```

This must be a separate choice.

Default behavior should preferably preserve settings unless there is a strong reason otherwise.

Discuss the safest default.

---

# 19. Videos must never be removed by the uninstall script

The script must clearly tell the user:

> Removing Big Screen does not remove videos downloaded or installed using Big Screen.

And if the user chooses to remove the settings file:

> Removing Big Screen settings also does not remove downloaded videos.

Inspect current storage paths and ensure the removal plan cannot accidentally include:

- downloaded videos;
- map videos;
- Video Library media;
- user choreography files;
- other user-created content.

This separation must be explicit and tested.

---

# 20. What exactly should the removal script delete?

Determine the exact set of files installed by the current Build & Deploy workflow.

The eventual uninstall script should remove only Big Screen-owned deployment artifacts.

Potential categories:

```text
libbigscreen.so
Big Screen private libraries
Big Screen QMOD runtime files
Big Screen-owned dependency/runtime assets
temporary install artifacts
```

Be careful around shared dependencies.

Do **not** remove:

- Scotland2;
- beatsaber-hook;
- BSML;
- SongCore;
- CustomTypes;
- other mods;
- shared libraries needed by other mods

unless a particular file is provably private to Big Screen.

List precisely what is safe to remove.

---

# 21. ADB/device safety

The removal workflow should:

- verify ADB exists;
- verify exactly one appropriate Quest is connected/authorized or handle multiple-device selection safely;
- verify Big Screen paths before deletion;
- refuse dangerous/empty paths;
- never execute broad wildcard deletes over shared mod directories;
- report every relevant action;
- return a non-zero exit code on failure;
- remain open when launched by double-click so the user can read the result.

Follow the defensive style of the existing repository scripts.

---

# 22. MBF transition guidance

If there is no reliable way to make MBF recognize the current source-installed copy, determine whether the correct user workflow should be:

```text
1. Run Remove-BigScreen.bat
2. Confirm removal
3. Open MBF
4. Install Big Screen normally
```

If so, make that explicit.

Potentially the removal script could display:

```text
Big Screen source installation removed.

You may now install the QMOD version using ModsBeforeFriday.
```

Do not automate MBF unless there is a supported, reliable way to do so.

---

# 23. Tests that would be needed

Discuss what tests/invariants should eventually be added.

Potential logging tests:

- logging toggle defaults ON;
- logging disabled means no session files;
- menu entry creates exactly one new session;
- leaving menu closes the session;
- values are recorded correctly;
- errors appear in both appropriate logging systems;
- yt-dlp output is associated with the correct download;
- cancel actions are logged;
- rotation keeps the requested number of logs;
- logging failures do not break UI/download behavior;
- concurrent events do not corrupt files.

Potential removal-script tests:

- correct files targeted;
- videos never targeted;
- settings removed only after separate confirmation;
- shared dependencies never removed;
- cancellation changes nothing;
- missing mod produces a clean result;
- ADB/device failures are handled;
- no unsafe wildcard deletion.

Potential repository invariant checks:

- source deploy and source uninstall remain symmetrical;
- every file copied by source deployment is either removed by uninstall or deliberately preserved with documentation.

---

# 24. Deliverable

Do not write code.

Return a technical discussion with these sections:

## Current logging architecture

Explain how Big Screen currently logs menu actions, downloads, yt-dlp activity, and errors.

## Proposed diagnostic-session logging architecture

Explain:

- session lifetime;
- file ownership;
- thread safety;
- event format;
- error correlation;
- yt-dlp integration;
- retention/rotation.

## Menu-event coverage

List which actions can reliably be logged today and any that would require deeper changes.

## Download-event coverage

Explain exactly how the song-menu download sequence can be reconstructed.

## yt-dlp feasibility

State whether complete yt-dlp output can be captured and how.

## Log lifecycle risks

Identify potential cases where a file could remain open or a session could terminate unexpectedly, and recommend how to handle them.

## Log retrieval changes

Explain what the existing BAT/script would need to retrieve.

## Source-deploy vs MBF analysis

Explain exactly why MBF fails to recognize/replace the source-installed version and whether the install workflow should change.

## Removal BAT design

List exactly what should be removed, what should be preserved, and how the user confirmation flow should work.

## Risks / edge cases

Rank meaningful concerns:

```text
Critical
High
Medium
Low
```

## Recommended implementation order

Suggest the safest sequence for implementing these features after I approve the design.

---

# Important rules

- **Do not modify code.**
- **Do not create scripts yet.**
- **Do not change logging behavior yet.**
- **Do not change the Build & Deploy workflow yet.**
- Inspect the current implementation before making recommendations.
- Preserve the existing error logging system.
- Detailed logging must be optional and default ON.
- Detailed logging must never be allowed to crash or interfere with Big Screen.
- Do not allow diagnostic logs to grow without retention limits.
- Do not remove downloaded videos during uninstall.
- Do not remove shared Quest mod dependencies.
- Treat source deployment and source removal as symmetrical operations.
- Challenge any part of this proposed design if the current architecture suggests a safer or simpler solution.

The goal of this discussion is to determine the cleanest way to make Big Screen support **reproducible user-session diagnostics** and a **safe one-click source uninstall workflow** before any implementation begins.
