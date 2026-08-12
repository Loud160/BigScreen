# Settings reference

Big Screen has five tabs: General, Screen, Environment, Update, and Misc. Settings are global unless a description explicitly says “current play,” “selected video,” or “selected layout.” Reset to Defaults resets all five layouts and every option described below.

## General

### Video Frame Rate Limit — default: 30 FPS

Sets the maximum number of frames Big Screen presents per second: 15, 30, or 60. It does not speed up or slow down the video. A 24/30 FPS source remains at its native cadence under a 60 FPS cap; a 60 FPS source drops presentation frames evenly under a 30/15 FPS cap while synchronization still follows song time. Lower values reduce decoding, memory-copy, and texture-upload pressure.

### Video Resolution — default: 720p

Sets the maximum output height: 480p, 720p, or 1080p. Big Screen keeps the source MP4 unchanged and downsizes decoded output in memory; switching this option does not download another file. Sources below the selected limit are not enlarged. 720p is the recommended Quest balance. 1080p consumes more CPU, memory bandwidth, battery, and texture upload time.

### Big Screen Enabled — default: On

Master switch. Off stops previews, downloads, screens, and environment changes while leaving the Big Screen Mods entry available. Related controls are disabled so a partially disabled configuration cannot continue interacting with Beat Saber.

### Distraction Free Menu — default: On

While the Big Screen menu is open, temporarily hides the neon Beat Saber sign and supported clock/battery overlays it can detect. Objects are restored on exit. Detection is defensive: stock installations and installations without those optional UI objects remain supported.

### Video In Map — default: On

Global gameplay-video master. Turning it off also disables Preview Video to avoid background decoder work. It is synchronized with the identically named song-selection header toggle.

### Preview Video — default: On

Plays assigned videos while browsing songs. It requires both Big Screen Enabled and Video In Map. Disable it if rapid song browsing causes unwanted decoder/storage activity while retaining in-map playback.

### Add My Own Video

