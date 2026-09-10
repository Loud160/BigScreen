# Advanced Video and Audio Sync

Alpha 14 development: the first integrated build is ready for live Quest testing.
Compilation and generated-audio tests do not establish headset accuracy, UI
placement, or performance. This document describes the implemented workflow;
the implementation plan records validation status and remaining measurements.

## Opening the workspace

Select a map in Big Screen's video menu, turn on **Advanced Sync**, then select
**Configure**. Settings belong to that map. Advanced Sync disables the four
basic timing controls; turning it off restores their previous values without
deleting the advanced configuration.

New YouTube downloads also acquire matching compressed audio. Old downloads
without audio need to be downloaded again. Local videos can use their embedded
audio. If usable audio is missing, the switch returns to Off and explains the
problem; basic video playback remains available. A failed companion download
does not discard an otherwise valid video.

The center **Audio Sync** tab prepares its audio automatically. Long operations
show progress and **Cancel**. The right-hand video preview remains available
during preparation. You cannot select a different map or replace its video
while this workspace is open.

The top workspace row keeps **Audio Monitoring**, **Apply Changes**, and
**Reset Track** together. Apply saves without closing. Reset restores the
track's original mapper timing, or neutral timing for a user-added video, and
still requires Apply before it becomes permanent.

Controls that depend on prepared audio remain visibly disabled until audio is
ready. The four basic timing controls on the right also explain that Advanced
Sync owns timing while it is enabled; mapper-authored timing is a reset baseline,
not an unexplained lock on the Advanced Sync editor.

Custom/WIP songs are decoded from their map audio files. Built-in songs use
the game's full song clip, not its short song-selection sample. Resident clips
are copied in small batches. Streaming clips may require one silent sequential
pass, which can take the song's full duration. A completed, unchanged capture
is cached for later sessions. That fallback's timing fidelity still requires
Quest validation.

## Automatic alignment

Directly below **Sync Workflow**, choose the number of matching points and a
sample duration of 4, 8, 12, or 16 seconds, then select **Analyze Audio** beside
the workflow selector. The button and analyzer-setting row are visible only in
Automatic Match. Analyze remains available after a result, so different
settings can be tried more than once. More points and longer samples can take
longer.

Completed analysis opens a frontmost result dialog containing confidence,
accepted/rejected match-point counts, independent-check count, proposed offset,
and proposed playback-speed change. **Discard** closes the result without
changing timing. **Apply** accepts the proposal into the unsaved editor draft;
it does not bypass the workspace's existing **Apply Changes** save action.
Video Offset and Video Speed remain visible in Automatic Match so the accepted
result can be fine-tuned without changing workflows. Switching to **Manual
Adjustment** retains that same proposal and every later adjustment.

Switching from Manual Adjustment to Automatic Match does not erase edits, but a new Analyze
operation searches independently rather than treating the manual timing as
its starting answer. Ambiguous/repeated music and different edits of a song
can produce low confidence or no usable proposal. Confidence is not presented
as an uncalibrated percentage.

The initial automatic search covers approximately 0.9x–1.1x speed differences.
The manual range is wider. Substantial cuts/rearrangements may not be representable
by a single offset and playback speed; the analyzer must not silently claim
that a discontinuous song is aligned.

## Manual alignment and listening

- Choose **Time Step** and **Speed Step** independently: 0.1, 0.01, 0.001, or
  0.0001.
  The same native sliders used by Big Screen's basic timing controls provide
  a grab handle for coarse adjustment and arrow buttons for the selected fine
  precision. Values remain double precision internally.
- Choose **Set Offset and Speed** to edit Video Offset and Video Speed directly,
  or **Match Start and End Points** to fit matching
  map/video audio points. Only controls used by the selected method are shown.
  Map Audio Start/End are shown together, followed by Video Audio Start/End, so
  corresponding ranges are easy to compare. Point matching can speed up or slow
  down video.
- Listen with map left/video right, reversed ears, mixed audio, map only, or
  video only. Both tracks use one stereo output and one shared source clock.
- **Playback Speed**, beside the waveform transport timeline, temporarily slows both
  sources. It does not change the saved video speed. Their pitches lower
  together deliberately in this mode.
- Enable **Pitch Correction** and use **Set Automatically** for compensation
  based on video playback speed, then fine-tune within ±200 cents. This affects
  the synchronization preview only, not gameplay music.
- **Show Video** can be turned off to stop video decoding/uploads while
  listening. It is not merely hiding a still-running video renderer.
- **Loop Matched Section** repeats Map Audio Start through Map Audio End while
  auditioning. It does not loop the Beat Saber map during gameplay. **Hide After
  Video End** prevents content after Video Audio End from being shown. These
  options appear with the point-matching controls they depend on.
- The map and video peak waveforms use different colors and can be shown side
  by side, stacked, or overlaid. Cyan is map audio, magenta is video audio,
  yellow is playback, and green/red mark each source's start/end. In point
  matching mode the green/red lines are draggable; the numeric sliders provide
  precision nudges and exact values.
