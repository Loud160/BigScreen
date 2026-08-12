# Big Screen user guide

## Adding and adjusting a video

Open Big Screen from Beat Saber's Mods area and choose a song in Video Library.
You can paste either a normal `youtube.com/watch` URL or a `youtu.be` share URL.
Big Screen validates the URL, shows its video thumbnail, and enables Download
Video only when the video is available. Private, login-required, unavailable,
and parental-control-restricted videos report their actual reason.

After downloading or assigning a local/imported MP4, use the playback preview
to adjust Video Playback Offset and Playback Speed. Fit to Song keeps the
playable video range aligned with the map length, including any lead-in created
by the offset. The lead-in can be transparent or solid black.

## Screen layouts

The Screen tab stores three independent layouts. Select the layout being edited,
then adjust placement, size, tilt, and curve. The Screen Layout control on the
song-selection header switches the active layout without opening the mod menu.

## Performance

480p and 720p reduce conversion and texture-upload cost. The 15/30/60 FPS
setting is a maximum: a 24 or 30 FPS source is not duplicated to reach 60 FPS.
Automatic Performance watches five-second presentation windows. When the
configured missed-frame threshold is crossed it steps 60 to 30 to 15 FPS, then
1080p to 720p to 480p. These temporary reductions do not overwrite preferences.

Show Performance Information displays source/output resolution and FPS, missed
frames, decode delay, and automatic reductions while playing and on results.

## Storage

The Storage tab opens a review page. Scan Storage lists the exact Big
Screen-owned orphan downloads, unused thumbnails, or abandoned temporary files
that can be removed. Cleanup requires confirmation. Assigned downloads,
required runtime files, map-folder MP4s, and Video Import MP4s are protected.
