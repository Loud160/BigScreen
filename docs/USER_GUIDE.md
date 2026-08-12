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

For a custom or WIP map, copy one or more `.mp4` files into the map folder. For OST, DLC, or any other song, copy the file into:

`/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Video Import`

Select the song in Video Library. Compatible filenames appear with **SET**; the active assignment is green. An incompatible MP4 remains visible in red with **HELP**, which explains the detected codec/resolution problem. Accepted local files are H.264/AVC MP4 at no more than 1920×1080. The file need not be renamed.

Video Import files can be assigned to any song. Map-folder files are offered only for their map. Assigning a YouTube download replaces the active local registration, and assigning a local file replaces the active download registration; timing controls remain available for either source.

**Remove Video** stops preview playback first. For a map-folder or Video Import file, it unregisters the assignment but leaves the physical file untouched. For a Big Screen-managed download, the confirmation can remove the managed video and its assignment.

## Synchronizing video and song

The playback group starts/stops both the map audio and video. Drag the scrubber to seek; the centered time shows current position and duration. A completed video can be scrubbed backward and played again.

- **Video Playback Offset** moves the video relative to song time. Negative values create time before video frame zero.
- **Playback Speed** manually adjusts video rate.
- **Fit to Song** continuously accounts for map duration and offset so the playable video ends with the song; it can handle a source longer or shorter than the song.
- **Lead-In Background** shows negative-time lead-in as solid black when on. Off leaves the screen transparent/hidden until video begins.

Map completion never waits for a longer video; Beat Saber ends the map normally.

## Screen layouts and mapper overrides

The Screen tab stores three independent layouts. Select the layout being edited, then adjust placement, size, tilt, curve, and transparency. The Screen Layout control on song selection changes the active global layout. During a playable video map, pause offers a session-only Video Screen switch and, when Big Screen owns geometry, a live Layout 1–3 selector.

Flat layouts scale from 0.5x through 4.0x. Curved layouts are limited to 2.5x. Enabling curvature on a larger flat layout immediately clamps it to 2.5x; returning to flat restores the 4.0x range.

Allow Chroma Override is enabled by default. For maps with Cinema/Chroma presentation data, mapper screen/environment choices take precedence. Detection is map-wide and applies even when the user assigned the video. Turn the option off to use Big Screen's selected layout/environment controls instead. URL/timing-only Cinema metadata does not claim presentation ownership.

## Performance

480p and 720p reduce conversion and texture-upload cost. The 15/30/60 FPS setting is a maximum; sources below it are not duplicated. Automatic Performance watches five-second windows and temporarily steps FPS down before resolution when the chosen missed-frame threshold is crossed. Saved preferences return on the next map.

Show Performance Information displays source/output resolution and FPS, missed frames, decode delay, and automatic reductions during gameplay and on the results/failure screen.

## Storage

The selected-song storage row distinguishes the current map's managed download, local-file bytes when local files exist, total Big Screen video-library usage, and free Quest storage. Values under 1 GB are displayed in MB.

The Storage tab opens a review page. Scan Storage lists only Big Screen-owned orphan downloads, unused thumbnails, or abandoned temporary files. Cleanup requires confirmation. Assigned downloads, required runtime files, map-folder MP4s, and Video Import MP4s are protected.
