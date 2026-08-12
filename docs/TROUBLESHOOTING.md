# Troubleshooting

Big Screen logs to Beat Saber's standard mod log folder:

`/sdcard/ModData/com.beatgames.beatsaber/logs`

Expected video/content errors are shown in the menu and do not count against the
mod's safety circuit. If two internal Big Screen errors occur within
three minutes, the mod turns its own Enabled switch off, queues one explanatory
dialog, and leaves Beat Saber/map playback running. Re-enable it from General
after reviewing the log.

During a map, playback failures are logged silently and any user-facing notice
is deferred until gameplay has ended. This prevents a dialog from interrupting
normal play or Replay capture.

Big Screen does not require a separately installed JavaScript runtime. If a log
mentions a missing JavaScript runtime, confirm the active QMOD contains the
current `libbigscreen.so` and `Runtime/bigscreen_jsc_provider.py`. A successful
downloader initialization log names the bundled QuickJS-NG version. An
incompatible yt-dlp update is rejected automatically and the prior downloader
is restored on the next Beat Saber start.

If the video library JSON is damaged, Big Screen quarantines it and restores the
newest of two known-good backups. If neither backup is usable, it reconstructs
managed downloads from installed song IDs and deterministic filenames. Timing
uses safe defaults when the damaged metadata cannot be recovered.
