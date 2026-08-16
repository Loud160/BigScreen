# Settings reference

Big Screen has five tabs: General, Screen, Environment, Update, and Misc. Settings are global unless a description explicitly says “current play,” “selected video,” or “selected layout.” Reset to Defaults resets all five layouts and every option described below.

## General

### Video Frame Rate Limit — default: 30 FPS

Sets the maximum number of frames Big Screen presents per second: 15, 30, or 60. It does not speed up or slow down the video. A 24/30 FPS source remains at its native cadence under a 60 FPS cap; a 60 FPS source drops presentation frames evenly under a 30/15 FPS cap while synchronization still follows song time. Lower values reduce decoding, memory-copy, and texture-upload pressure.

### Big Screen Enabled — default: On

Master switch. Off stops previews, downloads, screens, and environment changes while leaving the Big Screen Mods entry available. Related controls are disabled so a partially disabled configuration cannot continue interacting with Beat Saber.

### Distraction Free Menu — default: On

While the Big Screen menu is open, temporarily hides the neon Beat Saber sign and supported clock/battery overlays it can detect. Objects are restored on exit. Detection is defensive: stock installations and installations without those optional UI objects remain supported.

### Show Menu Environment — default: On

Shows Beat Saber's normal menu scenery, lighting, and floor behind Big Screen. Off provides an unlit, unobstructed placement space and leaves screens visible below gameplay floor height while preserving the environment hierarchy, menu cameras, controller input, Big Screen UI, preview screen, and optional lane guides. The complete environment is restored on focus loss, mod disable, menu exit, and errors.

### Show Lane Guides — default: Off

Draws thin, non-interactive lane rails, depth marks, and a player-origin marker from the menu player position to the default back wall. It works with the floor or complete environment either visible or hidden. The guides exist only in Big Screen's menu and are removed on every menu/focus/gameplay boundary.

### Video In Map — default: On

Global gameplay-video master. Turning it off also disables Preview Video to avoid background decoder work. It is synchronized with the identically named song-selection header toggle.

### Preview Video — default: On

Plays assigned videos while browsing songs. It requires both Big Screen Enabled and Video In Map. Disable it if rapid song browsing causes unwanted decoder/storage activity while retaining in-map playback.

### Add My Own Video

