# Troubleshooting

## Switching one build checkout between Windows and Linux

If the same source checkout is built from both Windows and native Linux, use
the normal launcher each time. Windows builds inside WSL, so both supported
hosts use the Linux x86-64 NDK, but QPM still generates links containing the
active Linux/WSL checkout and cache paths. The bootstrap detects a mismatch,
replaces only ignored QPM output, and rewrites `ndkpath.txt` before restore. Do
not manually copy `extern/` or an NDK path between environments. If a build was
interrupted during that transition, rerun the normal build launcher.

## Build QMOD versus Build and Deploy

Use `Build-QMOD.bat` on Windows or `Build-QMOD-Linux.sh` on Linux to create the
complete installer for MBF or SideQuest. These build-only launchers never start
ADB, inspect a headset, or install the mod.

Use `Build-And-Deploy.bat` or `Build-And-Deploy-Linux.sh` only for a direct
source-managed development install. That path builds the same QMOD first, then
uses ADB and refuses to overlap a Big Screen package registered by a QMOD
manager. If deployment reports a registered package, remove it through the
manager before trying the source deployer again.

`Remove-BigScreen.bat` and `Remove-BigScreen-Linux.sh` remove source-managed
Big Screen files even if their installed hashes have changed. They preserve
shared dependencies, maps, logs, and library data, then ask separately whether
to remove settings and Big Screen-managed downloaded videos; both optional
answers default to No.

## Collecting a support bundle

On Windows, double-click **`Collect-BigScreen-Logs.bat`** in the repository or
source archive. On Linux, run `./Collect-BigScreen-Logs-Linux.sh`. Enter
approximately how many minutes ago the problem occurred; pressing Enter uses
30 minutes. Both launch the same collector, which finds ADB automatically
through platform-tools or the Android SDK; the Windows path also detects
SideQuest and QPM copies. No ADB commands are required.

An authorized phone or tablet connected at the same time is ignored. The
collector verifies that its target identifies as a Meta/Oculus Quest and has
Beat Saber installed. If multiple matching Quests are connected, it lists
their model names and serial numbers and asks which numbered headset should be
used before reading any logs.

The resulting `BigScreen-Support-<date>-<time>.zip` is saved beneath
`BigScreen Support Logs`. Send the complete ZIP when reporting a problem. Its
`REPORT.txt` separates each source into:

- **FRESH** — timestamped inside the selected incident window;
- **OLDER CONTEXT** — useful for comparison, but not evidence of this crash;
- **NOT FOUND** — the Quest had no usable record from that layer.

This distinction matters because Big Screen, Beat Saber, and Android do not
always fail together. Big Screen also opens its persistent history on every
startup, so the collector evaluates the newest timestamped error inside that
file rather than incorrectly treating its file-modification time as a crash.
Android process-exit records distinguish signaled/crash exits from ordinary
force-closes, and app tombstones are discovered dynamically rather than by a
fixed filename.

The collector is read-only. It does not stop Beat Saber, clear logcat, change
settings, or alter files on the Quest. Logs can contain song/map names, file
paths, video URLs, or usernames; review the extracted text before posting the
ZIP publicly.

With **Misc > Detailed Diagnostic Logging** enabled, the support ZIP also
contains `Sessions/Menu` and `Sessions/Download`. Big Screen keeps ten files in
each headset folder. Menu actions, settled slider changes, previews, download
choices, five-percent progress points, cancellation, terminal results, and
concise error correlation IDs are recorded as JSONL. Full errors remain in
`error-history.log` under the same ID. Missing session folders are normal when
the option was off and do not stop the rest of the collection.

If the collector had to start ADB, it stops that daemon automatically when
finished. If ADB was already running, it asks whether to stop it and defaults
to **No** after five minutes. This prevents the collector from silently ending
another tool's established ADB session while still giving nontechnical users a
one-key way to free the Quest for ModsBeforeFriday.

When no existing ADB installation can be found, the platform launcher offers
to download the pinned Google Android SDK Platform Tools 37.0.0 archive for
Windows (approximately 7.8 MB) or Linux (approximately 8.7 MB). It discloses
the official URL, Google SDK terms, and exact destination before asking. No
response for five minutes defaults to **No**. Approved archives are SHA-256
checked, and Windows additionally requires a valid Google LLC signature on the
extracted `adb.exe`. Nothing is installed system-wide: the files live under
`BigScreen Tools` beside the launchers and can be removed by deleting that
folder. Download progress and every verification/extraction stage remain
visible so slow machines or connections do not appear to have stalled.

