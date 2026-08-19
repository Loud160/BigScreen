# Privacy and network access

Big Screen has no telemetry, analytics, advertising, account service, or background upload feature. Song names, library assignments, settings, and videos remain on the headset.

Network access occurs only for user-facing video features:

- **Search YouTube** opens the Quest's normal web browser with a YouTube search containing the selected artist and song title. Browser behavior is governed by that browser and YouTube, not Big Screen.
- **URL validation/download** contacts YouTube and related media/CDN hosts through yt-dlp after the user pastes a URL, presses Download Video, or accepts a mapper-provided download.
- **Video thumbnails** request YouTube's `i.ytimg.com` artwork for recognized YouTube IDs.
- **Check yt-dlp** contacts GitHub's API for official stable/nightly release metadata once when Big Screen first opens in a game session, when the player manually checks, or after three consecutive YouTube download failures. Release assets are downloaded only after the player accepts an offered update. Stable users are never automatically switched to nightly.
- **Big Screen Showcase** contacts BeatSaver only after **Download Map** is pressed and uses the normal YouTube path only after **Download Video** is pressed. Opening the readiness page and pressing Recheck perform no download. Each network asset therefore requires its own explicit action.

Big Screen does not accept cookies, credentials, or YouTube account logins. Videos requiring authentication cannot be downloaded and should produce a readable explanation. Mapper metadata is restricted to HTTPS YouTube-family hosts so it cannot turn the downloader into an arbitrary web fetcher.

Local data is stored under `/sdcard/ModData/com.beatgames.beatsaber/BigScreen`. Read [Troubleshooting](TROUBLESHOOTING.md) before sharing logs because filenames/song identifiers may reflect the user's library.

Detailed diagnostic sessions are enabled by default and remain local until a
user runs the support collector. They may contain song/map names, selected
settings, local filenames, and the original user-facing YouTube URL. Big Screen
sanitizes yt-dlp operational messages before writing them: cookies,
Authorization values, PO tokens, signed media-query strings, and comparable
temporary secrets are redacted or omitted. The setting can be disabled in
Misc without disabling the persistent error history.
