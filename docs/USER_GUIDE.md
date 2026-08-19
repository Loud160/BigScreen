# Big Screen user guide

## Finding a song

Open **Mods > Big Screen > Video Library**. The browser counts songs once, not once per difficulty. Use Search Maps, the alphabetical jump list, normal scroll bar, or the filter selector for All Maps, Custom Maps, WIP Maps, and OST/DLC maps. A thumbnail appears beside a song only when Big Screen knows the assigned video's YouTube artwork; album covers are intentionally not substituted.

Selecting a song opens its editor. **Back to Song List** returns to the same filter, jump position, order, and song identity.

The editor's **Refresh** button reloads that song's map-folder Cinema JSON and
rebuilds the one-per-session Cinema playlist index. Use it after copying an
edited metadata or playlist file back to the Quest. Big Screen does not poll
those files during preview or gameplay.

## YouTube search and download

**Search YouTube** launches the Quest browser with the selected song and artist already entered. Return to Beat Saber, copy/paste a result from the Quest clipboard, and confirm the fetched thumbnail. Both normal `https://www.youtube.com/watch?...` links and `https://youtu.be/...` share links are accepted. HTTP, lookalike domains, and arbitrary mapper URLs are rejected.

After the URL probe succeeds, the per-song editor shows one compact button for every compatible source tier the video actually provides: **480p**, **720p**, **1080p**, and/or **1440p**. The choices stay in one row beside the full-size video thumbnail. A source with none of those tiers instead offers its single best lower resolution, such as 360p. The selected tier is the native resolution Big Screen will play, so choose the balance of clarity and Quest workload you want before downloading. The UI then shows preparation, byte/percentage progress, speed/ETA when known, completion, cancellation, or the actual failure. Clearing the URL before assignment also clears its thumbnail.

If a mapper supplied a URL but not the video, the normal song-detail screen offers **Download Video**. It opens a resolution dialog, checks the URL when necessary, and lists the same available tiers before downloading into the same library. Private/login-required, deleted, age/parental-control-restricted, 403/404, rate-limited, and temporary server failures are translated into readable explanations and recovery suggestions. Big Screen has no YouTube login and does not consume browser cookies.

Only one downloaded source is assigned per song. Selecting another tier explains what will be replaced and leaves the current video untouched until the new transfer and library commit succeed. Replacing a browser-selected local file changes only the assignment; Big Screen never deletes that user-owned file. A 1440p choice warns that software decoding is unsupported and playback will stop safely, without stopping the map, if hardware decoding fails.

After completion, the synchronized video/map-audio preview starts automatically so timing can be checked.

## Local video files

Select a song and choose **Show File Browser** in its child editor. Custom and WIP songs initially open their own map folder. OST, DLC, and other built-in songs initially open Big Screen's automatically created import folder:

`/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Video Import`

The center-screen browser uses a translucent dark background for legibility and can navigate through any readable folder under Quest shared storage. **Back One Folder** moves up one level; every tier in the path breadcrumb is also clickable for jumping several levels at once. Folders are listed first. Compatible videos are green; select one and then choose **Set Video**. The Set button remains disabled until a compatible file is selected. An incompatible file remains visible in red with **HELP**, which explains the detected codec, resolution, container, HDR/bit-depth, or incomplete-file problem. **Cancel** closes the browser without changing the current assignment. Accepted local files are 8-bit SDR H.264/H.265 MP4 and VP8/VP9 WebM up to 1440p. H.265 and content above 1080p require hardware decoding. The file need not be renamed.

The selected file is referenced in place; it is not copied into Big Screen storage. Assigning a YouTube download replaces the active local registration, and assigning a local file replaces the active download registration; timing controls remain available for either source. The side editor shows the active local filename but keeps directory browsing on the wider center screen.

**Remove Video** stops preview playback first and opens three choices. **Unlink** removes only the map assignment and keeps the physical file on the Quest. **Delete File** permanently removes a selected local MP4/WebM, while **Delete Video** permanently removes a Big Screen-managed download. Map-authored local-file assignments can also be unlinked without editing the map; that opt-out is saved in `library.json`. Unlinked files may be reassigned through **Show File Browser**, and unlinked managed downloads are also discoverable by Storage Maintenance.