Explains the accepted local format and where custom-map videos can be placed. The button does not copy a file by itself; see [Video Library](USER_GUIDE.md#local-video-files).

### Reset to Defaults

Shows a confirmation dialog, then restores every global option and all five layouts. Live menu preview geometry is rebuilt from the new values so the visible screen and control values cannot disagree.

## Screen

The menu always displays a blank screen in the placement environment. Changes update it immediately. Turn off **General > Show Menu Environment** for an unobstructed, unlit placement space that also permits positioning below the stock floor, and use **Show Lane Guides** independently when a gameplay-coordinate reference would help. Mapper-controlled Cinema/Chroma presentation can intentionally replace these values for an authored map.

### Editing Screen Layout — default: Layout 1

Chooses one of five independent layout profiles. It both selects the active layout and chooses which profile the controls edit. Layout can also be changed from song selection and, when Big Screen owns the current screen, from pause.

### Advanced Screen Controls — default: Off

Enables independent video framing and free screen placement for the selected layout after an **I Understand** confirmation. Each of the five layouts saves this switch independently, allowing basic and advanced layouts to coexist. Turning it off ignores that layout's saved letterbox transparency, screen rotation, video transforms, and undocked placement without deleting their values. Video Opacity remains available in basic mode. Extreme settings may reduce performance or interact poorly with authored map effects.

### Allow Chroma Override — default: On

Preserves the intended Chroma/Cinema environment for a video map. The map takes ownership of the screen canvas only when both conditions are true: the map is detected as using Chroma, and its video metadata supplies custom position, rotation, size, or curvature. Otherwise the selected Big Screen layout and its Screen Canvas controls remain active. Video Controls, Video Opacity, and Letterbox Transparency always control how the picture is composed inside whichever canvas is active. Detection is map-wide and works whether the video came from the mapper, YouTube, Video Import, or the map folder. Turn this off to force Big Screen's canvas and environment options. Timing metadata remains honored either way.

### Screen Distance Offset — default: 0, range: -180 to +180

Adds to mapper/default depth. Negative moves the screen closer; positive moves it farther away.

### Screen X Offset — default: 0, range: -180 to +180

Negative moves left and positive moves right.

### Screen Y Offset — default: 0, range: -180 to +180

Negative moves down and positive moves up. The extended range preserves full placement control for screens enlarged to the 8.0x maximum, including enough upward travel to lift the complete default canvas above the menu floor.

### Screen Tilt Offset — default: 0°, range: -180° to +180°

Adjusts the vertical viewing angle. It is useful when the default screen appears to lean toward or away from the player.

### Screen Size Multiplier — default: 1.0x

Multiplies the authored/default screen height. Flat and curved layouts both range from 0.5–8.0x. Menu and gameplay use the same calculation. Physical size changes do not add curved-screen segments or increase decoder resolution; they enlarge the existing canvas geometry.

### Curved Screen — default: Off

Switches between a flat quad and segmented curved geometry. Both modes support the full 0.5–8.0x Screen Size Multiplier range.

### Maintain Aspect Ratio — default: Off

Visible only for curved screens. On keeps the original width-to-height relationship while bending. Off preserves Big Screen's wider curved behavior, which some users prefer for a wraparound presentation but stretches the edges more strongly.

### Screen Curve — default: +0.35, range: -7 to +7

Visible only for curved screens. Positive values wrap edges toward the player; negative values bend them away. Drag the slider for coarse movement or use arrows for 0.05 fine adjustments.

The following controls are visible only when **Advanced Screen Controls** is enabled. Every value belongs to the currently selected layout.

### Video Opacity — default: 1.00

Sets the opacity of the decoded picture from 0.00 (invisible) to 1.00 (fully opaque). This basic, per-layout control applies to docked and undocked screens. Values below 1.00 use alpha blending so scenery and lights behind the video can show through.

### Letterbox Transparency — default: Off

Available with Advanced Screen Controls. On removes the black background visible when rotation, zoom, pan, or aspect-ratio preservation leaves part of the screen canvas uncovered. It does not fade the picture. Off keeps those unused areas solid black. This applies to docked and undocked screens.

### Screen Rotation — default: 0°, range: -180° to +180°

Rolls the complete screen frame clockwise or counterclockwise around its viewing axis. Width and height are not swapped at 90° or 180°; the saved frame simply rotates.

### Video Rotation — default: 0°, range: -180° to +180°

Rotates only the picture inside the frame. It does not rotate or resize the frame. Any uncovered region is solid black when Letterbox Transparency is off and transparent when it is on.

### Video Zoom — default: 1.0x, range: 0.5x to 3.0x

Scales the picture inside the fixed frame without changing decode resolution. Values below 1 reveal more background; values above 1 crop image edges. Use Video X/Y Position to choose which portion remains visible.

### Video X Position / Video Y Position — defaults: 0, ranges: -1 to +1

Pan the picture horizontally or vertically inside the frame. These are most useful with Video Zoom or Video Rotation and never move the screen itself.

### Video Tilt — default: 0°, range: -75° to +75°

Applies perspective tilt to the picture so its top or bottom appears closer. This differs from Video Rotation, which spins the picture in its own plane, and Screen Tilt Offset, which angles the entire frame.

### Stretch Video to Fit — default: Off

On distorts the picture as needed to fill the full frame. Off preserves the source aspect ratio and letterboxes it unless zoom fills or crops the frame. The unused background follows Letterbox Transparency.

### Undock Screen — default: Off

Replaces map-relative offsets with an absolute position, angle, width, and height saved for the current layout. Enabling requires confirmation. **Position Screen** opens a frame-only editor: hold the trigger on the frame to move/rotate it, drag the lower-right handle to resize it, then select **Save Screen**. The editor displays the current aspect ratio. Once saved, all editor controls are destroyed so controller rays pass through the normal playback screen.

While undocked, the map-relative Distance, X/Y, Screen Tilt, Screen Size Multiplier, and Screen Rotation controls are disabled because the controller editor owns those absolute values. Curved Screen, curve amount/aspect behavior, Video Opacity, Letterbox Transparency, and all video-framing controls remain available.

Leaving Big Screen, changing layouts/settings, disabling the mod, or opening the Quest system menu cancels unsaved positioning and restores the last saved placement. **Cancel Positioning** provides the same safe exit. Flat/curved controls continue to apply to an undocked screen.

Allow Chroma Override retains priority only when an authored map actually supplies Cinema/Chroma presentation data. With that option on, authored presentation wins; without authored presentation, the selected Big Screen layout—including an undocked screen—wins. Turning Allow Chroma Override off first restores Big Screen's neutral back-wall canvas, then applies the selected layout; mapper X/Y/Z, rotation, and size are not retained as hidden offsets.

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

### Showcase

#### Play Big Screen Showcase

Opens a dedicated center-screen readiness page. It checks SongCore's live capability registry for both Chroma and Noodle Extensions, reports the downloader runtime, and independently reports whether the exact showcase map and its video are ready. A mod that is installed but failed to load is treated as unavailable.

Opening the page never starts a download. A missing map gets its own **Download Map** button; downloaded files that SongCore has not recognized get **Recheck Map**; and a missing video gets **Download Video** after the map is ready. The map action obtains the immutable `11cf8` Up & Down revision from BeatSaver, validates and safely extracts it under Big Screen's managed `DemoLevels` folder, registers the folder with SongCore, and waits for the song refresh. The video action uses the ordinary managed video library at 1080p. Existing user-installed maps and user video overrides are never replaced or deleted.

**Play Showcase** remains disabled until Chroma, Noodle Extensions, the map, and the video are all ready. It then shows the motion-sickness warning, closes the readiness page, waits for Beat Saber's main menu hierarchy to stabilize, and starts Lawless Expert+ directly with No Fail enabled for that showcase session. Big Screen uses a temporary modifier copy and immediately restores the player's menu modifiers, so playing the map normally is unaffected. Big Screen does not alter the completed results page or attempt to reopen itself afterward; use Beat Saber's normal navigation, then reopen Big Screen from Mods when wanted.

### Performance

#### Use FFmpeg 9 — default: On

This is an experimental comparison option. It selects which bundled decoder runtime opens the next video. Off uses FFmpeg 4.4.8; on uses FFmpeg 9.0.1. It does not change, convert, or redownload the video. If a Video Library preview is active, changing the switch safely recreates playback at the retained song position. A map already in gameplay is never switched underneath its running decoder; the new choice applies when the next playback session starts. The performance overlay and results summary identify the runtime that actually opened.

#### Hardware Video Decoding — default: On

On requests Android MediaCodec for H.264, H.265/HEVC, VP8, or VP9 from whichever FFmpeg runtime is selected. MediaCodec output is copied into CPU-readable memory for Big Screen's existing color conversion and Unity texture upload; this preserves every screen shape and effect but is not a zero-copy GPU path. Startup or mid-video hardware failures reopen with software only when the codec and resolution policy permits it. HEVC and content above 1080p instead stop video safely while the map continues. Turn the option off to force the permitted FFmpeg software decoder for H.264, VP8, or VP9 content at no more than 1080p. The live panel, results summary, error history, and power benchmark identify the backend that actually remained active. Changing this option restarts an active Video Library preview at its retained time and applies to gameplay on the next map.

#### Automatic Performance — default: Off

Continuously watches video presentation for the entire map. At the end of each selected response-time window, frame loss at or above the trigger lowers the presentation limit by 5 FPS, down to a 15 FPS floor. The first useful reduction begins below the video's effective cadence, including Fit to Song playback speed: a 30 FPS video under a 60 FPS preference moves directly to 25 rather than walking through ineffective 55/50/45/40/35 limits, while a 24 FPS video begins at 20. A complete healthy window restores one exact prior limit in reverse order. The controller keeps reevaluating, so it can move down and back up repeatedly as the map becomes more or less demanding. It never changes video resolution, reopens the decoder, modifies the source file, exceeds the saved FPS preference, or overwrites that preference. The next playback session starts from the saved limit.

Turning this on opens a confirmation explaining that Automatic Performance is experimental and still under development. Nothing is enabled or saved until the player confirms.

#### Frame Rate Loss Trigger — default: 5%

Available when Automatic Performance is on. This 1–15% slider selects the video frame-rate loss at which quality steps down. Below the selected value, quality is eligible to step back up. This measures Big Screen video presentation performance, not Beat Saber's headset refresh rate.

#### Scaling Response Time — default: 5.0 seconds

Available when Automatic Performance is on. This 0.5–10.0 second slider defaults to 5.0 seconds and controls how long either condition must continue before one 5 FPS step is taken. The same duration is used for reduction and recovery. It operates during gameplay and Video Library preview playback. Short times react quickly but may change limits more often; longer times require more sustained evidence before changing the limit.

#### Show Performance Information — default: Off

Displays the active hardware/software backend, presentation-oriented native video resolution, source FPS, current FPS limit, expected versus presented source frames, missed percentage, full decode-request delay, and automatic reductions during gameplay. Hold the trigger anywhere on the panel to move and angle it. Big Screen saves that transform when the switch is turned off or the menu/gameplay context closes, and the same placement is reused in the Video Library and during video maps. The circular reset button immediately to the left of this switch returns the panel to its safe default position and angle. The results/failure summary also identifies the loaded FFmpeg runtime and reusable RGBA allocation count. Enable this when comparing decoder builds, tuning frame rate, or reporting performance problems.

#### Record Power Benchmark — default: Off

Records one sample per second for every played map, including maps played with **Video In Map** off. Each sample contains the Quest battery charge counter, instantaneous and Android-averaged battery current when the headset exposes them, battery percentage/charging state, whole Beat Saber process CPU time, Big Screen decoder-worker CPU time, actual video resolution, active FPS limit, and playback statistics. Data stays in memory during gameplay and is appended to CSV files only after the map ends or is exited. When this release first writes the simplified native-resolution schema, an older CSV is preserved with a timestamped `-legacy-` filename.

Use this for controlled A/B tests, not as a permanent gameplay option. Unplug USB/external power, hold brightness, refresh rate, map, difficulty, modifiers, graphics, and headset temperature constant, then play the same map once with video off and once with video on. A blank battery field means the Quest firmware reported that individual Android property as unsupported; it is not treated as zero.

## Song-selection header controls

- **Screen Layout:** global Layout 1–5 selector; available while the mod is enabled.
- **Preview Video:** same global preference as General.
- **Video In Map:** same global master as General.
- **Download Video:** appears when mapper metadata has a valid YouTube source but no local video. It opens a modal that checks and lists the source's available 480p/720p/1080p/1440p tiers (or its single best lower tier), then shows preparation and byte progress in the existing row. Cancel and Retry retain the selected tier, and the result is recorded in the same library as Video Library downloads.

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
- **Show File Browser:** opens a wide center-screen browser over a translucent dark background. Custom/WIP songs begin in their map folder; built-in songs begin in the automatically created Video Import folder. **Back One Folder** and clickable path breadcrumbs navigate anywhere under readable Quest shared storage. Folder scanning and MP4 compatibility probes run off the UI thread.
- **Remove Video:** opens a three-choice confirmation. **Unlink** removes only the assignment and keeps the file on the Quest. **Delete File/Video** removes both the assignment and physical media. This works for Big Screen downloads, file-browser assignments, and local videos declared by a map; map-local opt-outs persist without modifying the map package.