Explains the accepted local format and where custom-map videos can be placed. The button does not copy a file by itself; see [Video Library](USER_GUIDE.md#local-video-files).

### Reset to Defaults

Shows a confirmation dialog, then restores every global option and all five layouts. Live menu preview geometry is rebuilt from the new values so the visible screen and control values cannot disagree.

## Screen

The menu always displays a blank screen in the placement environment. Changes update it immediately. Mapper-controlled Cinema/Chroma presentation can intentionally replace these values for an authored map.

### Editing Screen Layout — default: Layout 1

Chooses one of five independent layout profiles. It both selects the active layout and chooses which profile the controls edit. Layout can also be changed from song selection and, when Big Screen owns the current screen, from pause.

### Advanced Screen Controls — default: Off

Enables independent video framing and free screen placement for the selected layout after an **I Understand** confirmation. Each of the five layouts saves this switch independently, allowing basic and advanced layouts to coexist. Turning it off ignores that layout's saved transparency, screen rotation, video transforms, and undocked placement without deleting their values. Extreme settings may reduce performance or interact poorly with authored map effects.

### Allow Chroma Override — default: On

When a video map contains Cinema/Chroma presentation data, lets the mapper control screen geometry, curvature/transparency choices, requested environment, and environment-object modifications. Chroma detection is map-wide and applies regardless of whether the video came from the mapper, YouTube, Video Import, or the map folder. Turn this off to force the active Big Screen layout/environment options. Timing metadata remains honored either way.

### Screen Distance Offset — default: 0, range: -40 to +40

Adds to mapper/default depth. Negative moves the screen closer; positive moves it farther away.

### Screen X Offset — default: 0, range: -40 to +40

Negative moves left and positive moves right.

### Screen Y Offset — default: 0, range: -40 to +40

Negative moves down and positive moves up.

### Screen Tilt Offset — default: 0°, range: -30° to +30°

Adjusts the vertical viewing angle. It is useful when the default screen appears to lean toward or away from the player.

### Screen Size Multiplier — default: 1.0x

Multiplies the authored/default screen height. Flat range is 0.5–4.0x; curved range is 0.5–2.5x. Enabling Curved Screen while above 2.5x immediately clamps and saves 2.5x. Returning to flat restores the 4.0x adjustment range, not the former oversized value.

### Curved Screen — default: Off

Switches between a flat quad and segmented curved geometry. Curved surfaces cost more geometry and can grow very wide, so their maximum size is lower.

### Maintain Aspect Ratio — default: Off

Visible only for curved screens. On keeps the original width-to-height relationship while bending. Off preserves Big Screen's wider curved behavior, which some users prefer for a wraparound presentation but stretches the edges more strongly.

### Screen Curve — default: +0.35, range: -7 to +7

Visible only for curved screens. Positive values wrap edges toward the player; negative values bend them away. Drag the slider for coarse movement or use arrows for 0.05 fine adjustments.

The following controls are visible only when **Advanced Screen Controls** is enabled. Every value belongs to the currently selected layout.

### Video Transparency — default: Off

On uses partial transparency so scenery/lights behind the plane can show through. Off uses an opaque material and blocks geometry behind the screen; lighting or bloom drawn on/in front of the same plane may still appear visually over it.

### Screen Rotation — default: 0°, range: -180° to +180°

Rolls the complete screen frame clockwise or counterclockwise around its viewing axis. Width and height are not swapped at 90° or 180°; the saved frame simply rotates.

### Video Rotation — default: 0°, range: -180° to +180°

Rotates only the picture inside the frame. It does not rotate or resize the frame. Any uncovered region is solid black when Video Transparency is off and transparent when Video Transparency is on.

### Video Zoom — default: 1.0x, range: 0.5x to 3.0x

Scales the picture inside the fixed frame without changing decode resolution. Values below 1 reveal more background; values above 1 crop image edges. Use Video X/Y Position to choose which portion remains visible.

### Video X Position / Video Y Position — defaults: 0, ranges: -1 to +1

Pan the picture horizontally or vertically inside the frame. These are most useful with Video Zoom or Video Rotation and never move the screen itself.

### Video Tilt — default: 0°, range: -75° to +75°

Applies perspective tilt to the picture so its top or bottom appears closer. This differs from Video Rotation, which spins the picture in its own plane, and Screen Tilt Offset, which angles the entire frame.

### Stretch Video to Fit — default: Off

On distorts the picture as needed to fill the full frame. Off preserves the source aspect ratio and letterboxes it unless zoom fills or crops the frame. Letterbox background follows Video Transparency.

### Undock Screen — default: Off

Replaces map-relative offsets with an absolute position, angle, width, and height saved for the current layout. Enabling requires confirmation. **Position Screen** opens a frame-only editor: hold the trigger on the frame to move/rotate it, drag the lower-right handle to resize it, then select **Save Screen**. The editor displays the current aspect ratio. Once saved, all editor controls are destroyed so controller rays pass through the normal playback screen.

While undocked, the map-relative Distance, X/Y, Screen Tilt, Screen Size Multiplier, and Screen Rotation controls are disabled because the controller editor owns those absolute values. Curved Screen, curve amount/aspect behavior, transparency, and all video-framing controls remain available.

Leaving Big Screen, changing layouts/settings, disabling the mod, or opening the Quest system menu cancels unsaved positioning and restores the last saved placement. **Cancel Positioning** provides the same safe exit. Flat/curved controls continue to apply to an undocked screen.

Allow Chroma Override retains priority only when an authored map actually supplies Cinema/Chroma presentation data. With that option on, authored presentation wins; without authored presentation, the selected Big Screen layout—including an undocked screen—wins. Turning Allow Chroma Override off always forces the selected Big Screen layout.

## Environment

These options apply only when a playable video map is being prepared. Ordinary maps without an assigned/playable video are unaffected. Changes marked “next map” require environment creation and cannot safely rebuild the current gameplay scene.

### Map Light Show — default: On

Master for map-authored lighting during video maps. Off suppresses the light-show processing and disables the three individual light-group controls below. Their saved states are retained and restored when Map Light Show returns on.

### Hide Back Wall Lights — default: On

With Map Light Show on, hides back-wall and center-lane groups that commonly wash over the screen while retaining other lighting.

### Hide Ring Lights — default: On

Hides the ring-light group while allowing remaining map light events.

### Hide Side Laser Lights — default: On

Hides left/right laser-light groups whose beams commonly cross a large screen.

Disabled child controls do not display hover hints; Map Light Show keeps its own hint so the reason remains discoverable.

### Use Big Mirror Override — default: On

Loads Big Mirror for video maps because its open back-wall area fits a large screen. Off preserves the map's intended environment, which can place scenery in front of the video. A mapper-owned Chroma/Cinema environment takes precedence when Allow Chroma Override is active.

### Disable Rotation and Motion — default: Off

On stops supported rotating/moving background components for video maps. This is useful when large scenery distracts from the screen.

### Hide Track Rings — default: On

Hides overhead ring/arch geometry that can pass across the screen.

### Hide Side Bars — default: On

Hides Big Mirror's paired near-building structures that can block the screen edges.

### Hide Spectrogram Bars — default: On

Hides audio-reactive lane-side spectrogram geometry.

## Update

### Use Nightly Builds — default: Off

Selects yt-dlp's nightly release channel. Enabling requires confirmation because nightly builds receive fixes sooner but are more likely to contain regressions. This option affects downloader updates only, not Big Screen updates.

### Check yt-dlp / Install Update

The hover hint identifies the active stable/nightly channel. Checking contacts GitHub and reports whether a newer official package exists. Installation downloads the official asset, verifies it against the official SHA-256 list, validates the archive, and stages it for the next Beat Saber restart. It never replaces a running Python module.

On restart, Big Screen import-tests the candidate. An incompatible candidate is rolled back to the last working package or shipped baseline and marked rejected so the same release is not repeatedly offered. A newer version remains eligible.

## Misc

### Storage

#### Manage Storage

Opens a centered review page and stops any active library preview. **Scan Storage** finds only Big Screen-owned orphan downloads, unused thumbnails, and abandoned temporary files. Candidates are checked by default; uncheck individual files to keep them, then confirm removal of only the remaining checked files. Assigned downloads, the embedded runtime, map-folder MP4s, and Video Import MP4s are protected.

### Performance

#### Automatic Performance — default: Off

Monitors five-second playback windows. If missed presentation requests reach the selected trigger, the current map temporarily steps through 60→30→15 FPS and then 1080→720→480p. It never modifies the MP4 or overwrites saved preferences; the next map starts from the saved limits.

#### Automatic Performance Trigger — default: 10% missed frames

Available when Automatic Performance is on. 5% reacts sooner, 10% is balanced, and 20% tolerates more missed frames before reducing output. This measures Big Screen presentation performance, not Beat Saber's headset refresh rate.

#### Show Performance Information — default: Off

Displays source/output resolution and FPS, requested versus presented frames, average decode delay, and automatic reductions during gameplay. A summary is retained on results/failure screens so it can be read after playing. Enable this when tuning quality or reporting performance problems.

## Song-selection header controls

- **Screen Layout:** global Layout 1–5 selector; available while the mod is enabled.
- **Preview Video:** same global preference as General.
- **Video In Map:** same global master as General.
- **Download Video:** appears when mapper metadata has a valid YouTube source but no local video. Shows preparation and byte progress, supports cancellation, and records the result in the same library as Video Library downloads.

## Pause-menu controls

- **Video Screen:** appears only while a map with an assigned playable video is running. It hides/restores the current screen without changing the global Video In Map setting; map lighting/environment choices stay active.
- **Screen Layout:** appears when Big Screen, rather than mapper/Chroma presentation, owns the screen. It applies Layout 1–5 live without restarting video or changing the song clock.

## Per-video editor controls

These belong to the selected library entry, not the global settings file:

- **Fit to Song:** continuously derives a playback rate/range that ends with the map while including offset lead-in.
- **Playback Speed:** manual rate when Fit to Song is off.
- **Video Playback Offset:** shifts video time relative to song time; negative values create lead-in before video frame zero.
- **Lead-In Background:** on shows negative-time lead-in as solid black; off keeps the screen transparent/hidden until video begins.
- **Play/Pause, scrubber, time:** previews map audio and video together for timing. Scrubbing after end restarts a valid preview position.
- **Remove Video:** removes the assignment. Managed downloads can be deleted; local/import files are only unregistered and the confirmation explains that the source file remains.