## Synchronizing video and song

The playback group starts/stops both the map audio and video. Drag the scrubber to seek; the centered time shows current position and duration. A completed video can be scrubbed backward and played again. Long-running previews retain compressed packets while MediaCodec is busy and fully drain asynchronous final frames before looping, preventing intermittent freezes near the end.

The song editor keeps the full-height timing controls, **Playback Position**, and **Video Storage** visible together without a nested scroll area. The storage row shows the selected map's managed download, any active local-video size, all Big Screen downloads, free Quest space, and the video removal action.

- **Video Playback Offset** moves the video relative to song time. Negative values create time before video frame zero.
- **Playback Speed** manually adjusts video rate.
- **Fit to Song** continuously accounts for map duration and offset so the playable video ends with the song; it can handle a source longer or shorter than the song.
- **Lead-In Background** shows negative-time lead-in as solid black when on. Off leaves the screen transparent/hidden until video begins.

Map completion never waits for a longer video; Beat Saber ends the map normally.

## Screen layouts and mapper overrides

The Screen tab stores five independent layouts. Select the layout being edited, then adjust its basic placement, size, tilt, curve, and video opacity. Each layout has its own Advanced Screen Controls switch. Enabling it adds per-layout letterbox transparency, independent picture rotation, zoom, pan, perspective tilt, stretch/letterbox behavior, screen rotation, and controller-based undocked placement. The Screen Layout control on song selection changes the active global layout. Pause-menu video controls are not currently exposed on Beat Saber 1.40.8; use the song-selection controls before starting the map.

Flat and curved layouts both scale from 0.5x through 8.0x. Enlarging a curved screen changes its physical dimensions without increasing its segment count or the decoded video resolution.

For layouts that extend below the menu's visible floor, turn off **General > Show Menu Environment**. The same switch removes the menu scenery, lighting, and floor while preserving Big Screen's UI, preview screen, and controller input. **Show Lane Guides** independently adds thin lane rails, depth marks, and a player-origin marker extending to the default back wall. All menu visuals return to their normal state when Big Screen closes, loses focus, is disabled, or the environment switch is restored. These are menu placement aids only and do not alter gameplay environments.

Respect Mapper Settings and Allow Chroma Override are enabled by default but own different things. Respect Mapper Settings applies Cinema-authored screen geometry, image effects, additional screens, and explicit Cinema environment instructions. Allow Chroma Override preserves the broader gameplay environment only when the map is actually detected as using Chroma. Turn Respect Mapper Settings off to keep mapper media/timing while using the selected Big Screen screen layout; turn Allow Chroma Override off to use Big Screen's environment options. URL/timing-only Cinema metadata never claims the screen canvas.

## Performance

480p and 720p reduce conversion and texture-upload cost. 1440p is hardware-only. Big Screen plays the selected local or downloaded file at its native presentation-oriented resolution; changing resolution therefore means selecting or downloading a different source file. The 15/30/60 FPS setting is a maximum, so sources below it are not duplicated. Automatic Performance runs throughout gameplay and Video Library previews. Sustained loss for the selected attack time lowers only the effective FPS limit by the configured step, down to a 15 FPS floor. Sustained healthy playback for the release time restores the exact preceding limit in reverse order. Optional oscillation prevention stops upward recovery after the same recovery repeatedly proves unstable, while still allowing further reductions. The controller accounts for Fit to Song playback rate, never reopens the decoder or changes resolution, and starts the next playback session from the saved FPS preference. Because this feature is experimental and still under development, turning it on requires confirmation before the setting is saved.

**Misc > Performance > Use FFmpeg 9** provides an in-game A/B switch between the bundled 9.0.1 decoder (on, the default) and the 4.4.8 compatibility decoder (off). The same native-resolution file, screen settings, and reusable-buffer path are used for both. An active Video Library preview restarts at the retained position; gameplay adopts the choice when the next map begins. Use the performance overlay or results summary to confirm which runtime actually ran, then compare repeated plays under the same headset temperature, charge, graphics, and map conditions.