The first-party logger comparison writes Big Screen's current and previous
general logs to:

- `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/bigscreen-native.log`
- `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/bigscreen-native.previous.log`

Development builds currently also retain Paper2 output under Beat Saber's
standard mod log folders so both backends can be compared. The support
collector pulls the Big Screen-owned files first, then any Paper2 candidates
and Android logcat. Users should run the collector rather than manually decide
which one file is relevant.

Power benchmark CSV files are separate from the normal mod log:

- `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/power-benchmark-summary.csv`
- `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/power-benchmark-samples.csv`

If current, charge, energy, or capacity cells are blank, Android reported that
specific fuel-gauge property as unsupported. Do not replace a blank with zero.
Runs made while USB or a battery pack is connected are useful for CPU/decode
analysis but not for headset-drain comparisons. Instantaneous current is noisy;
compare repeated full-map charge consumption and the summary average rather
than drawing a conclusion from one sample.

Expected video/content errors are shown in the menu and do not count against the
mod's safety circuit. If two internal Big Screen errors occur within
three minutes, the mod turns its own Enabled switch off, queues one explanatory
dialog, and leaves Beat Saber/map playback running. Re-enable it from General
after reviewing the log.

YouTube dialogs identify common causes such as private, removed,
age-restricted, members-only, region-blocked, administrator-restricted,
rate-limited, network, storage, format, and downloader-compatibility failures.
The short support code in the dialog matches the detailed entry in
`error-history.log`; the raw yt-dlp output is kept in the log instead of being
placed in an oversized headset popup.

Inside Big Screen, a popup remains attached to the left, right, or center panel
whose action opened it. Its visible surface and input blocker are moved back to
the front after later menu refreshes, so another control cannot cover its
buttons while the dialog is open. All tracked popups are dismissed when Big
Screen closes so a stale invisible blocker cannot return on the next visit.

Outside Big Screen's own menu, error dialogs wait for the currently visible Beat
Saber flow to finish transitioning, then open in front of that flow. If the user
changes screens while a dialog is open, the message is requeued for the next
stable screen rather than leaving an invisible input blocker behind the UI.

During a map, playback failures are logged silently and any user-facing notice
is deferred until gameplay has ended. This prevents a dialog from interrupting
normal play or Replay capture.

If a 1440p video reports that hardware decoding is required, enable **Hardware
Video Decoding** or download a 1080p-or-lower tier. Big Screen never attempts a
software decode above 1080p. HDR/10-bit files must be re-exported as 8-bit SDR;
WebM alpha is also unsupported. These failures stop only video playback and do
not interrupt the map.

Big Screen does not require a separately installed JavaScript runtime. If a log
mentions a missing JavaScript runtime, confirm the active QMOD contains the
current `libbigscreen.so` and `Runtime/bigscreen_jsc_provider.py`. A successful
downloader initialization log names the bundled QuickJS-NG version. An
incompatible yt-dlp update is rejected automatically and the prior downloader
is restored on the next Beat Saber start.

If three YouTube downloads fail consecutively, Big Screen performs a background
yt-dlp release check before showing guidance. A found update is offered as a
possible compatibility fix. If no update is available, check again later from
the Update tab; YouTube sometimes changes video delivery before a stable yt-dlp
fix is published, and the optional nightly channel may receive that fix first.
The notice does not mean every failed URL is an updater problem—private,
restricted, removed, or region-limited videos can still fail independently.

Some YouTube clients provide an H.264 tier as fragmented HLS/MPEG-TS rather
than a normal MP4. After the transfer, **Preparing video for playback** copies
that stream into a seek-safe MP4 without re-encoding, so the picture quality is
unchanged. This temporarily requires room for both files. If preparation fails
but the original produces a software-decoded test frame, Big Screen assigns it,
records `BS-DL-PREP-SW-001`, and warns the player; hardware decoding is
unavailable for that file and software playback may reduce video or gameplay frame rate. A
`BS-DL-PREP-001` failure means neither a validated MP4 nor a verified software
fallback could be published, so the previous assignment remains unchanged.

If the video library JSON is damaged, Big Screen quarantines it and restores the
newest of two known-good backups. If neither backup is usable, it reconstructs
managed downloads from installed song IDs and deterministic filenames. Timing
uses safe defaults when the damaged metadata cannot be recovered.
