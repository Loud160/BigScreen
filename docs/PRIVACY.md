# Privacy and network access

Big Screen has no telemetry, analytics, advertising, account service, or background upload feature. Song names, library assignments, settings, and videos remain on the headset.

Network access occurs only for user-facing video features:

- **Search YouTube** opens the Quest's normal web browser with a YouTube search containing the selected artist and song title. Browser behavior is governed by that browser and YouTube, not Big Screen.
- **URL validation/download** contacts YouTube and related media/CDN hosts through yt-dlp after the user pastes a URL, presses Download Video, or accepts a mapper-provided download.
- **Video thumbnails** request YouTube's `i.ytimg.com` artwork for recognized YouTube IDs.
- **Check yt-dlp** contacts GitHub's API and official yt-dlp release assets. Scheduled checks may run when enabled by normal mod startup policy, but updates are not installed without the player's action.
- **Big Screen Showcase** contacts BeatSaver only after **Download Map** is pressed and uses the normal YouTube path only after **Download Video** is pressed. Opening the readiness page and pressing Recheck perform no download. Each network asset therefore requires its own explicit action.

Big Screen does not accept cookies, credentials, or YouTube account logins. Videos requiring authentication cannot be downloaded and should produce a readable explanation. Mapper metadata is restricted to HTTPS YouTube-family hosts so it cannot turn the downloader into an arbitrary web fetcher.

Local data is stored under `/sdcard/ModData/com.beatgames.beatsaber/BigScreen`. Read [Troubleshooting](TROUBLESHOOTING.md) before sharing logs because filenames/song identifiers may reflect the user's library.