**Misc > Performance > Hardware Video Decoding** defaults on. Big Screen asks the selected FFmpeg runtime to decode H.264, H.265/HEVC, VP8, or VP9 through the Quest's MediaCodec service. Decoded YUV frames still pass through Big Screen's established RGBA conversion and Unity texture upload, so this is hardware decoding rather than a zero-copy rendering pipeline. H.264, VP8, and VP9 at 1080p or below may fall back to software; HEVC and content above 1080p are hardware-only and stop video safely if MediaCodec fails. Turn the switch off to force supported formats through software decoding. The performance panel, results screen, and benchmark CSVs report the active backend after any fallback.

Show Performance Information displays the actual presentation-oriented video resolution, source FPS, current FPS limit, frames skipped, decode delay, and automatic reductions during gameplay and on the results/failure screen. Frames Skipped is a cumulative count of presentation deadlines missed during the current playback session, so a later decoder catch-up cannot make the value decrease. Automatic Performance separately evaluates a bounded recent window, allowing it to recognize recovery without rewriting that session total. The movable panel shares one saved position and angle between the Video Library and gameplay; placement is committed when the panel is disabled or its current menu/gameplay context closes. Use the adjacent circular reset button if the panel is moved out of reach.

When performance logging is enabled, the completed-session log also includes decoder startup time. It is kept in the log rather than the live panel because startup occurs only once when that playback session opens.

Record Power Benchmark creates two analysis files under `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs`: `power-benchmark-summary.csv` has one row per map, while `power-benchmark-samples.csv` has the one-second observations. It records baseline maps even when Video In Map is off, so compare otherwise identical runs by the `video_active` column; `decode_method` identifies hardware, software, or no active video decoder. Whole-process CPU includes Beat Saber and every loaded mod; `decoder_cpu_ms` isolates Big Screen's owned decoder worker, including YUV-to-RGBA conversion. Main-thread texture upload and screen rendering remain part of the whole-process difference rather than being falsely attributed to H.264 decoding. If the CSV schema changes, Big Screen preserves the older file with a timestamped `-legacy-` name before starting the new schema.

## Misc: storage and performance

The selected-song storage row distinguishes the current map's managed download, local-file bytes when local files exist, total Big Screen video-library usage, and free Quest storage. Values under 1 GB are displayed in MB.

The Misc tab contains Storage, Showcase, and Performance sections. Manage Storage opens a centered review page and stops any active library preview. Scan Storage lists only Big Screen-owned orphan downloads, unused thumbnails, or abandoned temporary files. Every candidate starts checked; uncheck anything you want to keep, then clean only the selected files after confirmation. Assigned downloads, required runtime files, map-folder MP4s, and Video Import MP4s are protected. The Performance section contains the decoder switch, Automatic Performance and its tuning controls, the optional playback-information overlay, and the power benchmark recorder.

**Play Big Screen Showcase** opens an optional center-screen demonstration manager. It lists Chroma, Noodle Extensions, the pinned BeatSaver map, its video, and downloader readiness. Nothing downloads on page open: use **Download Map**, **Recheck Map**, or **Download Video** beside the missing item. **Play Showcase** enables only when everything is ready, then shows the strong-motion warning and starts Lawless Expert+ directly with session-only No Fail. This temporary modifier does not change saved modifiers or affect the map when launched normally. The map is stored in Big Screen's managed `DemoLevels` folder rather than copied over a user's normal custom-map installation. After the run, Beat Saber's ordinary results and navigation controls remain unchanged; reopen Big Screen from Mods to run the showcase again. User video overrides still take priority if the same map is already present.

## Update status

The Update tab shows the installed Big Screen version and the yt-dlp package currently active in the embedded downloader. Big Screen checks the latest public stable mod release once when its menu first opens during each Beat Saber session. A newer version produces one popup showing the installed and available versions; reopening the menu does not repeat that automatic check. **Check Big Screen Update** always performs a fresh manual check and reports whether an update exists, the installed version is current, or the check could not be completed. Mod updates remain user-installed through ModsBeforeFriday or GitHub rather than being silently installed in-game.
