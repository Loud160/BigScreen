# Big Screen user guide

## Adding and adjusting a video

Open Big Screen from Beat Saber's Mods area and choose a song in Video Library.
You can paste either a normal `youtube.com/watch` URL or a `youtu.be` share URL.
Big Screen validates the URL, shows its video thumbnail, and enables Download
Video only when the video is available. Private, login-required, unavailable,
and parental-control-restricted videos report their actual reason.

If a mapper listed a downloadable video but did not include the MP4, the normal
song-detail screen shows Video available and Download Video. The same row shows
preparation and byte/percentage progress, allows cancellation/resume, and saves
the completed mapper video into Big Screen's durable library. A failed download
shows the specific reason and directs the player to the Video Library, where a
different YouTube result or compatible local H.264 MP4 can be assigned.
Common web failures are translated into plain language. For example, HTTP 403
is identified as YouTube refusing access to the requested stream, while 404,
429, authentication errors, and temporary YouTube server errors explain what
the response means and whether retrying, updating yt-dlp, or changing videos is
the appropriate next step.

After downloading or assigning a local/imported MP4, use the playback preview
to adjust Video Playback Offset and Playback Speed. Fit to Song keeps the
playable video range aligned with the map length, including any lead-in created
by the offset. The lead-in can be transparent or solid black.

## Screen layouts

The Screen tab stores three independent layouts. Select the layout being edited,
then adjust placement, size, tilt, and curve. The Screen Layout control on the
song-selection header switches the active layout without opening the mod menu.
While a playable assigned video is active, the pause menu provides a Video
Screen toggle that hides or restores the screen for the current play without
changing the global Video In Map preference. Big Screen-controlled maps also
show a Screen Layout selector. Switching layouts updates the live geometry
without reopening the decoder, restarting the video, or changing its song-time
synchronization. Mapper/Chroma-controlled screens keep their authored layout
and therefore do not show the layout selector.
Flat layouts can scale the screen from 0.5x through 4.0x. Curved layouts are
limited to 2.5x; enabling curvature on a larger flat layout immediately resizes
it to 2.5x, while returning to flat mode restores the 4.0x adjustment range.

Allow Chroma Override is enabled by default. When a map includes mapper-authored
Cinema or Chroma presentation data, the mapper's screen position, rotation,
size, curvature, transparency, requested environment, and environment-object
changes take precedence. Quest Chroma continues to process the difficulty's
Chroma environment data. Turn the option off to ignore that presentation and
use the selected Big Screen layout and environment controls instead. Detection
is map-wide and independent of how the video was assigned: Chroma requirements,
suggestions, or environment data are honored for mapper, downloaded, imported,
and map-folder videos. Non-Chroma maps that contain only video URL/timing data
are unaffected by this option.

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
