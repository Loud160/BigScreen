# Troubleshooting

Big Screen logs to Beat Saber's standard mod log folder:

`/sdcard/ModData/com.beatgames.beatsaber/logs`

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

If the video library JSON is damaged, Big Screen quarantines it and restores the
newest of two known-good backups. If neither backup is usable, it reconstructs
managed downloads from installed song IDs and deterministic filenames. Timing
uses safe defaults when the damaged metadata cannot be recovered.
