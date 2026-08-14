# Big Screen user guide

## Finding a song

Open **Mods > Big Screen > Video Library**. The browser counts songs once, not once per difficulty. Use Search Maps, the alphabetical jump list, normal scroll bar, or the filter selector for All Maps, Custom Maps, WIP Maps, and OST/DLC maps. A thumbnail appears beside a song only when Big Screen knows the assigned video's YouTube artwork; album covers are intentionally not substituted.

Selecting a song opens its editor. **Back to Song List** returns to the same filter, jump position, order, and song identity.

## YouTube search and download

**Search YouTube** launches the Quest browser with the selected song and artist already entered. Return to Beat Saber, copy/paste a result from the Quest clipboard, and confirm the fetched thumbnail. Both normal `https://www.youtube.com/watch?...` links and `https://youtu.be/...` share links are accepted. HTTP, lookalike domains, and arbitrary mapper URLs are rejected.

Download Video appears only after the URL probe confirms that a usable video exists. The UI shows preparation, byte/percentage progress, speed/ETA when known, completion, cancellation, or the actual failure. Clearing the URL before assignment also clears its thumbnail.

If a mapper supplied a URL but not the MP4, the normal song-detail screen offers the same download workflow and saves into the same library. Private/login-required, deleted, age/parental-control-restricted, 403/404, rate-limited, and temporary server failures are translated into readable explanations and recovery suggestions. Big Screen has no YouTube login and does not consume browser cookies.

After completion, the synchronized video/map-audio preview starts automatically so timing can be checked.

## Local video files

Select a song and choose **Show File Browser** in its child editor. Custom and WIP songs initially open their own map folder. OST, DLC, and other built-in songs initially open Big Screen's automatically created import folder:

`/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Video Import`

The center-screen browser uses a translucent dark background for legibility and can navigate through any readable folder under Quest shared storage. **Back One Folder** moves up one level; every tier in the path breadcrumb is also clickable for jumping several levels at once. Folders are listed first. Compatible MP4s are green; select one and then choose **Set Video**. The Set button remains disabled until a compatible file is selected. An incompatible MP4 remains visible in red with **HELP**, which explains the detected codec, resolution, container, or incomplete-file problem. **Cancel** closes the browser without changing the current assignment. Accepted local files are H.264/AVC MP4 at no more than 1920×1080. The file need not be renamed.

The selected file is referenced in place; it is not copied into Big Screen storage. Assigning a YouTube download replaces the active local registration, and assigning a local file replaces the active download registration; timing controls remain available for either source. The side editor shows the active local filename but keeps directory browsing on the wider center screen.

**Remove Video** stops preview playback first. For every browser-selected file, it unregisters the assignment but leaves the physical file untouched in its original folder. For a Big Screen-managed download, the confirmation can remove the managed video and its assignment.

## Synchronizing video and song

The playback group starts/stops both the map audio and video. Drag the scrubber to seek; the centered time shows current position and duration. A completed video can be scrubbed backward and played again.

- **Video Playback Offset** moves the video relative to song time. Negative values create time before video frame zero.
- **Playback Speed** manually adjusts video rate.
- **Fit to Song** continuously accounts for map duration and offset so the playable video ends with the song; it can handle a source longer or shorter than the song.
- **Lead-In Background** shows negative-time lead-in as solid black when on. Off leaves the screen transparent/hidden until video begins.

Map completion never waits for a longer video; Beat Saber ends the map normally.

## Screen layouts and mapper overrides

The Screen tab stores five independent layouts. Select the layout being edited, then adjust its basic placement, size, tilt, curve, and video opacity. Each layout has its own Advanced Screen Controls switch. Enabling it adds per-layout letterbox transparency, independent picture rotation, zoom, pan, perspective tilt, stretch/letterbox behavior, screen rotation, and controller-based undocked placement. The Screen Layout control on song selection changes the active global layout. During a playable video map, pause offers a session-only Video Screen switch and, when Big Screen owns geometry, a live Layout 1–5 selector.

Flat layouts scale from 0.5x through 4.0x. Curved layouts are limited to 2.5x. Enabling curvature on a larger flat layout immediately clamps it to 2.5x; returning to flat restores the 4.0x range.

Allow Chroma Override is enabled by default. For maps with Cinema/Chroma presentation data, mapper screen/environment choices take precedence. Detection is map-wide and applies even when the user assigned the video. Turn the option off to discard mapper-authored screen geometry and return to Big Screen's neutral back-wall canvas before applying the selected layout. URL/timing-only Cinema metadata does not claim presentation ownership.

## Performance

480p and 720p reduce conversion and texture-upload cost. The 15/30/60 FPS setting is a maximum; sources below it are not duplicated. Automatic Performance runs throughout gameplay and Video Library previews, reevaluating after every selected response-time interval. Frame loss at or above the chosen trigger steps FPS down before resolution; an interval below the trigger restores one quality step in the exact reverse order. It can move down and up repeatedly as load changes, never rises above the saved FPS and resolution preferences, and starts the next playback session from those saved preferences.

**Misc > Performance > Use FFmpeg 9** provides an in-game A/B switch between the bundled 4.4.8 decoder (off, the default) and 9.0.1 decoder (on). The same MP4, screen settings, output tier, and reusable-buffer path are used for both. An active Video Library preview restarts at the retained position; gameplay adopts the choice when the next map begins. Use the performance overlay or results summary to confirm which runtime actually ran, then compare repeated plays under the same headset temperature, charge, graphics, and map conditions.

Show Performance Information displays source/output resolution and FPS, missed frames, decode delay, and automatic reductions during gameplay and on the results/failure screen.

Record Power Benchmark creates two analysis files under `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs`: `power-benchmark-summary.csv` has one row per map, while `power-benchmark-samples.csv` has the one-second observations. It records baseline maps even when Video In Map is off, so compare otherwise identical runs by the `video_active` column. Whole-process CPU includes Beat Saber and every loaded mod; `decoder_cpu_ms` isolates Big Screen's owned FFmpeg worker. Main-thread texture upload and screen rendering remain part of the process difference rather than being falsely attributed to the decoder thread.

## Misc: storage and performance

The selected-song storage row distinguishes the current map's managed download, local-file bytes when local files exist, total Big Screen video-library usage, and free Quest storage. Values under 1 GB are displayed in MB.

The Misc tab contains Storage and Performance sections. Manage Storage opens a centered review page and stops any active library preview. Scan Storage lists only Big Screen-owned orphan downloads, unused thumbnails, or abandoned temporary files. Every candidate starts checked; uncheck anything you want to keep, then clean only the selected files after confirmation. Assigned downloads, required runtime files, map-folder MP4s, and Video Import MP4s are protected. The Performance section contains Automatic Performance, its frame-loss trigger, the shared downscale/recovery response time, the optional playback-information overlay, and the power benchmark recorder.