- Full Track uses a dense filled amplitude envelope like a conventional audio
  editor overview. Zoomed views switch to one higher-resolution amplitude
  trace built at the analyzer's native 32 ms cadence, so increasing zoom
  reveals additional peaks instead of enlarging the filled summary buckets.
  The source data is mono, so the zoomed trace is not duplicated below a
  centerline in a way that could be mistaken for separate stereo channels.
- **Wave Zoom** ranges from Full Track through even-numbered levels up to 20x.
  The highest level targets approximately five visible seconds. At zoom, the
  yellow playhead remains fixed while the waveforms move beneath it during
  playback, preserving the chosen detail instead of scrolling the cursor off
  the display.
- Graph Layout, Wave Zoom, and Wave Height use three compact label/dropdown
  groups on one row. The selectors are sized to their actual values rather than
  inheriting a half-panel width from BSML's stock dropdown layout.
- **Wave Height** stretches the existing viewing area vertically from Default
  through 2x, 4x, and 8x. It moves the following controls through normal scroll
  layout and does not rebuild, amplify, or otherwise change waveform data. The
  nested graph and outer scroll extents are rebuilt together after a height
  change so 4x/8x views cannot be clipped against the old content height. The
  waveform image and marker bars stretch to the viewport's resolved height, so
  Unity layout rounding cannot leave an expanding blank strip above the trace.
- Sync Workflow, Graph Layout, Wave Zoom, and Wave Height are saved with the
  map's Advanced Sync profile. The most recently accepted automatic result is
  also summarized with its confidence, proposed offset/speed, and match-point
  counts. Reopening the editor therefore restores the same working view and
  result context. Older version-1 records migrate with the original defaults;
  new records use the version-2 schema.
- The play/pause button and shared timeline sit directly beneath the waveforms,
  without a separate section heading that could become detached while scrolling.
- The yellow waveform playhead and the transport timeline are linked.
  Pressing or dragging either seeks both audio tracks and the video. While an
  audition is playing, a second bounded worker prepares the requested position
  while the current audio continues; the output switches only after the new
  quarter-second startup reserve is ready. Rapid controller movement is
  coalesced to the newest requested time rather than launching unbounded work.
  The overview remains a visual aid; numerical controls are more precise than
  its pixels. Marker transforms use explicit horizontal center anchors, stretch
  through the waveform viewport, and render above the waveform images,
  including at the exact beginning or end of a track. Aspect preservation is
  disabled for their square source sprite so it renders as a vertical bar
  rather than collapsing into a nearly invisible dot.
- Waveform layout refreshes derive the scroll extent from the active controls'
  preferred height rather than reusing the scroll view's previously assigned
  height. Repeatedly switching layouts or heights therefore cannot accumulate
  empty space above the waveforms.

## Saving, resetting and storage

Closing with edits asks to Save or Discard. A forced menu teardown or
master-switch shutdown cancels work and abandons unsaved edits.

Unlinking/removing a video clears its advanced settings and Big Screen-owned
companion audio. Unlink preserves the video itself, including any audio embedded
inside it. Delete File/Delete Video removes the video as described by the
existing confirmation.

Prepared mono audio is disk-backed, not full-track PCM retained in RAM. The
disposable **Audio Sync Cache** is limited to 128 MB with oldest-unused eviction.
Storage Maintenance lists unused entries separately; active analysis/audition
leases prevent cleanup deleting files still in use. Compressed downloaded
audio and saved map settings are not part of that eviction budget.

Initial compact-index limits are separately selectable in code for Quest
2/Pro/unknown models and Quest 3/3S. Those limits are safety bounds, not claims
of measured RAM or speed. Very long tracks can exceed a working/cache limit
and receive an explanatory error rather than unbounded allocation.

## Precision and testing

A circular reset button beside each of the six manual timing controls restores
that one value to the map's initial synchronization value without discarding
the other edits. **Time Precision** controls both the arrow step and the number
of decimal places shown by all six timing values. **Speed Precision** continues
to control the arrow step for Video Playback Speed; its displayed decimal count
still follows Time Precision so all timing readouts use one consistent format.

A 0.0001-second adjustment step does not guarantee sub-frame video presentation
or sample-accurate perceived timing. Audio output buffering, pitch processing,
headset refresh and video FPS all have physical limits. Host fixtures establish
known-answer algorithm behavior; audible output latency, capture timing and
real-song matching still need Quest tests.

The video preview follows Unity's continuously advancing audible source clock.
PCM callback counters are intentionally not used as a presentation clock:
Unity requests streaming audio in large blocks, so that counter can jump by
hundreds of milliseconds even while the audio itself plays smoothly.

Existing native logs, optional detailed menu/download sessions, error history,
frontmost dialogs, and the support-log collector remain the diagnostic paths.
