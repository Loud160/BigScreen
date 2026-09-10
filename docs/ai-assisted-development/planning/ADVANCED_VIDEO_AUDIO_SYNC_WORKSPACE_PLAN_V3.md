# Advanced Video and Audio Synchronization Workspace Plan

Status: alpha 14 first integrated live-test candidate built and packaged.
Host checks passed; Quest behavior, layout and audio latency remain unverified.
This document records the contract and evidence, not a release-readiness claim.

Last reviewed: September 6, 2026 (synchronization/DSP review and agent
workflow revision integrated; subsequent user decisions recorded below)

## Accepted follow-up decisions: September 6, 2026

The user refined V3 after reviewing it. This repository copy now incorporates
those decisions; the original supplied V3 remains unchanged in Downloads. The
following explicitly supersede earlier on-demand-download and draft proposals:

- New video downloads must also download and process matching audio. Opening
  the editor loads/prepares that stored audio for use; it does not normally
  initiate the first download of its audio.
- Advanced Sync is a per-map enable switch in the existing song/video preview
  panel, with a `Configure` button available when enabled. The workspace tab is
  named `Audio Sync`.
- Enabling Advanced Sync disables both basic timing sliders and both basic
  toggles. The center workspace owns advanced timing edits.
- Advanced changes are saved explicitly with Apply. Closing the center panel
  with unsaved changes asks whether to Save or Discard. Reopening starts from
  the previously saved advanced configuration.
- End markers support both fitting-only and stop-at-marker behavior through a
  switch, so the user can test both.
- Audio preparation starts automatically and shows a cancellable progress
  popup. Normal video preview remains available during processing.
- While the center Audio Sync panel is open, disable the button for returning
  to the song list and prevent changing the selected map/video. The user closes
  the center panel, resolving Save/Discard first, before leaving the map.
- Legacy downloaded videos without matching audio do not require an automatic
  backfill/migration. Keep the Advanced Sync switch clickable. Attempting to
  enable it without matching audio shows a detailed popup and returns the
  switch to Off, explaining how to obtain usable audio for that source.
- Storing audio inside the media or alongside it is an implementation choice.
  The additional size of compressed audio is acceptable to the user.
- Advanced Sync starts off for newly assigned videos. Switching it off restores
  the map's basic controls and saved basic playback settings, while retaining
  the saved advanced/audio configuration for later use.
- Provide a per-track master reset inside Audio Sync that returns its advanced
  configuration to starting values.
- Removing a video also removes its saved advanced/audio configuration and any
  associated Big Screen-owned audio file. The deletion confirmation must
  explicitly describe that cleanup.
- If audio download or preparation fails but the video is valid, basic playback
  remains available. Show the actual audio failure through a popup matching
  Big Screen's established dialogs.
- Downloaded and locally supplied videos without audio retain the existing
  basic adjustment behavior.
- The clickable-switch popup explicitly supersedes the earlier disabled-switch
  and hover-hint design. Missing audio prevents activation, not interaction
  with the switch that explains why activation is unavailable.
- Show High/Medium/Low match confidence and useful measured timing residuals.
  A confidence percentage is desired if calibration supports an honest one;
  do not invent a probability from a raw correlation score.
- Pitch fine adjustment is -200 to +200 cents around automatic compensation,
  in one-cent steps. Speed gets a separate precision selector with 0.1x,
  0.01x, 0.001x, and 0.0001x increments.
- Default end-marker behavior is fitting-only. Enabling Stop at End Marker
  overrides the mapper cutoff while Advanced Sync is active; disabling it
  restores normal mapper cutoff behavior. The user will judge this on Quest.
- Begin with a 128 MB disk cache for disposable analysis/visualization data,
  with least-recently-used eviction and a Manage Storage cleanup action. Stored
  audio and saved settings are excluded. Support device-specific working-memory
  profiles and measure limits on Quest 2 before increasing them elsewhere.
- Disabling map navigation while the center panel is open supersedes the
  earlier navigate-away confirmation for that state. Existing cancellation
  warnings still apply if audio work continues outside the open center editor.

Implementation is authorized. The switch-off, audio-failure, and no-audio
basic-mode decisions are resolved. Remaining decisions records later-stage
choices and technical measurements. The earlier proposal for the right-side
timing controls to edit the same draft is superseded: those controls are
disabled while Advanced Sync is enabled.

## Purpose

This document preserves the complete design discussion for adding an advanced
per-map synchronization workspace to Big Screen. It is intentionally detailed
because the implementation will span multiple development sessions and context
compactions. Future work should treat the decisions marked **Locked** as the
agreed product behavior unless a later discussion explicitly changes them.

Recommendations, implementation sketches, and values that have not yet been
confirmed are marked **Recommended**, **Proposed**, or **Open**. They are not
permission to expand the feature silently. They exist to prevent future work
from losing relevant constraints, repeating earlier investigation, or creating
incompatible one-off systems.

The central goal is to make it substantially easier to synchronize a video's
audio with a Beat Saber map while retaining the manual precision requested by
advanced users. The workspace must be usable by a normal player, must not block
or destabilize Beat Saber's menu, and must integrate with Big Screen's existing
timing, logging, error handling, and retained-menu architecture.

## How implementation agents must use this document

This plan is both the design record and the implementation contract. The work
will span multiple sessions, agents, and context compactions, so the following
operating rules are themselves requirements:

1. Re-read this document from disk at the start of every work session and
   after any context compaction. Do not rely on a summarized memory of it. At
   minimum, re-read Purpose, Non-goals, every **Locked** section, the current
   stage, and Remaining decisions before writing code.
2. Implement in dependency order, but per the user's subsequent instruction,
   continue across stages using builds, fixtures, and automated review without
   pausing for intermediate user tests. Defer user headset testing until the
   integrated feature is ready to exercise. Record implemented/host-tested and
   Quest-validated status separately; deferred exit criteria remain outstanding.
3. Update the Progress log at the end of every session: what was done, what
   merely compiled versus what was verified on Quest, and any new questions.
   On-device behavior claims require Quest evidence; compilation alone proves
   nothing.
4. Never reword, weaken, delete, or summarize away a **Locked** requirement.
   If a Locked requirement appears impossible or unsafe, stop, record the
   evidence in this document, and raise it for discussion per the
   change-control note.
5. Remaining decisions are classified `[Ask]` or `[Measure]`. `[Ask]` items
   are product decisions: stop and ask before implementing either way.
   `[Measure]` items may be resolved by the agent through code inspection,
   fixtures, or Quest measurement, and the chosen answer plus its evidence
   must be recorded in this document. Neither kind may be guessed silently.
6. If this document and the code disagree, this document wins unless the
   Progress log records an agreed change.

Current stage status (agents update this table as stages progress):

| Stage | Status |
|---|---|
| 1. Data model and pure timing logic | Implemented and host-tested; source-bound library overlay, separate basic settings, explicit draft save/discard/reset. Quest persistence checks pending. |
| 2. Center workspace shell | Implemented: retained, staged native UI creation, tab host, scroll body, per-map switch/Configure and navigation ownership. Quest layout/input checks pending. |
| 3. Audio source discovery and cache | Implemented: matching compressed companion acquisition, file decode, resident clip copy/streaming capture fallback, 128 MB pinned disk PCM cache and storage cleanup. Quest acquisition/capture checks pending. |
| 4. Waveforms, spectrogram data, and shared transport | Implemented: worker-generated waveform/frequency overviews, source markers and common-clock stereo audition. Quest latency/visual checks pending. |
| 5. Manual routing and marker workflow | Implemented: five monitoring routes, separate precision selectors, manual rate/marker fitting, fitting-only or stop-at-end, save/reset. Quest control checks pending. |
| 6. Automatic matching | Implemented and integrated: bounded coarse candidates, rate-aware refinement, robust fit and held-out confidence. Expanded positive/negative fixtures and integration validation recorded below. Real-song calibration pending. |
| 7. Audition speed, pitch correction, and picture suspension | Implemented: common slowdown, private Sonic pitch DSP, Auto/fine cents and decoder-stop picture suspension. Host clock/duration tests; Quest sound/performance pending. |
| 8. Hardening and integration audit | Integration review covers lifetime, cancellation, cache quota/leases, scene recovery, guarded callbacks and matching failure paths. Final build/test/package evidence is recorded below; not a claim of Quest validation. |

## Scope

The first implementation is an **Audio Sync** workspace for the map currently
open in Big Screen's video editor. It will support:

- automatic alignment using multiple samples from the map and video audio;
- manual alignment using waveforms, spectrogram information, timelines, markers,
  and controlled audio auditioning;
- adjustment of the existing video start offset and playback speed;
- detection and use of meaningful audio endpoints rather than relying only on
  container duration;
- optional selection of the video's stopping point;
- video-audio pitch correction during manual auditioning;
- user-selectable analysis effort and manual adjustment precision;
- a mode that disables the rendered video while the user works only with
  audio, reducing decoder, upload, GPU, and memory load.

The center workspace must be designed as a tabbed host immediately, even though
Audio Sync is the only tab implemented now. Future per-map Video, Screen, and
Environment settings are specifically anticipated but are **not** part of this
implementation.

## Non-goals for the first implementation

- Do not add multiplayer synchronization support.
- Do not move the existing per-map video, screen, or environment controls into
  the center workspace yet.
- Do not add visible empty or disabled tabs merely to advertise future work.
- Do not upload either audio track to a server or depend on a cloud alignment
  service.
- Do not replace Beat Saber's authoritative song clock.
- Do not change the existing gameplay video-audio policy. Video audio is for
  synchronization auditioning; Beat Saber's map audio remains the audio heard
  during normal play unless a separate future feature explicitly changes that.
- Do not bulk-download audio for existing library entries. New video downloads
  include matching audio under the accepted follow-up requirements; existing
  entries without audio become eligible after the user downloads them again.
- Do not make the menu wait synchronously for audio decoding, waveform
  generation, analysis, file I/O, or a network request.
- Do not interpret a high numerical precision setting as a guarantee that a
  video frame can physically be presented with sub-frame accuracy.

## Locked product decisions

The following points were explicitly agreed during design discussion.

### Workspace placement and behavior

1. Advanced Sync opens in Big Screen's **center menu workspace**, not in a
   floating or grabbable panel.
2. The center workspace uses the normal full-size center HMUI panel, comparable
   to Saber Stage's center control area. It must not be squeezed into the left
   or right side-panel dimensions.
3. The existing large world-space video screen remains visible enough for the
   user to judge results while making adjustments.
4. The workspace is modeless with respect to the right-side video editor and
   preview. Opening it must not behave like a blocking dialog.
5. Closing Advanced Sync returns the center to Big Screen's normal neutral
   center view and returns the user to the same selected map and right-side
   video editor state.
6. Opening Advanced Sync must not stop and recreate the current preview merely
   because a center page was opened.

### Tab host and scrolling

1. The center workspace is a tab group from its first implementation.
2. `Audio Sync` is the only visible tab initially.
3. The internal structure must allow future Video, Screen, and Environment
   tabs to be added without reconstructing Audio Sync or migrating every
   existing control into a new container.
4. The Audio Sync page is built inside a native scroll container from the
   start. If controls outgrow the available vertical space, the page must
   scroll instead of compressing text, clipping buttons, or forcing a later
   layout rewrite.
5. Text and pointer targets must remain comfortably readable and usable from
   several meters away in VR.

### Per-map enablement and basic-control gating

1. Add an `Advanced Sync` toggle to the selected map's existing video preview
   panel. Provide a `Configure` button when the toggle is enabled.
2. Both the enable state and the saved advanced configuration belong to that
   map, not to global Misc settings or a shared current-video variable.
3. When enabled, disable the existing `Fit to Song` and `Lead-In Background`
   toggles and the `Playback Speed` and `Video Playback Offset` sliders,
   including their related reset/nudge actions. Preview transport remains
   available. No disabled basic control may save or override advanced timing.
4. Applying advanced changes saves the selected map's settings. Reopening its
   editor starts from those saved values, including marker/mode information
   needed to continue editing rather than just the derived offset and rate.
5. Keep the Advanced Sync toggle clickable when matching audio is missing.
   Attempting to turn it on shows a detailed popup explaining that Advanced
   Sync is unavailable because there is no usable associated audio. Restore
   the toggle to Off without invoking its change callback recursively. Basic
   controls remain available and Configure remains unavailable. Do not commit
   an enabled state, start an advanced session, or disable basic controls before
   audio availability has been validated.
6. Determine eligibility from the assigned media and its validated audio, not
   merely whether an MP4 container includes an audio stream: separate audio
   files are permitted.
7. The end-marker switch selects fitting-only or stop-at-marker behavior. Both
   modes must be implemented and available for the user's comparison.

8. Advanced Sync starts off for newly assigned videos. Switching it off restores
   the map's separately retained basic settings and enables the basic controls;
   it retains the saved advanced/audio configuration for later use.
9. Enabling Advanced Sync again restores that map's saved advanced profile.
   Basic-mode edits must not overwrite the retained advanced profile, and
   advanced-mode edits must not overwrite the retained basic profile.
10. Missing or failed audio prevents Advanced Sync activation without disabling
    its explanatory toggle, basic playback, or basic timing controls, for both
    downloaded and locally supplied media. A rejected enable attempt must not
    erase any retained advanced configuration.
11. Include a master reset for the selected track inside Audio Sync. Removing
    the track's video clears the associated saved advanced/audio configuration
    and Big Screen-owned audio companion files as described under cleanup.
12. While the center panel is open, disable Back to Song List and prevent all
    equivalent map/source-selection actions from changing the editing target.
    The close button at the top of the center panel remains available, with
    the agreed Save/Discard behavior. Restore navigation after successful
    close, and on recovery/teardown. This must not strand the user in the menu.

### Automatic and manual modes

Follow-up: Automatic-to-Manual retains the calculated offset, playback rate,
and corresponding markers in the same draft for fine tuning. Manual-to-Automatic
does not feed those manual values into the analyzer: a new Analyze operation
computes independently from source audio. Switching modes alone neither saves
nor destructively clears the draft; only previewing a new proposal replaces its
timing, and Apply/Save remains required for persistence.

1. A mode selector at the top of Audio Sync switches between `Automatic` and
   `Manual` modes.
2. Mode-specific controls are shown only for the selected mode so the workspace
   does not present every advanced option at once.
3. The waveform/spectrogram displays, transport controls, and timeline scrubber belong
   below the mode-specific controls and remain relevant to both modes where
   practical.
4. Automatic mode must be capable of changing video start offset and playback
   speed and must inspect the track near its end so it can detect accumulated
   drift.
5. Manual mode must allow the user to set matching start and ending points.
6. Manual mode must support setting playback speed directly or calculating it
   from the selected map/video start and end markers.
7. Calculated playback may speed the video up or slow it down.

### Automatic analysis controls

1. The user can choose the desired number of useful synchronization anchors.
2. The user can choose the duration of the high-resolution refinement/audition
   window associated with those anchors.
3. More anchors and longer refinement windows may take longer to analyze; that
   cost is an intentional user-selected tradeoff.
4. The selected anchor count is a target for useful accepted anchors, not a
   command to inspect exactly that many equally spaced positions. The analyzer
   may inspect more inexpensive candidate regions and retain the strongest,
   spatially separated evidence.
5. The analyzer must consider meaningful audio near the end of both tracks,
   rather than aligning only the beginning and assuming the remainder stays in
   sync.
6. Automatic matching must preserve each anchor's authoritative source time. A
   detected active-audio boundary may guide sampling but must never silently
   re-zero either source timeline.

### Manual auditioning

1. The user can audition map audio in one ear and video audio in the other.
2. The channels can be reversed.
3. The user can mix both sources into both ears.
4. Map-only and video-only monitoring should also be available because they are
   useful for locating landmarks and diagnosing a poor source.
5. Both audio tracks can be slowed together for careful listening. This
   audition speed is temporary and must not overwrite the saved video playback
   speed.
6. A switch can turn off visual video playback during synchronization. This is
   intended to reduce CPU, GPU, upload, and memory load when only the audio is
   needed.
7. Both audition sources must ultimately be scheduled from one explicit
   audition clock and must use the same final output timing path where
   practical. If separate output paths introduce a deterministic latency
   difference, that difference must be measured and compensated before the UI
   presents millisecond/sub-millisecond manual alignment as meaningful.

### Timeline-origin and source-timing invariants

1. Beat Saber's effective song clock remains the authoritative map timeline.
2. Video analysis results must be expressed on Big Screen's actual video media
   timeline, not merely as decoded-audio sample indexes.
3. Detecting meaningful audio start/end points must never trim or re-zero the
   source timeline. If meaningful map music begins at song time `5.250`, an
   anchor extracted there remains at song time `5.250`.
4. Intentional map lead-in silence or dead time must therefore remain part of
   synchronization. Automatic analysis may skip that silence when choosing
   useful features, but the fitted timing must preserve it.
5. Legacy map-level song timing metadata must be honored exactly once. Big
   Screen must use Beat Saber's effective runtime song/audio relationship where
   possible and must not blindly add a parsed map offset that Beat Saber has
   already incorporated.
6. Audio decoder/container timing such as presentation timestamps, stream start
   time, encoder delay, AAC priming, or Opus pre-skip must not create a hidden
   shift between analysis time and video media time.

### Precision and pitch correction

1. Manual time-adjustment precision must offer `0.1`, `0.01`, `0.001`, and
   `0.0001` second steps.
2. Video-audio pitch correction is optional and controlled by a switch.
3. A manual pitch adjustment slider is available when pitch correction is
   enabled.
4. An `Auto` button beside that slider calculates the compensation implied by
   the current video playback speed.
5. After automatic compensation, the user can fine-tune the pitch manually.

## Current Big Screen behavior and constraints

This section records the current architecture so implementation does not begin
from incorrect assumptions.

### The center controller already exists

`MenuFlowCoordinator` creates and retains a center `HMUI::ViewController`. On
normal activation it intentionally supplies that empty controller as the main
view so the user's forward view remains clear. Storage Maintenance, Showcase,
the Local Video Browser, and the Thumbnail Picker temporarily replace the same
center controller.

Advanced Sync should use the same retained center-controller lifecycle. It
must not create an unrelated floating screen or a second flow coordinator.
Unlike the existing Local Video Browser and Thumbnail Picker transitions, its
open callback must **not** call `StopActivePreview()`, because continuous
preview and auditioning are core requirements.

The existing staged menu prewarmer creates retained controllers and then builds
individual pages over separated main-thread frames. The Advanced Sync shell
must join that process instead of moving all of its UI construction back to
first entry.

### Current timing model

Big Screen currently maps Beat Saber's song clock to video media time as:

```text
videoMediaSeconds = songSeconds * playbackRate + offsetSeconds
```

This mapping must remain authoritative. The Advanced Sync engine produces or
edits values for the established `offsetSeconds` and `playbackRate` fields; it
must not create a competing timing loop.

Given two user-selected matching points:

```text
map start marker   = S0
map end marker     = S1
video start marker = V0
video end marker   = V1
```

the marker-fit mode can calculate:

```text
playbackRate = (V1 - V0) / (S1 - S0)
offsetSeconds = V0 - playbackRate * S0
```

Those formulas match the current media-time relationship. They naturally
support both rates above `1.0` and below `1.0`.

Automatic matching uses the same model. Each accepted correspondence is an
anchor `(S, V)` where `S` is the anchor's authoritative Beat Saber song-clock
time and `V` is the corresponding video media time. The analyzer fits:

```text
V = playbackRate * S + offsetSeconds
```

The acquisition and decode layers must preserve enough timing information to
construct those authoritative coordinates correctly. In particular, code must
not assume that decoded audio sample zero necessarily equals video media time
zero. Container/codec timing can include a non-zero stream start, PTS origin,
encoder delay, AAC priming, Opus pre-skip, or similar metadata. Analysis sample
positions must be translated back to Big Screen's video media timeline before
being used as `V` anchors.

The same rule applies to map audio. A detected meaningful-audio start is not a
new song-time origin. For example, if a map intentionally contains five seconds
of silence and matching music begins at song time `5.000`, a feature extracted
from that music remains anchored at `S = 5.000`; it does not become `S = 0`.
That preserves mapper/player lead-in time automatically.

Older map formats may contain explicit song-time offset metadata. Advanced Sync
must first determine how Beat Saber has resolved that map's effective runtime
song/audio relationship. It must not blindly parse and add a legacy offset on
top of a song clock that already includes it. Relevant legacy timing must be
reflected exactly once. The implementation should verify the runtime timing
path for OST, DLC, custom, WIP, and supported legacy maps before encoding any
format-specific correction.

`Fit to Song` already recalculates playback speed from duration. While Advanced
Sync is enabled, its saved/draft timing is authoritative and the basic Fit to
Song calculation must not run. Both basic toggles and both basic sliders are
disabled. Preserve any basic-mode values needed for a later switch back rather
than silently overwriting them. Turning Advanced Sync off restores that saved
basic-mode profile while retaining the advanced configuration.

`MapVideoConfig` supports a mapper-provided `endVideoAt` value through
`stopAtVideoSecond`. The user library currently persists offset, playback rate,
Fit to Song, and lead-in behavior, but does not persist an equivalent
user-authored stop marker. The user has now requested a switch supporting both
fitting-only and actual playback-cutoff behavior. The per-map schema therefore
needs the selected marker and the stop-at-marker flag, with compatible defaults
for old records. A fitting marker must not become a playback cutoff unless
that switch is enabled. Its initial value is Off: fitting-only leaves normal
mapper stopping behavior intact. When the user enables it, the saved advanced
end marker replaces the mapper cutoff while Advanced Sync is active. Switching
Advanced Sync off restores the normal basic/mapper policy. Keep source-end/EOF
safety independently enforced. This behavior is accepted for initial testing
and may be refined following the user's on-device comparison.

### Current downloads do not include audio

This describes the current code baseline, not the newly approved target
behavior. New downloads will include matching audio as described below.

The YouTube format selector deliberately accepts only formats whose `acodec` is
`none`. Big Screen therefore downloads video-only streams. This is useful for
normal playback because it avoids downloading bytes the existing video player
does not use, but it means the Advanced Sync feature cannot assume that a
managed YouTube video file contains an audio track.

The current remuxer and last-resort transcoder also create video-only output.
A matching separate audio file is still recommended because it keeps those
video repair paths focused. The user also permits embedded audio if that proves
the better implementation; additional compressed-audio storage is acceptable.

### Current private FFmpeg builds are video-focused

The private FFmpeg builds currently disable `swresample` and enable video
decoders and the media containers needed by Big Screen. They do not yet provide
the complete audio-decode/resample surface needed by this workspace.

The audio implementation will need a deliberately bounded addition. At minimum
it should support the codecs and containers actually selected for the
downloaded YouTube audio sidecar. Broader local-file support will likely require
AAC, Opus, Vorbis, and MP3 decoding plus resampling. The exact enabled set must
be verified against real YouTube, local MP4, and local WebM fixtures rather than
enabling all FFmpeg components without review.

### Map audio is owned by Beat Saber

The menu preview currently uses Beat Saber's `SongPreviewPlayer` and active
`AudioClip`. Custom, WIP, OST, and DLC songs do not all provide a normal loose
audio file that Big Screen can open by path.

The acquisition layer therefore needs to support Beat Saber's loaded audio
objects. Any Unity `AudioClip` access must happen on the main thread. It should
copy only bounded PCM windows or bounded chunks needed to produce compact
analysis features into native buffers, then hand those ordinary buffers to
background analysis. It must not retain or duplicate an entire multi-minute
stereo floating-point clip unless measurement proves that to be safe on Quest
2.

The map-audio acquisition layer must also preserve the relationship between
sample positions and Beat Saber's authoritative song clock. Active-audio
endpoint detection may identify that useful music begins later than time zero,
but it must not silently re-zero extracted features. Before implementation,
inspect the runtime timing objects/code paths used for OST, DLC, custom, WIP,
and relevant legacy map formats so any map-level song-time offset is honored
exactly once rather than ignored or double-applied.

## Proposed center-workspace architecture

### Controller ownership

Add one retained Advanced Sync center ViewController owned by
`MenuFlowCoordinator`. A dedicated controller/service should own the page's UI
state and media-analysis session; it should not add another collection of
uncoordinated static callbacks to `VideoLibraryMenu`.

Suggested responsibilities:

- `MenuFlowCoordinator`
  - creates and prewarms the retained center controller;
  - performs safe center-stack replacement;
  - restores the neutral center controller on close;
  - cancels the workspace during full menu teardown or recovery;
  - never stops the preview merely to open Advanced Sync.
- `AdvancedSyncMenu` or equivalent UI owner
  - builds the header, tabs, scroll page, controls, and page-owned modals;
  - holds weak Unity references and clears them on scene invalidation;
  - turns immutable worker results into main-thread UI updates;
  - owns a draft editing session for one selected map.
- `AudioSyncSession`
  - contains no direct Unity UI ownership;
  - tracks selected map/video identity, generation, cancellation, markers,
    audition routing, and draft timing;
  - coordinates audio acquisition and the analysis engine;
  - rejects results that belong to an old map or old operation generation.
- `AudioSyncAnalyzer`
  - is testable native logic for feature extraction, matching, outlier
    rejection, endpoint detection, and affine timing fit;
  - never touches Unity objects.
- An audio asset owner and `AudioSyncCache`, or carefully scoped extensions of
  `VideoLibrary`
  - track successful downloaded audio separately from disposable derived
    waveform/features;
  - owns safe temporary-file and cleanup policy;
  - never deletes a user-owned external video or audio file.

These are responsibility boundaries, not mandatory class names.

### Tab registry

Use a small tab descriptor/registry rather than hardcoding a chain of unrelated
index checks. Initially it registers only:

```text
Audio Sync
```

The host can later register Video, Screen, or Environment pages without
changing Audio Sync's internal hierarchy. Future tab identifiers may be
reserved in code, but unfinished tabs should not be displayed to users.

Changing tabs in the future should activate retained page roots rather than
destroying and rebuilding controls. Tab changes must not implicitly stop the
preview, decoder, analysis session, or audio unless the destination tab's
documented ownership requires it.

### Layout hierarchy

Recommended high-level hierarchy:

```text
Advanced Sync center ViewController
|-- Fixed header
|   |-- Title: Advanced Video Sync
|   |-- Selected song/video identity
|   `-- Close button
|-- Fixed native segmented tab selector
`-- Active-tab viewport
    `-- Audio Sync native scroll container
        |-- Audio-source readiness/status
        |-- Automatic / Manual mode selector
        |-- Active mode controls
        |-- Proposed/current timing summary
        |-- Map and video waveform/spectrogram displays
        |-- Shared transport controls
        |-- Shared timeline scrubber
        `-- Apply / Revert actions
```

The header and tab strip remain fixed. All Audio Sync content belongs to the
scroll page from the beginning. If later testing shows that transport controls
must remain visible while the upper page scrolls, a fixed transport footer can
be introduced without changing the tab host; that behavior has not yet been
locked.

Use the normal wide center canvas and stock HMUI/BSML controls wherever
possible. Do not copy Saber Stage's `54`-unit side-panel widths. Big Screen's
existing center pages already use widths in the approximate `104`-to-`124`
unit range, which is a more relevant starting point. Exact dimensions must be
validated from a Quest screenshot and controller interaction, not accepted
solely because the page compiles.

The center background should be translucent enough that the user retains
spatial awareness of the video screen, but opaque enough that waveform lines,
labels, and markers remain readable over bright video. Avoid fully transparent
text regions and avoid a large solid panel that unnecessarily hides the
preview.

### Proposed Audio Sync page organization

#### Common source header

Show concise state for:

- map audio ready/loading/error;
- video audio ready/loading/downloading/extracting/error;
- audio duration and detected active-audio range for each source;
- whether the assigned video is managed, mapper-local, imported, or external;
- whether an existing cached sidecar can be reused.

Long technical detail belongs in a popup opened from a clear `Show Error` or
`Details` action, not in a clipped single-line status label.

#### Automatic mode

Recommended controls:

- an analysis preset dropdown;
- desired accepted-anchor count;
- high-resolution refinement/sample duration;
- `Analyze` and `Cancel` buttons;
- visible progress with the current stage and completed/total work;
- result confidence and ambiguity status;
- proposed start offset and playback speed;
- start, middle, and end residual/error summary;
- held-out validation summary;
- `Preview Result`, `Apply`, and `Discard` actions.

Recommended bounded presets:

| Preset | Desired accepted anchors | Refinement duration | Purpose |
|---|---:|---:|---|
| Fast | 5 | 5 seconds | Quick estimate with enough redundancy to reject one weak anchor |
| Balanced | 8 | 8 seconds | Default accuracy/cost balance |
| Thorough | 12-16 | 10 seconds | Stronger ambiguity, outlier, and drift resistance |
| Custom | bounded user range | 2-20 seconds | User-selected cost/accuracy tradeoff |

These bounds and defaults are proposed, not locked. Quest 2 measurements may
justify changing them. The selected anchor count means the desired number of
useful accepted anchors. The analyzer may inspect substantially more cheap
candidate regions before selecting those anchors.

Automatic sampling should not simply divide container duration into equal
positions and trust every window. It should:

1. detect and avoid long silence, low-energy sections, or featureless tones;
2. prefer distinct musical regions with useful transient/spectral information;
3. consider more candidate regions than the requested accepted-anchor count;
4. retain spatially separated evidence, including useful regions near the start,
   middle, and meaningful audio end;
5. preserve every selected region's authoritative source timestamp rather than
   re-zeroing it to an active-audio boundary;
6. search against a bounded low-resolution whole-track feature index rather than
   retaining whole-track PCM;
7. retain a small bounded set of plausible coarse matches for an anchor when
   repeated material makes several locations credible;
8. use cross-anchor/global timing consistency to disambiguate repeated choruses
   before expensive refinement;
9. refine predicted regions with localized normalized cross-correlation or an
   equivalently measured high-resolution matcher;
10. assign strength and ambiguity information to each refined anchor;
11. reject weak matches and statistical outliers;
12. fit the remaining `(song time, video media time)` anchors to the current
    affine timing equation;
13. validate the result against meaningful windows that were not used to derive
    the final fit;
14. inspect residual shape for evidence that the source cannot be represented by
    one offset and one playback rate;
15. report low confidence or non-affine/discontinuous timing honestly rather
    than applying a questionable result.

The user must always be able to preview and reject an automatic result.

#### Manual mode

Recommended controls:

- output routing: `Map Left / Video Right`, `Video Left / Map Right`, `Mixed`,
  `Map Only`, and `Video Only`;
- `Show Video While Syncing` switch;
- adjustment precision dropdown;
- timing method: `Manual Playback Speed` or `Fit Between Markers`;
- map start/end markers;
- video start/end markers;
- explicit playback-speed control in manual-speed mode;
- calculated playback rate in marker-fit mode;
- optional `Stop Video at End Marker` behavior;
- pitch-correction switch, `Auto` button, and fine-adjustment slider;
- audition-speed slider for slowing both sources together;
- loop-selection option for repeatedly auditioning a short region.

The map and video timelines should resemble a simple non-linear editor: two
clearly labeled horizontal lanes, draggable in/out markers, current-playhead
indicator, visible time values, waveform data, an optional time-aligned
spectrogram view, and an understandable indication of which markers are linked.
The design does not need trimming or editing features beyond synchronization.

### Adjustment precision

The locked precision values are seconds per nudge:

```text
0.1 s     = 100 ms
0.01 s    = 10 ms
0.001 s   = 1 ms
0.0001 s  = 0.1 ms
```

The selector should control arrow/button nudges for time markers and offset.
Dragging remains a coarse positioning method. Exact numeric text should be
shown beside the selected marker.

**Locked follow-up:** speed has a separate precision selector with `0.1x`,
`0.01x`, `0.001x`, and `0.0001x` increments. Time adjustments retain their
separate seconds-based selector. Clearly show units on both.

The UI must explain that input precision is not presentation accuracy. A 60 FPS
video exposes a new frame about every `16.67 ms`; a 25 FPS video exposes one
about every `40 ms`. Beat Saber's changing render cadence and Unity's audio
buffer also limit when a result is perceived. Big Screen can store and calculate
sub-millisecond values without claiming that the headset can display a video
frame at an arbitrary `0.1 ms` boundary.

## Audio acquisition design

### Managed YouTube video

**Locked follow-up:** new video downloads acquire and process matching audio as
part of the download operation. Opening Audio Sync automatically loads/prepares
the stored audio for the editing session and shows a progress popup with Cancel.
Do not silently implement first-time audio downloads only on editor open.

Recommended storage: a separate compressed audio file associated with the
downloaded video. Embedded audio is also permitted. Use the same canonical
YouTube identity, retain timing provenance, and validate that the audio belongs
to the video before marking Advanced Sync eligible.

For an existing downloaded video without matching audio, keep basic playback
available and Advanced Sync off. Its toggle remains clickable; an enable
attempt explains in a popup that removing and downloading the video again will
obtain audio, then returns the toggle to Off. A bulk migration is unnecessary.
Do not delete an existing video automatically to enforce this policy.

Loading audio into RAM begins when the center panel opens. This does not
require retaining whole-track PCM: use bounded decode buffers, an audition
ring buffer, and compact cached features. Session audio buffers are released
when the panel closes or its session is invalidated.

Advantages:

- the video and audio can be downloaded independently under one visible job;
- the established video validation/remux/transcode pipeline remains isolated;
- the stored audio is reusable for later adjustments without downloading again;
- audio can use the most appropriate available stream independently of the
  selected video resolution;
- deleting or replacing a video can cleanly invalidate its associated analysis
  data.

Prefer one predictable audio format that the private FFmpeg build supports.
AAC in an M4A/MP4 container is a practical first choice when YouTube exposes it.
Opus may be retained as a fallback if testing shows it is necessary. The
selection policy should be explicit and logged.

The sidecar's decoded samples are analysis material; they are not automatically
identical to Big Screen video-media timestamps. Preserve stream/container timing
information needed to map sidecar audio positions back to the corresponding
video media timeline. Account for non-zero stream starts, presentation
timestamps, encoder delay/priming, Opus pre-skip, or equivalent behavior exposed
by the selected container/decoder. Test that mapping with fixtures rather than
assuming sample zero equals video time zero.

Also preserve the transformation performed by Big Screen's video repair path:
the current remuxer subtracts the input video stream's start timestamp when
writing output packets. Audio must ultimately align with the final installed
video, including any remux/transcode rebasing, not just the original URL's
timestamps. This is a source-to-final-media identity/timing requirement.

Download progress, cancellation, bounded retries, low-storage checks, source
identity, partial-file identity, and stale-operation rejection must follow the
same hardened rules as the existing YouTube video downloader. A failed or
cancelled audio-sidecar download must not leave an untracked `.part` file or a
false successful cache record.

If video succeeds but audio download/preparation fails, retain the valid video
for basic playback and keep Advanced Sync unavailable. Show a popup matching
the existing Big Screen dialog style with the actual audio failure and a clear
statement that the video can still use basic playback. Log the technical cause
through the existing logger/error facilities. Publish audio readiness
explicitly; never report Advanced Sync ready while audio is incomplete.

Cancellation is not an error and must not produce a misleading failure popup.
Partial audio artifacts must be cleaned up. A stale completion for a map the
user has already left must not open a popup over a different map; preserve its
diagnostic without reviving an obsolete operation.

### Mapper-local, imported, and external video

If the assigned file contains an audio stream, the workspace can decode it
directly without altering the video. It must not rewrite a mapper's or user's
source file merely to extract analysis audio.

The decoder must preserve the audio stream's relationship to the video's media
timeline. A local file with a non-zero audio stream start, edit list, priming,
or other timing metadata must not be shifted merely because decoded PCM begins
at sample index zero.

If the file has no audio stream:

- Advanced Sync cannot activate unless a validated matching audio companion
  exists; its toggle remains clickable to show the explanatory popup;
- basic video playback remains available;
- the UI explains the missing audio. For a user-owned local file, recommending
  an audio-containing replacement is more appropriate than claiming Big Screen
  can download that file again. Keep the wording specific to the source; basic
  adjustments remain exactly as available for videos without audio today.

If the local file contains an unsupported or malformed audio stream, report the
actual container/codec failure and leave the existing video assignment intact.

### Map audio

The feature must work with OST, DLC, custom, and WIP maps. It cannot rely solely
on a custom-song file path.

Recommended sequence:

1. obtain or await the selected map's Beat Saber preview/runtime `AudioClip`
   through the existing main-thread menu ownership;
2. resolve the clip/sample positions against Beat Saber's authoritative song
   timeline, including any effective legacy map timing exactly once;
3. identify requested candidate/refinement windows from duration and analysis
   policy without treating detected active-audio start as time zero;
4. copy bounded windows or bounded sequential chunks on the Unity main thread;
5. immediately hand ordinary native buffers plus their source-time metadata to
   a background worker;
6. release Unity references when the session/map generation changes;
7. cache compact waveform/spectrogram/features rather than retaining full PCM.

Unity imposes a hard constraint on direct sample access: `AudioClip.GetData`
returns usable samples only when the clip's sample data is resident in memory
(for example a `DecompressOnLoad` load type). Clips created with a streaming
load type -- a common way song audio is loaded -- return no usable data from
`GetData`. The acquisition layer must therefore check each clip's load
type/state at runtime and must not assume direct sample access works for every
map category. Expected fallback order:

1. custom and WIP maps: decode the map's own audio file (typically Ogg
   Vorbis) directly through the bounded FFmpeg audio build, which local-video
   audio support requires anyway;
2. OST and DLC maps: use `GetData` when the loaded clip permits it;
3. last resort: a bounded, effectively muted sequential capture pass through
   an audio-thread filter tap that copies samples and zeroes its output,
   explicitly measured for timing fidelity before it is trusted.

Which categories require which path must be verified on device and recorded in
this document rather than assumed.

If a selected source cannot provide random-access clip data, add an explicit
fallback acquisition path and test it on the affected map category. A bounded
sequential pass is acceptable when needed to construct compact whole-track
features, but Unity audio APIs must never be called from the analysis worker.

Before locking implementation details, verify how Beat Saber exposes effective
song/audio timing for OST, DLC, custom, WIP, and supported legacy maps. Do not
blindly add a parsed map metadata offset if the runtime song clock has already
incorporated it.

### Cache and cleanup policy

Recommended cache identity includes:

- normalized video source identity or a local-file fingerprint;
- assigned file size and modification time where applicable;
- audio stream identifier/codec;
- relevant stream timestamp/start-time identity;
- analysis format version;
- sample rate/channel policy;
- analyzer algorithm version.

Successful downloaded audio companions are persistent supporting media for the
video. They must not be silently evicted by a derived-data cache limit, making
Advanced Sync unavailable until the user downloads the video again. Derived
waveforms, spectrogram tiles, whole-track coarse features, and refined anchor
features are compact, replaceable caches. The storage UI must distinguish the
two categories.

The initial disposable disk-cache limit is 128 MB. Evict the least recently
used eligible entries and provide a dedicated cleanup action in Manage
Storage. Downloaded audio and saved per-map configurations are excluded from
that quota and from this cleanup action. Pin entries in active use or otherwise
coordinate eviction safely with readers. A disk quota must not be interpreted
as permission to allocate that amount of PCM in RAM.

Keep disk retention and in-memory working budgets separately configurable in
the implementation. The user requests tuning for Quest 2/Pro versus Quest
3/3S. Treat these as provisional performance groups, not proof of equal RAM or
available headroom within a group. Detect the actual device and available
memory information where reliable, use conservative defaults for unknown
models, and record measured per-device budgets. Larger memory does not imply
unlimited CPU/decoder throughput or that all of it is available to this mod.
Do not invent a higher Quest 3/3S RAM budget before measurement. A visible user
memory slider has not been requested by this decision.

Cleanup must occur when:

- Big Screen deletes a managed video;
- a map is assigned a different source;
- the cache identity no longer matches (invalidate derived data; validate media
  identity before deciding whether a stored audio companion is obsolete);
- the user clears the relevant cache/storage category (derived-cache clearing
  retains downloaded audio; removing downloaded audio needs explicit wording);
- a download/extraction/analysis operation fails before publishing a complete
  artifact.

Never delete a mapper-local, imported, or external user-owned source file as
part of Audio Sync cleanup.

### Removing a video: associated audio and settings

**Locked follow-up:** the existing video-removal operation also removes the
saved advanced/audio configuration associated with that video and any
accompanying audio file created or downloaded by Big Screen. This includes MP3
if used, but cleanup must use the recorded asset identity rather than assume
every audio file ends in `.mp3`; AAC/M4A, Opus, or embedded audio are also
permitted by this plan.

The implementation must:

- cancel and invalidate preparation/analysis/audition for the removed source;
- stop readers and safely release audio/decoder resources before file deletion;
- clear that video's advanced enable state, saved profile, working draft,
  markers, pitch configuration, and derived analysis records;
- remove its Big Screen-owned companion audio and temporary/derived artifacts;
- retain the existing video-removal policy for mapper/local/external media;
  detaching a user-owned video does not grant authority to delete arbitrary
  neighboring audio files;
- avoid deleting audio still legitimately referenced by another assignment if
  the implementation shares stored assets;
- make late worker completions unable to recreate the removed configuration or
  republish the deleted audio;
- report failed cleanup accurately and log the files/settings affected rather
  than claiming everything was removed when a file operation failed.

Update the existing removal confirmation using the same styling, wrapping,
frontmost presentation, and button conventions. Suggested managed-video text:

> Remove this video, its associated audio file, and its saved Advanced Sync
> settings? You will need to download it again to use Advanced Sync.

Adapt the text for absent/embedded audio and for a local video being detached.
The confirmed scope must match the actual files and settings being removed.
Removing derived caches alone is different from removing the video/audio pair.

## Analysis engine

### Recommended signal preparation

For matching, decode both sources into bounded analysis representations:

- mono;
- a fixed analysis sample rate, such as 12-16 kHz;
- normalized cautiously enough to avoid turning silence/noise into a strong
  match;
- optional high-pass/low-pass shaping to reduce irrelevant extremes;
- compact low-resolution whole-track features such as log-energy, coarse
  spectral/log-mel bands, spectral flux, or another measured fingerprint;
- higher-resolution PCM/features only for selected candidate/refinement windows;
- source timestamp metadata sufficient to translate every feature/window back to
  authoritative Beat Saber song time or video media time.

Full-track PCM must not be retained for automatic analysis. A sequential decode
of the complete usable/active audio range is allowed when needed to construct a
bounded whole-track feature index. Temporary full-resolution buffers should be
released immediately after producing compact features or completing localized
refinement.

Exact sample rate, transform, hop size, band count, filtering, and feature
representation must be measured. They are implementation choices, not locked
product behavior.

### Matching approach

A robust initial implementation should use a hierarchical matcher rather than
cross-correlating arbitrary full tracks.

Recommended pipeline:

1. **Source preparation**
   - acquire both sources while preserving authoritative timestamps;
   - convert them to the bounded common analysis representation;
   - detect usable/meaningful audio ranges without re-zeroing source time.
2. **Whole-track coarse index**
   - build compact low-resolution features over the usable portions of each
     source;
   - score inexpensive candidate map regions for energy, transient/spectral
     information, uniqueness, ambiguity, temporal separation, and track
     coverage;
   - inspect more candidate regions than the user's desired accepted-anchor
     count when useful.
3. **Coarse matching**
   - search each selected map anchor against the video feature index;
   - retain a small bounded set of plausible video locations, such as the top
     few well-separated candidates, instead of immediately keeping only one;
   - reject obviously weak or featureless candidates.
4. **Global consensus**
   - find candidate combinations that can coexist under one approximate affine
     relationship `V = rS + b`;
   - use agreement across spatially separated anchors to disambiguate repeated
     choruses, riffs, drops, or intros;
   - reject assignments that require incompatible offsets/rates.
5. **Localized refinement**
   - decode or obtain higher-resolution data only around predicted candidate
     regions;
   - refine each correspondence using normalized cross-correlation or another
     measured high-resolution matcher;
   - recover precise `(songTime, videoMediaTime)` anchors.
6. **Robust fit**
   - reject statistical outliers;
   - fit remaining anchors using a robust approach such as RANSAC or a
     Huber-weighted regression, followed by an appropriate final weighted fit;
   - convert slope/intercept directly to Big Screen's playback rate and offset.
7. **Held-out validation**
   - reserve meaningful windows that do not contribute to the final fit;
   - test whether the fitted transform predicts matching audio at those
     locations;
   - validate separated portions of the track, including meaningful end evidence
     where available.
8. **Compatibility/confidence classification**
   - inspect anchor ambiguity, cross-anchor agreement, fit residuals, held-out
     validation residuals, and residual shape;
   - report low confidence or non-affine/discontinuous timing rather than
     manufacturing a misleading line.

Repeated choruses are a known ambiguity. One short window may correlate strongly
to several choruses. The analyzer must therefore retain multiple bounded coarse
candidates and let globally consistent evidence across several anchors select
the correct timeline before high-resolution refinement.

The selected sample/anchor count represents desired useful accepted evidence,
not the number of cheap regions the engine is permitted to inspect.

### Meaningful audio start and end

Container duration is not necessarily the musical duration. Sources may include
silence, audience noise, title cards, spoken/cinematic intros, long fades, or
outros. Endpoint detection should use an energy envelope and hysteresis rather
than a single sample threshold.

The analyzer should expose the detected active ranges and allow manual
override. A fade should not be chopped simply because its instantaneous energy
drops below one threshold. Recommended logic is a sustained-window threshold
relative to each track's measured noise floor and program level.

Detected active ranges are search/sampling boundaries and visualization
metadata. They do **not** redefine source time and do not prove that the map's
active start corresponds to the video's active start, or that their active ends
correspond. A music video may have an audible cinematic intro or outro that is
not present in the map. Actual correspondence comes from matched anchors.

In particular, intentional map lead-in silence must remain represented on Beat
Saber's authoritative song clock. The analyzer may skip silent windows when
choosing useful evidence but must preserve the elapsed time before the first
meaningful music anchor.

### Confidence and safe application

An automatic result should include:

- number of candidate regions considered;
- desired, accepted, and rejected anchor counts;
- match strength for each accepted anchor;
- ambiguity information, including whether an alternate candidate was nearly as
  strong as the selected candidate;
- overall confidence/classification;
- fitted offset and rate;
- fit residuals near the beginning, middle, and end;
- held-out validation results reported separately from fit residuals;
- reason when confidence is too low or the source appears incompatible.

Confidence must not be based on raw correlation magnitude alone. For example, a
best score of `0.94` with a second candidate at `0.93` is more ambiguous than a
best score of `0.91` with the next candidate at `0.51`. Confidence should
consider at least match strength, alternate-candidate separation, accepted
anchor count/distribution, cross-anchor agreement, robust-fit residuals,
held-out validation, meaningful end evidence, and evidence of non-affine timing.

Do not expose a percentage such as `96%` unless the metric has been calibrated
to support percentage semantics. Until then, a qualitative classification such
as `High`, `Medium`, or `Low` plus diagnostic scores may be more honest.

The user approved High/Medium/Low with measured timing differences and requests
a percentage too if feasible. Treat percentage calibration as an implementation
measurement using representative known-answer fixtures. A scaled correlation
coefficient is not itself a calibrated likelihood of correct alignment. Retain
qualitative confidence if calibration cannot support that additional display.

Held-out validation is mandatory for a defensible automatic result. At least one
meaningful validation region must be excluded from the final fit; Balanced and
Thorough modes should use multiple held-out checks where practical. Validation
asks whether the fitted relationship predicts matching audio at independent
regions, not merely whether the fitted anchors have small residuals.

The analyzer must also recognize when the source cannot be represented by one
constant offset and playback rate. Examples include a live/remix arrangement,
an inserted or removed middle section, a discontinuous music-video edit, or
nonlinear timing changes. Inspect residual shape as well as average magnitude:
a step-like residual jump, curved trend, or multiple internally consistent
clusters can indicate a non-affine source.

A normal outcome such as:

```text
Non-affine or discontinuous timing detected.
This source cannot be accurately synchronized with one start offset and one
playback speed.
```

is an external/source compatibility result, not an internal error. Manual mode
may remain available.

Low-confidence or incompatible results must never overwrite saved timing
automatically. Previewing a proposed result is allowed. Applying any automatic
result requires an explicit user action.

### Python versus native implementation

Big Screen already ships CPython for yt-dlp, so a Python prototype is possible.
The recommended production implementation is native C++ DSP because it:

- avoids sharing downloader interpreter/GIL ownership;
- gives tighter memory and cancellation control on Quest 2;
- is easier to keep independent of downloader runtime updates;
- avoids Python callbacks near Unity UI and audio timing;
- can be covered by deterministic host tests.

Python may still be useful for offline algorithm experiments or for generating
golden reference fixtures. It should not become a synchronous UI dependency.

## Manual audio engine

### Clock ownership

Use one explicit audition clock. Map and video audio must be scheduled from the
same timeline rather than calling `Play()` on two unrelated sources in sequence
and hoping they remain aligned.

Both sources should preferably enter the same final mixer/output scheduling path
so a fixed difference in buffering latency does not masquerade as a timing
offset. If implementation constraints require different output paths, measure
and compensate any deterministic path-latency difference before exposing
millisecond/sub-millisecond manual alignment as meaningful. Document the chosen
clock, buffer scheduling, mixer ownership, final output path, expected fixed
latency, and any compensation.

Unity/Beat Saber audio work must stay on permitted threads. UI events should
request state changes; audio callbacks should consume prepared data without
allocating, performing file I/O, taking long locks, or touching Unity objects
from invalid threads.

The locked ear-routing modes cannot be delivered through Beat Saber's normal
`SongPreviewPlayer` output path: that player offers no per-ear routing, no
common audition-speed processing, and no sample-accurate co-scheduling with a
second source. The audition engine should therefore render **both** sources
itself from prepared PCM through one Big Screen-owned mixing/output path, with
`SongPreviewPlayer` paused or fully ducked while auditioning and restored when
auditioning stops, the workspace closes, or any cancellation trigger fires.
Auditioning must not fight the preview player's own crossfade and ownership
logic, and a guarded failure during auditioning must never leave Beat Saber's
menu music permanently silenced.

### Routing

The locked routing choices can be represented as:

| Mode | Left ear | Right ear |
|---|---|---|
| Map Left / Video Right | Map | Video |
| Video Left / Map Right | Video | Map |
| Mixed | Map + Video | Map + Video |
| Map Only | Map | Map |
| Video Only | Video | Video |

Stereo source material must be downmixed according to a documented policy
before hard left/right comparison so original stereo panning does not make one
reference unexpectedly disappear.

### Audition speed

The audition-speed control slows or restores **both** sources together. It is a
listening aid and is not saved to `playbackRate`. A proposed range is `0.25x` to
`1.0x`; the exact minimum and steps remain open pending audio quality and Quest
performance tests.

If simple resampling is used while slowing, pitch will fall equally for both
sources, which is acceptable for timing comparison. A higher-quality common
time-stretch method can be considered later if it is lightweight enough. This
control is separate from video-audio pitch correction.

The conceptual DSP order must be explicit.

For video audition audio:

```text
video source audio
    -> current proposed/saved video playback-rate transform
    -> optional video pitch compensation
    -> common audition-speed transform
    -> routing/mixer
    -> output
```

For map audition audio:

```text
map source audio
    -> common audition-speed transform
    -> routing/mixer
    -> output
```

The common audition-speed transform affects both references equally and never
changes the saved gameplay video timing.

### Pitch correction

Changing video playback speed changes its auditioned audio pitch when the
selected rate-changing method is ordinary resampling. In that case the automatic
semitone compensation for speed `r` is:

```text
autoSemitones = -12 * log2(r)
```

The `Auto` button applies that value. The manual slider adds a fine adjustment
on top of it.

Recommended UI display:

```text
Auto compensation: -1.65 semitones
Fine adjustment: +7 cents
Effective correction: -1.58 semitones
```

The approved fine slider range is -200 to +200 cents in one-cent steps around
automatic compensation; 100 cents equals one semitone. This range limits the
manual fine adjustment, not the automatic compensation implied by playback
speed. Show their combined correction clearly.

Pitch processing affects only the video's synchronization-audition audio. It
must not pitch-shift Beat Saber's map audio or change normal gameplay audio.

If the chosen video playback-rate DSP already preserves pitch, do **not** also
apply the semitone compensation formula as though the rate change shifted pitch.
The implementation must document which rate-changing path is active and avoid
double compensation.

Any real-time pitch-shift or higher-quality time-stretch implementation must
come from a lightweight native library whose license is compatible with the
project's deliberately LGPL-safe distribution posture (LGPL or more
permissive; no GPL components). Library selection, measured Quest 2 CPU cost,
and license verification are recorded as a `[Measure]` item in Remaining
decisions.

### Future difference/phase comparison

**Proposed future option, not part of the first implementation:** a
`Difference / Phase Compare` audition mode may phase-invert one source and mix
it with the other. For two encodes of substantially the same master, incomplete
cancellation can make very small timing errors easier to hear.

This is only a diagnostic aid. Different mastering, EQ, compression, stereo
content, codec artifacts, live recordings, and remixes can prevent useful
cancellation even when timing is correct. Do not include this mode in initial
acceptance criteria or implement it without separate authorization.

### Turning the video picture off

When `Show Video While Syncing` is disabled:

1. preserve the current audition/song position;
2. suspend or close the visual video decoder safely;
3. clear prepared-frame queues and GPU upload state that are no longer needed;
4. continue the audio audition and UI timeline;
5. when re-enabled, reopen or resume the decoder at the current position;
6. prewarm enough frames to avoid flashing stale or uninitialized content;
7. never feed old frames from a previous map or earlier generation.

Simply hiding the screen GameObject while continuing full decode/upload work
would not deliver the intended memory and performance benefit.

## Draft, Apply, Revert, and persistence

### Locked transaction behavior and proposed data model

Opening Advanced Sync should snapshot the map's current effective values:

- offset;
- playback rate;
- Fit to Song;
- any applicable stop marker;
- preview position and play/pause state.

Changes inside the workspace update a draft that may be auditioned live. They
do not persist on every marker movement or analysis sample. Both basic toggles
and both basic sliders remain disabled while Advanced Sync is enabled.

- `Apply` validates and atomically saves the final timing, refreshes the
  right-side state, and makes the draft the new baseline. It leaves the center
  editor open so the user can continue making changes.
- `Revert` is a proposed convenience action that restores the last applied
  baseline while staying in the workspace.
- `Close` with no unapplied changes returns normally.
- `Close` with unapplied changes asks `Save` or `Discard` in a frontmost modal.
  Save persists and applies the draft, then closes. Discard restores the last
  saved configuration and preview timing, then closes without saving the draft.
- A failed Save leaves the draft and editor available with an actionable error;
  it must not close and lose the user's work.
- Reopening loads the saved per-map advanced values and editing landmarks so
  the user can continue from where they left off.

Persist enough state to reproduce the chosen advanced method, markers, optional
cutoff, offset/rate, and relevant editing preferences. Exact UI-only preferences
that belong globally rather than per map remain a schema-design detail.

While the center panel is open, Back to Song List and equivalent actions that
would change the selected map/video are disabled. The user must finish or
resolve the advanced edit through the center Close button before leaving.
Closing cancels center-owned processing and resolves any dirty draft through
Save/Discard; it never changes maps implicitly. Restore navigation when closing
has completed. Forced scene teardown and recovery must still cancel safely.

If audio processing is active outside an open center panel, retain the earlier
warning that leaving cancels processing. Do not show a navigate-away warning
merely because the user points at the disabled Back button. If turning Advanced
Sync off while its editor is open would close it, route through the same
Save/Discard lifecycle before switching back to basic settings.

### Mapper-authored timing

The workspace must clearly distinguish mapper timing from a user override.
Applying an Advanced Sync result should create/update the user's override; it
must not edit the map's `cinema-video.json` or destroy the mapper baseline.

Reset behavior should continue following Big Screen's established rule:

- if mapper timing exists, reset returns to mapper-authored timing;
- otherwise reset returns to offset `0.00` and playback rate `1.00x`.

### Per-track master reset

**Locked:** Audio Sync includes a master reset that returns the selected track's
advanced configuration to its starting values. It must not reset another map,
global settings, or the separately retained basic-mode profile.

Recommended implementation, consistent with explicit Apply/Save:

- reset the current advanced draft and refresh every affected control without
  triggering recursive saves or stale callback writes;
- reset offset/rate to the mapper-authored baseline when present, otherwise
  `0.00` seconds and `1.00x`, following the existing reset rule above;
- reconstruct initial markers from the source timelines/durations and reset
  advanced pitch, cutoff behavior, and other per-track options to their
  documented starting defaults; do not mistake the most recently saved advanced
  values for the initial baseline;
- show the reset in the preview while retaining the prior saved profile until
  Apply or Save; Discard after a reset restores that saved profile;
- keep the audio media files available: reset clears settings, whereas video
  removal performs the associated file cleanup;
- keep the center workspace open after reset so the user can inspect and adjust
  the result.

Exact initial markers and advanced-option defaults must be recorded when those
controls are implemented. This reset is separate from the optional Revert
action, which returns to the last saved advanced profile instead of starting
values.

## Threading, cancellation, and object lifetime

### Non-negotiable rules

- Responsiveness is required across every implementation stage, not just
  automatic matching. Prefer bounded background tasks for all work that does
  not require Unity's main thread. One or two seconds is the progress-display
  threshold, NEVER an acceptable main-thread blocking budget.
- Unity and HMUI objects are created, queried, mutated, and destroyed only on
  the main thread.
- Split unavoidable main-thread setup, audio copies, and UI publication into
  small measured steps across frames. Do not synchronously wait for a worker,
  join a busy thread, or hold the UI behind a long-running task. Background
  concurrency and memory use must remain bounded so preview and gameplay
  retain CPU headroom on Quest 2.
- Network, file I/O, audio decoding, resampling, feature extraction, and
  correlation run away from the main UI thread.
- Worker exceptions are caught inside the worker boundary and converted into a
  result. They must never unwind through IL2CPP, Unity callbacks, or another
  thread.
- Every asynchronous operation carries a cancellation token and generation.
- A completion is applied only if its generation, selected map identity, video
  identity, and owning page are still current.
- Workers must not retain raw Unity pointers.
- UI destruction must not wait indefinitely for a worker.
- Locks held by UI callbacks must be short and must never cover disk access,
  network access, FFmpeg calls, or Unity calls.

### Cancellation triggers

Cancel and invalidate outstanding work when:

- the selected map changes;
- the assigned video changes or is removed;
- the workspace closes;
- the Big Screen menu closes;
- gameplay begins;
- the mod is disabled or its circuit breaker trips;
- the scene/menu hierarchy is rebuilt;
- Beat Saber loses focus in a state where the media session must stop;
- a newer analysis request supersedes the old one.

Cancelled work reports cancellation, not failure. Stale completions are logged
at a low diagnostic level and ignored; they must not show a popup or overwrite
another map's UI.

While the center panel is open, user map navigation is disabled. If audio work
is active outside that state, show the active-processing cancellation warning
before navigation commits. Forced scene teardown, master disable, and recovery
must still cancel immediately and restore any navigation controls.

### Prewarming and lazy loading

During Big Screen's existing staged prewarm:

- allocate the retained center ViewController;
- build the header, tab selector, Audio Sync scroll hierarchy, and inexpensive
  static controls;
- leave heavy textures, waveform/spectrogram meshes, audio buffers, FFmpeg contexts,
  Python state, and network work unallocated.

On first open for a selected map:

- bind the map/video identity immediately;
- automatically load/prepare the already stored audio and show the required
  cancellable progress popup without blocking the Unity thread;
- acquire only the media required for the active mode;
- populate waveform/spectrogram and analysis results incrementally.

The master enable switch and circuit breaker remain authoritative. If Big
Screen is disabled, prewarming and lazy preparation must stop and must not
recreate feature objects behind the user's back.

## Error handling and crash protection

Advanced Sync must use Big Screen's current error architecture rather than
introducing direct `try/catch + log` islands or unguarded BSML callbacks.

### Consistency with the existing mod

Before implementing each stage, inspect the current equivalent code paths in
Big Screen and follow their established error classification, recovery,
logging, and popup patterns. Reuse the existing helpers rather than creating a
feature-specific error framework or duplicate logging wrappers. New operation
names and diagnostic fields must follow the existing naming and message style.

Keep source formatting, indentation, naming, include organization, and comment
style consistent with the surrounding code and repository conventions. Explain
non-obvious lifetime, cancellation, and timing decisions in comments using the
same style as the rest of the mod. Each stage review must check this consistency
alongside functional correctness; avoid unrelated formatting churn.

### Comments for long-term maintainability

Comment every non-standard approach and every complex operation in enough
detail that a developer returning a year later can understand it without this
conversation. The more technical or complex the code, the more explanation it
requires. Keep the explanation beside the implementation; a planning document
or commit message is supporting context, not a substitute for code comments.

For these paths, explain:

- the purpose, why this approach is necessary, and why an apparently simpler
  implementation would be incorrect or unsafe;
- input/output contracts, assumptions, units, time origins, and invariants;
- the algorithm's stages, equations, thresholds, and the rationale for any
  non-obvious constants or performance tradeoffs;
- thread ownership, object lifetime, locks, generation checks, cancellation,
  cleanup order, and what happens on partial failure;
- relevant edge cases and what a future maintainer must preserve when changing
  the implementation.

Use plain-English summaries before technical details. Timing conversions,
audio alignment, confidence scoring, pitch/rate processing, bounded buffering,
and asynchronous publication particularly need these explanations. Include a
small worked example where it makes an otherwise subtle calculation clearer.
Distinguish measured limits from provisional tuning values; do not describe
an assumption as a verified guarantee.

Keep comments synchronized with code changes, avoid merely restating obvious
lines, and reference the relevant tests where useful. Each stage review must
verify that complex paths are understandable from the code and its comments
alone, not only to the original author.

### Main-thread guards

Public UI callbacks, center-page transitions, Unity-object updates, and
main-thread completion handlers must run through the established
`ErrorManager::Guard` pattern or an equivalent local wrapper that ultimately
reports through `ErrorManager`.

Expected external failures are not internal crashes. Examples include:

- video has no audio stream;
- unsupported audio codec;
- malformed local media;
- insufficient storage;
- YouTube/network failure;
- user cancellation;
- low-confidence match;
- selected map changed before completion.

These should leave the page usable and should not count toward the internal
error circuit breaker merely because the operation could not succeed.

Internal failures include invalid state transitions, impossible ownership,
unexpected exceptions, or Unity object/lifetime failures. They must be recorded
through `ErrorManager::ReportInternal`, participate in the existing recovery
policy, and leave the user with a safe route back to the menu.

### Recovery behavior

If Advanced Sync fails to initialize:

- keep or restore the right-side map editor;
- restore the neutral center controller;
- stop any Audio Sync-owned audio sources;
- cancel and detach workers;
- leave the normal video assignment and saved timing unchanged;
- show a plain-language error using the existing popup path.

If analysis fails after the page is already open, keep the page open where safe
and allow manual adjustment or retry.

No error should strand the user in an empty environment, leave an invisible
input blocker active, or make Beat Saber's menu appear frozen.

## Popup and status-message rules

The workspace itself is modeless. The accepted follow-up explicitly requires a
**progress popup with a progress bar and Cancel for automatic audio
preparation**. This supersedes the earlier inline-only progress proposal.
Video preview must remain playable while audio processing runs. Design the
progress surface and input behavior to preserve that requirement; merely
starting a synchronous operation behind a modal is not acceptable.

Every finite operation expected to take longer than one or two seconds must
provide a visible progress bar and a user-accessible Cancel action. Show these
from the start for known slow operations; if an initially short operation runs
long, expose them by that threshold rather than leaving the user guessing.
This applies to acquisition, preparation, analysis, visualization generation,
and other expensive workspace operations, not only downloads. Inline progress
is acceptable where specified below; automatic audio preparation retains its
required popup. Ordinary continuous playback is not a finite processing job.

Cancellation must be checked between bounded work chunks, with interruptible
I/O or bounded timeouts where supported. Acknowledge Cancel immediately in the
UI, stop scheduling new work, and finish safe cleanup off the UI thread. Show
"Cancelling" while cleanup is pending; do not claim completion while a worker
is still publishing results. Final publication must be atomic, so cancellation
cannot leave partially applied settings or replace existing usable media.

Show explicit stages so audio processing is not mistaken for a frozen download:

- downloading/extracting video audio;
- decoding sample windows;
- analyzing sample `n` of `N`;
- building waveform data;
- cancelling/cleaning up.

Use measured byte/sample/work progress where available and a named
indeterminate stage where the total is unknown. Do not invent a percentage or
leave a completed bar/text stale on the next map. Heavy preparation belongs to
the download workflow for new videos; loading/preparing the editing session
occurs when Audio Sync opens. The precise progress presentation for automatic
matching after preparation can remain inside the center page.

Use popups for information or decisions that genuinely require acknowledgement:

- missing/unsupported video audio;
- low storage or unrecoverable source error;
- low-confidence result explanation;
- discarding unapplied work;
- leaving the song editor while audio processing is active outside the center
  panel, warning that processing will be cancelled; with the center panel open,
  disable map navigation and use its Close/Save/Discard path instead;
- an internal error that forced workspace recovery.

### Missing-audio enable attempt

The Advanced Sync switch is the entry point for explaining missing audio. This
is an expected availability condition, not an internal error or a reason to
trip the circuit breaker. Present the popup over the right-side video editor
that owns the switch, using its existing frontmost, word-wrapped modal pattern.

Suggested downloaded-video message:

> Advanced Sync unavailable
>
> This video has no matching audio file. Advanced Sync needs the video's audio
> to compare it with the song. Remove and download the video again to include
> audio. You can continue using the basic playback controls.

Suggested local-video message:

> Advanced Sync unavailable
>
> This video has no usable audio track or matching audio file. Choose a version
> that includes audio to use Advanced Sync. You can continue using the basic
> playback controls.

Use an OK button and the same visual conventions as other Big Screen dialogs.
Do not open Configure or interrupt normal video preview for a rejected enable
attempt. Check both embedded audio and validated companion files; absence of
audio inside the MP4 alone is not sufficient evidence that audio is missing.
Keep the saved/runtime state Off and update the visible switch without a second
enable event. Repeated clicks must not stack duplicate dialogs or leave hidden
input blockers. Log the availability outcome through the existing diagnostic
path without representing an ordinary user action as a crash.

Every Advanced Sync modal must:

- be owned by the center Advanced Sync controller when invoked there;
- be presented with the existing `ShowModalInFront()` path;
- remain above the page and its input blocker;
- word-wrap within its visible panel;
- use readable font sizing and a panel size validated for the complete string;
- provide short, unambiguous buttons;
- be releasable from the frontmost-modal tracking list when hidden/destroyed;
- never be placed at the same depth as or behind another menu surface.

Errors shown from the right-side editor before the center page has opened should
continue using that panel's established modal ownership. Do not teleport every
error into the center if doing so would make it unrelated to the control the
user just pressed.

## Logging and diagnostics

All logging must use Big Screen's established Native Logger Quest-backed
`BigScreenLogger` and the existing diagnostic session facilities. Do not add a
Paper2 dependency, a parallel log file writer, `printf` debugging in release
paths, or a logger that intercepts other mods.

### Operation correlation

Each Advanced Sync open/analysis/audition operation should have a correlation
identifier. Diagnostic events should carry enough identity to reconstruct the
lifecycle without writing sensitive source data.

Recommended fields:

- operation/session identifier;
- sanitized level identifier;
- source category: managed, mapper-local, imported, external;
- audio codec/container and selected stream identifier;
- relevant source timestamp origin/time-base information;
- any stream-start, decoder-delay, priming, or pre-skip adjustment applied;
- desired accepted-anchor count and refinement duration;
- number of cheap candidate regions considered;
- accepted/rejected anchor count;
- bounded coarse candidates retained per anchor;
- per-anchor ambiguity/alternate-candidate information;
- number of held-out validation windows;
- per-stage elapsed time;
- cancellation reason;
- result confidence/classification;
- proposed/applied offset and playback rate;
- fit beginning/middle/end residuals;
- held-out validation residuals;
- detected active-audio ranges;
- non-affine/discontinuity classification/reason where applicable;
- peak temporary PCM bytes, coarse-index bytes, and cache bytes;
- whether the picture decoder was active or suspended;
- manual-audition output-path latency compensation if any;
- worker generation and stale-result rejection.

Do not log:

- raw PCM;
- complete signed YouTube media URLs or tokens;
- clipboard contents;
- large waveform/spectrogram/feature arrays;
- a user's full local path when a safe filename/category is sufficient.

### Performance logging

Separate wall-clock waiting from actual CPU preparation cost. Report at least:

- time waiting for map audio availability;
- audio sidecar network time;
- audio decode/resample CPU time;
- coarse whole-track feature-index construction CPU time;
- coarse matching/consensus CPU time;
- localized correlation/refinement CPU time;
- robust fitting and held-out validation CPU time;
- main-thread audio-copy time and largest single-frame copy cost;
- waveform/spectrogram UI construction time;
- feature-index memory and temporary PCM high-water marks;
- cancellation latency where measurable.

Do not label a duration that includes network wait or worker descheduling as
pure analysis CPU time. This follows the same principle used when correcting
misleading video decoder latency reporting.

### Support-log compatibility

Advanced Sync logs and correlated errors must land in paths already collected
by Big Screen's support-log scripts. If a new cache manifest or compact analysis
diagnostic is needed for support, explicitly add a sanitized copy to the
collector rather than asking users to locate it manually.

## Performance and memory policy

Quest 2 is the baseline device. Quest 3/3S testing can confirm additional
headroom, but the design must not require that headroom.

Prepare distinct device-budget profiles so Quest 2/Pro and Quest 3/3S tuning
can be adjusted without rewriting the pipeline. Validate actual device memory
and CPU behavior, including differences within those proposed groups. Disk
cache retention, temporary PCM, audition buffering, feature-index memory, and
video read-ahead are different budgets and must not each assume they own the
same spare memory. Until measured, use conservative limits on every model.

### Bounded-memory requirements

- Never retain full-track full-rate PCM for automatic analysis.
- A bounded sequential decode of the complete usable audio range is allowed to
  construct a compact whole-track coarse feature/fingerprint index.
- Decode/retain higher-resolution PCM only for selected candidate/refinement
  windows.
- Build display waveforms and spectrograms at screen-appropriate granularity,
  not one UI point per audio sample or an unbounded FFT history.
- Reuse PCM, FFT, correlation, and feature buffers.
- Use mono analysis unless a specific stereo feature requires otherwise.
- Keep compressed sidecars on disk and decode incrementally.
- Release full-resolution temporary buffers immediately after producing compact
  features or completing refinement.
- Clear video prepared-frame queues when the picture is deliberately disabled.
- Never allow anchor-count, candidate-count, or refinement-duration controls to
  multiply into an unbounded allocation.
- Cache compact features, source timing metadata, and derived visualization data
  rather than decoded audio whenever practical.

### Work scheduling

Audio analysis is menu-only work and should yield to important game/menu
operations. It should not run concurrently with gameplay. Avoid running heavy
analysis simultaneously with a YouTube video download, remux, or transcode
unless measurement proves that concurrency is safe.

A simple serialized heavy-media queue is preferable to multiple independent
workers competing for Quest 2 CPU, storage bandwidth, and memory. The UI can
remain responsive and report queued state.

### UI update rate

Waveform and spectrogram displays do not need to rebuild every Unity frame. Use
bounded refresh and cached/reused meshes, textures, and materials. A timeline
spectrogram is preferred to an expensive always-live frequency spectrum for
alignment because it preserves frequency information over time. If a true live
spectrum meter is retained for another diagnostic purpose, treat it as a
separate optional visualization.

Timeline text should update only when values change. Do not continuously force
layout rebuilds while the page is idle.

## Interaction with existing systems

### Video preview

Advanced Sync borrows the currently selected map's preview; it does not create
a second independent screen pipeline. Timing drafts should rebase/seek the
existing decoder rather than tearing it down for trivial offset, speed, marker,
or screen changes.

Decoder recreation remains appropriate only when media source, decoder backend,
pixel format, or another actual pipeline requirement changes.

### Right-side editor

While Advanced Sync is open:

- the right editor remains associated with the same map;
- current/proposed timing values should be understandable from the center;
- applying values refreshes the existing right-side toggles/sliders without
  triggering duplicate saves or recursive callbacks;
- both basic toggles/sliders and their reset/nudge actions are disabled while
  Advanced Sync is enabled; they cannot act as a second draft or save writer;
- user actions that change/remove/replace the selected media are unavailable
  while the center editor is open; if an external change invalidates the media,
  cancel the Audio Sync generation and recover safely;
- the user must not accidentally apply a result to a newly selected map;
- the user completes or explicitly resolves the advanced edit before leaving;
  Back to Song List is disabled until the center editor closes; unsaved drafts
  never silently carry into another map.

### Downloads and remux/transcode

The audio-sidecar operation participates in the same new-video download
workflow. Its decode/preparation implementation can remain separate from video
repair. Reuse download hardening and user-facing error classification; do not
redownload or transcode working video simply when the center editor opens.
Audio and video must share validated identity and final-media timing metadata.

If the video is being downloaded, repaired, or converted, Audio Sync should
show that it is waiting for a stable assigned video identity. It must not
analyze a staging file that may be replaced.

### Mapper settings

The feature edits user timing overrides. It does not rewrite mapper Cinema or
Chroma data and does not alter Respect Mapper Settings or Respect Chroma
Settings screen behavior.

### Future per-map settings tabs

The tab host is deliberately future-ready for:

- per-map video settings;
- per-map screen size, shape, placement, and layout;
- per-map environment visibility/options.

No data model, controls, persistence, or runtime overrides for those future
tabs are authorized by this plan. Their future addition must respect the same
scrolling, transaction, error, logging, and lifetime rules.

## Proposed implementation stages

Each stage should be independently buildable and reviewable. On-device visual
or behavior claims require Quest testing; compilation alone is not proof.

### Stage 1: Data model and pure timing logic

- define per-map enabled state, draft markers, mode, precision, routing, saved
  advanced configuration, and result types;
- implement marker-to-offset/rate calculations as pure functions;
- define user stop-marker persistence, the fitting-only/stop-at-marker switch,
  and schema migration without enabling Advanced Sync for old records by
  accident;
- add host tests for faster, slower, negative-offset, invalid-marker, and reset
  cases;
- test independent per-map basic/advanced profiles, disable/re-enable
  round-trips, reset draft versus saved state, and configuration removal;
- do not create UI or audio decoding yet.

Exit criteria: all listed host tests pass deterministically; no UI, Unity,
FFmpeg, or audio code was introduced.

### Stage 2: Center workspace shell

- create and prewarm the retained center ViewController;
- implement fixed header, one-tab segmented control, and Audio Sync scroll
  container;
- open from the selected map's video editor without stopping preview;
- implement the Advanced Sync switch, Configure button, missing-audio popup
  triggered by a rejected enable attempt, and gating of both basic
  toggles/sliders and related reset actions after successful activation;
- disable map/source navigation while the center panel is open and restore it
  after close, failed opening, or recovery;
- close back to neutral center without losing the editor selection;
- add guarded callbacks, page-owned frontmost test modal, and lifecycle logs;
- validate size, text readability, scrolling, and pointer behavior on Quest.

Exit criteria: Quest screenshot evidence of readable layout at the chosen
dimensions; repeated open/close cycles leave the preview running and the
editor state intact; lifecycle and guard logs verified on device.

### Stage 3: Audio source discovery and cache

- identify audio availability for managed and local video sources;
- acquire and prepare matching audio as part of new video downloads;
- implement automatic stored-audio loading on editor open and its cancellable
  progress popup, plus legacy missing-audio detection and enable-attempt
  explanations;
- add required bounded FFmpeg audio decode/resample components;
- preserve stream/container timestamp information needed to map decoded audio to
  video media time;
- obtain bounded map-audio windows/chunks safely from Beat Saber while preserving
  authoritative song-clock coordinates;
- verify Beat Saber's effective song/audio timing for OST, DLC, custom, WIP, and
  supported legacy maps so map timing offsets are neither ignored nor
  double-applied;
- verify per-category `AudioClip` load types and exercise the direct-access,
  file-decode, and capture-tap acquisition paths as applicable;
- implement cache identity, atomic publication, cancellation, and cleanup;
- distinguish retained audio companion media from disposable derived caches;
- retain valid basic-playback video on audio failure and present a guarded,
  source-specific audio-error popup using the established dialog path;
- extend video removal and confirmation text to cover its saved advanced/audio
  configuration and recorded companion audio files;
- keep preview available during processing; block map navigation while the
  center editor is open and confirm cancellation if leaving during audio work
  outside that state;
- test OST, DLC, custom, WIP, local MP4, local WebM, missing audio, corrupt
  audio, non-zero stream timestamp origins, and decoder-delay/priming fixtures.

Exit criteria: the acquisition fixture matrix passes on device; cache entries
publish atomically; cancellation and failure leave no partial artifacts or
false cache records; timestamp-origin fixtures map decoded positions to the
correct timelines.

### Stage 4: Waveforms, spectrogram data, and shared transport

- create compact waveform, timeline spectrogram, and coarse feature
  representations off the main thread;
- render them using reused Unity objects at a bounded refresh rate;
- implement shared play/pause, seek, and loop-range transport;
- keep preview and timelines synchronized to one audition clock;
- establish/document the final audition mixer/output timing path;
- preserve authoritative source timeline coordinates in all displayed and cached
  derived data;
- verify leaving/switching maps cannot carry stale audio or UI state.

Exit criteria: waveform/spectrogram rendering stays within the menu frame
budget at the bounded refresh rate; transport follows one audition clock; the
measured output-path latency (and any compensation) is documented; map
switching carries no stale audio or UI state.

### Stage 5: Manual routing and marker workflow

- add ear routing and source-isolation modes;
- add draggable start/end markers and precision nudges;
- implement manual-speed and fit-between-markers modes;
- define and test the playback-rate -> optional pitch correction -> common
  audition-speed -> routing/mixer DSP order;
- verify both audition sources share one final output timing path or use measured
  deterministic latency compensation;
- verify intentional map lead-in silence remains visible and numerically tied to
  Beat Saber's song clock;
- add draft preview, Apply, Revert, and close confirmation;
- add per-track master reset and verify Apply/Save/Discard after resetting;
- synchronize saved values back to the right editor.

Exit criteria: a real map can be manually aligned end to end; Apply, Revert,
and close confirmation round-trip correctly against the right editor and
library persistence; the documented DSP order is verified by test.

### Stage 6: Automatic matching

- build a bounded low-resolution whole-track feature index without retaining
  full-track PCM;
- implement high-information candidate-region scoring/selection;
- interpret the preset count as desired useful accepted anchors, not a hard cap
  on cheap candidate inspection;
- retain several bounded coarse candidates per anchor where ambiguity warrants;
- establish global affine consensus before expensive refinement;
- perform localized normalized cross-correlation or equivalent high-resolution
  refinement around predicted candidate positions;
- add Fast/Balanced/Thorough/Custom controls using the revised anchor semantics;
- add endpoint detection without source-time re-zeroing;
- add outlier rejection and robust weighted affine fit;
- add true held-out validation with data excluded from the fit;
- add ambiguity-aware confidence reporting;
- detect and report non-affine/discontinuous sources rather than forcing a
  misleading result;
- add progress/cancellation and deterministic fixtures;
- do not auto-save a result.

Exit criteria: synthetic fixtures with known offset/rate are recovered within
a stated tolerance; ambiguity, held-out, and non-affine fixtures are
classified correctly; cancellation is prompt; no result is auto-saved.

### Stage 7: Audition speed, pitch correction, and picture suspension

- slow both references together without altering saved timing;
- add video-audio pitch correction, Auto compensation, and fine tuning;
- prevent double pitch compensation when the selected rate-changing DSP already
  preserves pitch;
- implement actual decoder/upload suspension when the picture is disabled;
- safely restore the visual decoder at the current position;
- measure CPU, memory, path latency, and audible glitches on Quest 2.

Exit criteria: measured Quest 2 CPU/memory reduction with the picture
disabled; no double pitch compensation on the active rate path; decoder
resume is artifact-free at the current position.

### Stage 8: Hardening and integration audit

- audit every callback and worker boundary for guards and stale generations;
- verify all popups are frontmost, readable, and dismissible;
- test circuit-breaker and master-disable behavior;
- test low storage, network loss, cancellation, menu close, map change, source
  replacement, scene rebuild, and repeated open/close;
- verify support-log collection includes useful Advanced Sync evidence;
- perform a complete regression of existing preview, download, remux,
  transcode, gameplay, restart, and menu-navigation behavior.

Exit criteria: the full regression, navigation, performance, and UI matrices
pass and their results are recorded in the Progress log.

## Required test matrix

### Timing and audio content

- identical audio with only positive start offset;
- identical audio with negative start offset/lead-in;
- map audio with intentional leading silence/dead time, verifying useful-audio
  detection does not re-zero Beat Saber song time;
- matching video with a different-length audible/silent intro before the same
  music begins;
- legacy map with a non-zero map/song timing offset, verifying it is honored
  exactly once and not double-applied;
- fixture with a known non-zero audio stream timestamp origin;
- AAC/Opus or equivalent fixture exposing decoder delay, priming, or pre-skip,
  verifying decoded sample positions map correctly to video media time;
- video audio running slightly faster than map audio;
- video audio running slightly slower than map audio;
- long silence at the beginning or end;
- fade-in and fade-out;
- repeated chorus that can create an ambiguous single-window match;
- repeated intro/riff/drop with two nearly equal coarse candidates;
- several strong candidates where only one cross-anchor combination forms a
  globally consistent affine timeline;
- held-out-validation fixture where fit anchors appear valid but an independent
  region intentionally differs;
- inserted middle section causing a step-like residual discontinuity;
- removed middle section;
- nonlinear timing drift that cannot be represented by one rate;
- live/remix version with arrangement differences;
- audio that is genuinely unrelated;
- very short track;
- no audio stream;
- malformed or unsupported audio stream.

### Map and media categories

- OST;
- DLC;
- custom;
- WIP;
- map whose Unity `AudioClip` uses a streaming load type where direct sample
  access is unavailable;
- newly downloaded YouTube video with validated accompanying audio;
- legacy downloaded video without audio: clickable Advanced Sync switch,
  useful redownload popup on enable attempt, and switch restored to Off;
- local video without audio: source-appropriate popup on enable attempt;
- rejected enable leaves basic controls, saved configuration, and preview intact;
- repeated rejected enable attempts produce no recursive callbacks or stacked
  duplicate dialogs;
- successful video transfer with failed or cancelled audio preparation;
- separate audio companion retained after clearing derived analysis caches;
- mapper-local MP4 with AAC;
- external/imported MP4;
- WebM with Opus or Vorbis where supported;
- repaired/remuxed HLS video;
- last-resort transcoded video.

### Navigation and lifetime

- open/close Advanced Sync repeatedly;
- close with and without draft changes;
- Save versus Discard after live audition edits, including failed saves;
- per-map Advanced Sync enabled state and saved marker/cutoff state on reopen;
- disable/re-enable restores basic/advanced profiles independently;
- master reset of one track does not affect another track or saved basic values;
- Discard after master reset restores the last saved advanced profile;
- video removal clears saved advanced/audio settings and owned companion audio;
- video removal during audio work cannot be undone by a late completion;
- attempts to change basic toggles/sliders/reset controls while advanced is on;
- disabled Back to Song List and equivalent selection actions while the center
  editor is open; restored navigation on close/recovery;
- leave-for-song-list warning during extraction/preparation outside that state;
- switch maps during audio preparation;
- remove or replace video during analysis;
- leave Big Screen during analysis;
- enter gameplay during/after analysis;
- disable Big Screen through the master switch;
- trigger an internal guarded error and confirm safe recovery;
- reopen after a MenuCore/scene hierarchy rebuild;
- stop, cancel, or fail during active auditioning and confirm normal menu
  music ownership is restored;
- verify no invisible modal or raycaster remains.

### Performance

- Fast, Balanced, and Thorough analysis on Quest 2 using revised accepted-anchor
  semantics;
- coarse whole-track feature-index construction time and memory;
- picture enabled versus disabled;
- maximum allowed custom anchors/duration/candidates;
- repeated sessions without increasing retained memory;
- main-thread frame-time spikes during AudioClip window/chunk copies;
- waveform/spectrogram refresh overhead;
- analysis while a normal preview is running;
- localized-correlation/refinement cost;
- held-out-validation cost;
- cancellation latency;
- manual-audition output-path latency/alignment;
- storage/cache growth and cleanup.

Also verify 128 MB disk-cache eviction excludes downloaded audio/settings,
active readers survive eviction, and per-device working-memory limits are
measured independently from disk quotas.

### UI

- text readable from several meters;
- complete strings wrap within their panels;
- one-tab strip displays normally;
- scroll reaches every control without clipping;
- marker handles are usable with either controller;
- high-precision values are readable and do not overflow;
- waveform and optional timeline spectrogram remain readable and bounded;
- center page does not prevent viewing the world video screen;
- every popup appears above all relevant menu surfaces and can be dismissed.

## Remaining decisions before or during implementation

These details were not fully locked and must be resolved with focused tests
or a short design discussion rather than guessed silently. Each is classified
`[Ask]` (product decision: stop and ask the user) or `[Measure]` (the agent
may resolve it with recorded code inspection, fixtures, or Quest measurement):

1. `[Resolved]` Use `Advanced Sync` as the per-map switch in the song/video
   preview panel, `Configure` as its enabled action, and `Audio Sync` as the
   initial tab. Exact row placement is a layout measurement to confirm on Quest.
2. `[Measure]` Exact center-panel dimensions, font sizes, tab height, and
   translucency, validated with Quest screenshots and confirmed with the user
   before being treated as final.
3. `[Measure]` Whether transport remains inside the scroll content or becomes
   a fixed footer after layout testing.
4. `[Measure]` Exact automatic preset values and hard bounds after Quest 2
   measurement.
5. `[Measure]` Audio analysis sample rate, feature transform, hop size,
   coarse-index format, correlation implementation, and numerical thresholds.
6. `[Measure]` Maximum bounded coarse candidates retained per anchor.
7. `[Resolved]` Show High/Medium/Low and useful timing residuals. Include a match
   confidence percentage if calibration supports it. `[Measure]` Validate that
   percentage against known-answer fixtures before exposing it as probability.
8. `[Measure]` The preferred YouTube audio sidecar codec/container and
   fallbacks, verified against real fixtures.
9. `[Resolved]` Pitch fine adjustment is -200 to +200 cents around automatic
   correction, in one-cent steps.
10. `[Resolved]` Provide both fitting-only and stop-at-marker behavior on a
    per-map switch. Default to fitting-only. Enabling the switch replaces the
    mapper cutoff while Advanced Sync is active; disabling it restores normal
    mapper behavior. The user will judge this initial behavior on Quest.
11. `[Resolved]` Separate speed precision: 0.1x, 0.01x, 0.001x, 0.0001x.
12. `[Measure]` Exact audition-speed range and whether higher-quality
    time-stretch is worth its Quest cost.
13. `[Superseded]` Advanced Sync explicitly owns timing while enabled and
    disables both basic toggles/sliders. A separate Fit-to-Song warning policy
    is no longer the central decision; see item 23 for switch-off behavior.
14. `[Resolved initial disk budget]` 128 MB for disposable analysis/visualization
    cache with LRU eviction and a Manage Storage cleanup action. Audio media and
    saved settings are excluded. `[Measure]` Tune separate working-memory
    profiles for Quest 2/Pro and Quest 3/3S using actual device evidence; do not
    conflate the disk quota with RAM usage or assume equal RAM within a group.
15. `[Measure]` Exact runtime API/object used as the authoritative resolved
    song/audio timing source for each supported map category after code
    inspection.
16. `[Deferred]` Difference / Phase Compare stays outside this implementation.
17. `[Resolved]` New video downloads also acquire/process matching audio.
    Opening the center editor automatically loads/prepares that stored audio
    with a cancellable progress popup. Old downloads without audio require
    removal and redownload; attempting to enable Advanced Sync explains this
    in a popup and returns the switch to Off. The switch remains clickable.
18. `[Measure]` The concrete audition output mechanism (streaming clip fed
    from native buffers, audio-filter mixer, or another path), its measured
    output latency, and the exact `SongPreviewPlayer` pause/duck/restore
    coordination.
19. `[Resolved]` Apply saves without closing. Closing with changes asks Save or
    Discard; Save applies then closes, Discard restores saved values then closes.
    While the center panel is open, disable Back to Song List and equivalent
    map/video-selection actions. Restore navigation after closing or recovery.
20. `[Resolved for preview]` Video preview remains available while audio is
    processed. Center-open navigation is disabled; leaving during audio work
    outside that state warns that it cancels processing. `[Measure]` Schedule background work so preview has
    priority; establish when the two-track audition has enough prepared audio
    to become available without underruns.
21. `[Measure]` Pitch-shift/time-stretch library selection under the
    LGPL-compatible license constraint, including measured Quest 2 CPU cost.
22. `[Measure]` Per-map-category Unity `AudioClip` load types and which
    acquisition path (direct access, file decode, capture tap) each category
    actually requires.
23. `[Resolved]` Advanced Sync starts off for newly assigned videos. Turning it
    off restores the map's basic settings while retaining its saved advanced
    profile. Add a per-track master reset. Video removal also clears the
    associated advanced/audio settings and Big Screen-owned audio companion;
    update the deletion confirmation accordingly.
24. `[Resolved]` Retain valid video for basic playback when audio acquisition
    fails. Show the audio failure in a popup matching existing Big Screen
    dialogs. Advanced Sync remains unavailable until matching audio is valid.
25. `[Resolved behavior]` Basic adjustments remain available as today for both
    downloaded and local videos with no audio. Use source-appropriate missing
    audio hints; a local file must not imply that Big Screen can download it.
26. `[Measure]` In a muted capture fallback, establish how later manual seeking
    and audition retrieve source audio. A compact feature index is sufficient
    for coarse analysis but cannot reproduce the source audio. Any temporary
    disk-backed audio or recapture design needs bounded resources and measured
    latency; do not equate index availability with audition readiness.
27. `[Measure]` Preserve/remap audio against the final video timeline after
    remuxing or transcoding. The current remuxer subtracts input video stream
    start time. Verify this with final-video/companion-audio fixtures.

## Acceptance criteria

The feature is not complete until all of the following are true:

- Advanced Sync opens in the full center panel while the selected map and
  world-space video preview remain usable.
- Its one visible Audio Sync tab is backed by a real tab host and scroll page.
- The Advanced Sync enable state and saved configuration belong to each map;
  enabling it disables both basic timing toggles/sliders and their reset/nudge
  actions while preview transport remains available.
- Both end-marker modes can be selected and tested, and only the enabled
  stop-at-marker mode introduces a user cutoff.
- New video downloads acquire/process matching audio; legacy entries without
  it retain a clickable Advanced Sync switch that shows an explanatory popup
  on an enable attempt, returns to Off, and keeps basic controls available.
- Opening Configure loads/prepares stored audio automatically with visible,
  cancellable popup progress. Preview can continue while audio is processed.
- While the center panel is open, Back to Song List and equivalent map/video
  selection actions are disabled. Closing resolves Save/Discard and restores
  navigation; recovery cannot leave the Back action stuck disabled.
- Apply persists per-map changes; closing a dirty editor offers Save/Discard;
  reopening restores the saved advanced editing state.
- Switching Advanced Sync off restores basic playback settings and retains the
  advanced profile; switching back restores the advanced configuration.
- Per-track master reset restores starting advanced values with correct UI
  state and Apply/Save/Discard behavior.
- Removing a video clears its saved advanced/audio configuration and associated
  owned audio files, and the confirmation describes that scope accurately.
- An audio failure leaves valid video/basic controls working, explains the
  actual cause through a standard popup, and never marks Advanced Sync ready.
- Opening/closing it does not introduce a first-use menu stall or leave Big
  Screen unable to reopen.
- Automatic mode can discover an initially unknown video offset using a bounded
  whole-track coarse feature index without retaining full-track PCM.
- Automatic mode can produce and preview a defensible offset/rate result from
  multiple useful audio locations, including meaningful evidence near the end.
- The requested automatic anchor count represents useful accepted evidence;
  inexpensive candidate inspection can exceed that count without creating
  unbounded work or memory.
- Repeated musical sections are resolved through bounded multi-candidate,
  cross-anchor/global timing consistency rather than blindly selecting one
  locally strongest peak.
- At least one meaningful held-out validation region is excluded from the final
  fit, and validation results are reported separately from fit residuals.
- Automatic confidence incorporates ambiguity and cross-anchor/held-out
  agreement rather than raw correlation magnitude alone.
- Automatic mode can reject or clearly classify a source whose timing cannot be
  represented by one constant offset and playback rate.
- Intentional leading silence in the map does not re-zero Beat Saber's song
  timeline or produce an incorrect video offset.
- Relevant legacy map/song timing offsets are represented exactly once through
  the authoritative effective song/audio relationship.
- Audio stream/container timing metadata is preserved well enough that decoded
  analysis positions map back to the actual video media timeline.
- Map-audio acquisition is verified for every supported category, including
  clips whose Unity load type does not permit direct sample access.
- Manual mode can align start/end markers, directly set or calculate speed, and
  audition the sources with all agreed routing modes.
- Manual map/video audition does not introduce a false timing error through two
  uncompensated output paths with materially different fixed latency.
- Stopping, cancelling, or failing an audition restores Beat Saber's normal
  menu audio ownership.
- Precision, pitch correction, common audition speed, and picture suspension
  behave as described without corrupting saved timing.
- Video playback-rate processing, optional pitch compensation, and common
  audition-speed processing occur in a documented order and do not double-apply
  pitch correction.
- Waveform and optional timeline spectrogram data remain bounded and do not
  rebuild unnecessarily every frame.
- Applying timing updates the existing Big Screen data model and right editor;
  discarding restores the prior values.
- Every worker is bounded, cancellable, generation-safe, and isolated from
  Unity objects.
- Under slow-network and slow-processing tests, any finite operation exceeding
  one or two seconds exposes progress and Cancel; cancellation and cleanup do
  not stall the UI. Main-thread-only steps are profiled separately and remain
  small even with large libraries or long audio tracks.
- External failures, low-confidence matches, and incompatible/non-affine sources
  are explained without tripping the internal circuit breaker, and internal
  failures recover without stranding the user.
- Popups use Big Screen's frontmost, word-wrapped, dismissible modal path.
- Logs use the existing first-party logger and diagnostic correlation without
  leaking raw audio, large feature arrays, or signed URLs.
- Quest 2 testing demonstrates acceptable memory use, no persistent resource
  growth, and no visible/audible menu stalls.
- Existing video preview, map playback, YouTube download, repair/remux,
  transcode, and screen behavior still pass regression testing.

## Related implementation references

- `src/MenuFlowCoordinator.cpp` and `include/BigScreen/MenuFlowCoordinator.hpp`
  for retained center-controller ownership, staged menu prewarming, recovery,
  and frontmost modal tracking.
- `src/VideoLibraryMenu.cpp` for selected-map editor state, Beat Saber preview
  ownership, current timing controls, and status-modal conventions.
- `src/MapVideoConfig.cpp` and `include/BigScreen/MapVideoConfig.hpp` for the
  authoritative song-time to media-time relationship.
- `src/VideoLibrary.cpp` and `include/BigScreen/VideoLibrary.hpp` for user
  overrides, mapper timing preservation, storage ownership, and atomic
  persistence.
- `src/DownloadManager.cpp` for the current video-only yt-dlp format policy,
  cancellation, source identity, progress, and error classification.
- `scripts/build-ffmpeg-lgpl.sh` for the deliberately minimal current FFmpeg
  feature set that will need bounded audio additions.
- `src/ErrorManager.cpp`, `include/BigScreen/ErrorManager.hpp`, and
  `src/DiagnosticSessionLogger.cpp` for crash protection, user-visible errors,
  circuit-breaker behavior, and correlated diagnostics.
- Saber Stage's `src/ui/MenuFlowCoordinator.cpp` for normal center-panel
  ownership and `src/ui/MenuController.cpp` for a proven native segmented-tab
  plus independent scroll-page pattern. Reuse the pattern, not its side-panel
  dimensions.

## Change-control note

This document is the continuity record for the feature. Implementation work
should update its status and decisions as stages are completed and Quest tests
report real behavior. Do not rewrite a **Locked** requirement into a different
behavior merely because another implementation is easier. If testing shows a
locked decision is unsafe or impractical, record the evidence here and discuss
the proposed change before altering the product behavior.

The September 6, 2026 synchronization-design review was integrated directly
into this plan. That review clarified the bounded whole-track coarse-index
model, authoritative source timestamp handling, map lead-in/legacy timing,
multi-candidate consensus, held-out validation, ambiguity-aware confidence,
non-affine source detection, manual output-path latency, DSP ordering, and
waveform/spectrogram terminology. These corrections are part of this single
continuity record rather than a separate implementation prompt.

A further September 6, 2026 revision added the agent operating rules and
stage-status table, per-stage exit criteria, the `[Ask]`/`[Measure]` decision
classification, the Unity `AudioClip` sample-access constraint and acquisition
fallback order, audition-engine output ownership and `SongPreviewPlayer`
restoration requirements, sidecar preparation-consent wording, the pitch and
time-stretch license constraint, draft behavior on map change, and the
Progress log appendix.

## Appendix: Progress log

Planning update (September 6, 2026): added the user's long-term maintainability
requirement for detailed inline documentation of complex and non-standard
code. Comment depth must increase with technical complexity and explain the
rationale, contracts, safety constraints, and maintenance risks. Documentation
only; no implementation or device testing in this update.

Planning update (September 6, 2026): the user explicitly requires the existing
mod's error handling, logging, coding style, and code formatting throughout this
feature. The consistency rule above is a review requirement for every stage.
Also recorded background-first execution and progress/Cancel for finite work
lasting more than one or two seconds. These are documentation changes only;
implementation and device validation have not started.

Implementation agents append an entry at the end of every work session. Never
delete or compress earlier entries; this log is part of the continuity record.

| Date | Stage | Work completed | Quest evidence | Decisions / questions |
|---|---|---|---|---|
| 2026-09-06 | Planning; all stages not started | Imported supplied V3 and reviewed it. Incorporated the subsequent user decisions on per-map Advanced Sync/Configure, disabled basic controls, explicit Save/Discard, switchable end-marker behavior, audio acquired with new video downloads, automatic session loading, cancellable popup progress, and navigation warnings. | No build, deployment, or Quest test in this planning session. | Open: basic-mode restoration/default, partial audio failure, local-file help text, and combined navigation/draft handling. Technical checks: capture fallback audition access and final-video timestamp rebasing. |
| 2026-09-06 | Planning follow-up; all stages not started | Recorded the user's approval of basic-setting restoration with retained advanced settings, per-track master reset, associated audio/settings cleanup on video removal, updated delete wording, and standard audio-error popup while retaining basic playback. Updated the affected requirements, stages, and acceptance tests. | Documentation only; no implementation or headset access. | Decisions 23-25 resolved. Master-reset draft/Apply semantics are documented as the recommended application of the agreed save model. |
| 2026-09-06 | Planning follow-up; all stages not started | Replaced the disabled missing-audio switch/hover-hint design with the user's clickable toggle and detailed popup. Rejected activation returns the switch to Off without recursive callbacks, keeps basic controls/preview usable, and does not discard saved advanced settings. Updated stages and acceptance tests. | Documentation only; no implementation or headset access. | Missing audio blocks activation, not the explanatory switch. Downloaded and local sources receive appropriate guidance through the existing right-panel modal path. |
| 2026-09-06 | Planning defaults; all stages not started | Recorded approved confidence display with optional calibrated percentage, +/-200-cent pitch fine adjustment, separate speed precision, initial end-marker precedence, 128 MB disposable disk cache, device-specific working-memory tuning, and disabled map navigation while the center panel is open. | Documentation only; no implementation or Quest measurements. | Product choices resolved for starting staged work. Device limits and confidence calibration remain measurements. End-marker behavior remains subject to the user's Quest comparison. |
| 2026-09-06 | Implementation foundation: 1, parts of 3/4/6 | Added pure per-map profile/draft/schema/timing math, source fingerprinting, optional library overlay, generation-scoped cancellable worker operations, bounded file audio decode/resampling, 16-band streaming features, multi-candidate matching, robust fitting and held-out validation. Added five host test groups including generated AAC/Opus/Vorbis/PCM fixtures. Automatic-to-Manual retains the same draft; Manual-to-Automatic does not supply a manual seed to Analyze. | No ADB use, deployment, or Quest runtime test. The integrated feature is NOT ready for user testing: center UI, audio acquisition with downloads, map clip acquisition, cache, transport, routing, pitch and picture suspension remain outstanding. | FFmpeg 4.4 recipe revision 3 adds private audio components and libswresample; FFmpeg 9 video path is unchanged. Linux/WSL build prerequisites now explicitly include libswresample-dev. |

### Foundation evidence and next integration work

- `AudioSyncModelTests`: precision, marker fit, invalid inputs, per-map draft
  save/discard/reset, strict JSON, end-marker precedence, and Automatic-to-Manual
  carryover. The mode selector itself neither saves nor discards timing.
- `AudioSyncReaderTests`: bounded windows, seeking, cancellation, unavailable
  files, AAC/Opus/Vorbis/PCM, non-zero PTS and explicit final-video rebasing.
  The fixtures exposed an Opus packet-time-base omission; setting the codec's
  packet time base before opening it keeps decoder priming/pre-skip handling
  consistent with source timestamps. No extra codec delay is subtracted.
- `AudioSyncAnalysisTests`: leading silence, feature limits, invalid samples,
  robust fit/outliers, held-out rejection, rate-aware local refinement and
  asynchronous cancellation/result ownership.
- `AudioSyncServiceTests`: a 40-second generated musical fixture with a known
  0.300-second shift and rate `16000/16327` was recovered as approximately
  0.300024 seconds and 0.979972, with a maximum held-out residual of 0.394 ms.
  This is ONE synthetic result, not a claim of real-song accuracy or a
  confidence calibration. It took about 18.5 seconds on the development host
  in the current test build; Quest performance has not been measured.
- Refinement uses full-window energy envelopes to identify a phrase before
  jointly refining waveform lag and rate. A single short PCM snippet was
  insufficient: sustained notes can select the wrong waveform cycle even
  though their short-window correlation is high. Correlation alone is not
  treated as a probability of correctness.
- Current coarse rate search is provisionally 0.9x–1.1x. Manual timing math
  retains the existing 0.05x–8x safety range. Broader automatic-rate coverage,
  repeated sections, unrelated masters, cuts and codec combinations require
  more fixtures before the automatic matcher is considered finished.
- The library keeps basic records unchanged and applies a source-keyed
  advanced overlay only when enabled and matching the assigned video. Source
  replacement clears the obsolete profile. Overlapping background publishers
  use a scope-balanced count rather than a shared boolean, preventing one
  completion from advertising idle while another save holds the storage lock.
- The first Android build caught the file clock's implementation-defined
  128-bit tick representation. Fingerprints now serialize explicit
  microseconds instead. Build results must still be distinguished from
  headset operation and visual validation.
- Build validation after these fixes: the ARM64 alpha14 native build and
  private FFmpeg ELF checks pass. The full host run passed 19 of 20 tests;
  the repository invariant test initially rejected the stale generated
  manifest/old schema expectations. After regenerating the manifest and
  updating the explicit schema/recipe assertions, that invariant test passed
  separately, as did the canonical build pipeline and Linux/Distrobox tests.
  No test failure was suppressed. No QMOD was deployed or tested on Quest.

Next: finish the current build/invariant checks, then implement the retained
workspace and its navigation/draft transaction owner, compressed companion
acquisition/provenance and cleanup, bounded map-audio access/cache, shared
audition transport, manual controls and visualization, pitch/suspension, then
the integrated hardening pass. Do not ask the user to test these foundations
as if the complete feature were present.

### Integrated implementation after the interrupted work session

September 6, 2026: resumed after the user's computer restart. No evidence was
available to attribute that restart to this task. Native/host compilations are
serialized with one compiler job to limit host pressure. No headset/ADB access,
deployment, commit, branch change or publication was performed during this pass.
The earlier foundation-only "Next" entry remains historical, not current scope.

Stages 1–8 now have integrated code. This is a first live-test candidate, not a
claim that the following UI/audio behavior has already been demonstrated on Quest.

#### Source, timing and resource decisions

- Managed downloads request a direct AAC audio-only stream, or Opus fallback,
  tied to the same canonical YouTube ID. The compressed sidecar is recorded in
  `library.json` and included in the video publication/rollback transaction.
  HLS-only audio is not guessed onto a different segment origin. Audio failure
  is a user-visible warning while a valid video remains usable in basic mode.
  Companion audio has a 128 MB per-track cap and free-space reserve; it is not
  counted as disposable cache data.
- `AudioSyncReader` decodes/resamples using Big Screen's private FFmpeg 4.4
  runtime. The added resampler is `libswresample-bigscreen44.so`, not a shared
  system/mod package. New QMODs and source deployment include it. Existing
  FFmpeg 9 video decoding remains separate and unchanged.
- Map file decode uses SongCore's installed directory and Info.dat's audio
  filename on a worker. Built-in maps use the full game audio clip already
  requested by Big Screen, not the short song-selection preview. A resident
  decompressed clip is copied in bounded main-thread slices; other load types
  use a private zero-output tap and a sequential capture. A failed resident
  GetData call reports an error instead of pretending to have captured silence.
  DSP, disk writes and
  resampling stay on the worker. Capture overflow/timeouts fail explicitly.
  Streaming capture can take the whole song duration and remains cancellable.
- Cached mono float PCM is 16 kHz on disk, with original leading silence and
  source timeline preserved. Analysis uses bounded windows, not full-track PCM
  in memory. Shared leases pin files while analysis/audition readers need them.
  LRU eviction and Storage Maintenance skip active entries. Storage scans do
  not block behind a long capture; interrupted generated `.wav.part` files are
  recovered while owning the cache writer lock. No user media is cache-evicted.
- Feature indices use 512-sample transforms, 16 frequency bands, and a 32 ms
  hop. Initial limits are 24,000 frames per source for Quest 2/Pro/unknown and
  32,000 for Quest 3/3S. These are provisional bounds, not measured device RAM
  budgets. The two families must be tuned from actual headset logs.
- Separate YouTube audio is mapped onto the **final** video's presentation
  start, after any repair/remux/transcode. Embedded audio already shares its
  container clock. Codec priming/pre-skip is handled once by the decoder.
  Tests cover non-zero video PTS, rebasing to zero, and corresponding AAC
  origins; real downloaded HLS/direct/transcoded pairs still need comparison.
- Actual audio end and full video end are distinct. A silent video tail remains
  selectable and is painted as silence, rather than stretching the audio
  overview across it or invalidating previously saved video end markers.

#### Audition and editor ownership

- A single native producer fills one bounded stereo ring; both sources leave
  through the same streaming Unity AudioClip/AudioSource and game mixer bus.
  The callback only copies prepared samples and fills silence on underrun.
  Both source clocks stop advancing together on underrun. A generation check
  prevents a retired Unity clip callback consuming the next session's ring.
- Sonic revision `b93885dcb70aae50c6f76b0fe4e0868f029a077e` supplies optional
  pitch processing as private hidden static code under Apache-2.0. Its source
  archive is SHA-256 pinned and cached once for both native and host builds.
  No system Sonic installation or additional executable is required. License
  and attribution files are staged with the existing third-party notices.
- Video is first sampled at `r*S+b`; optional pitch correction applies the
  saved automatic semitone compensation plus fine cents. Common audition
  slowdown affects both sources and deliberately lowers both pitches. Input
  precision is not a promise of DAC/video presentation precision. Sonic's
  audible transient behavior and actual Unity output latency require Quest
  measurement; host duration tests do not prove perceptual alignment.
- Picture Off stops the video decoder/queue/upload path. Scrubbing while
  picture is hidden must not recreate that pipeline. Picture On restores the
  current draft's timing. Normal map audio/gameplay policy is unchanged.
- The retained center page is constructed in small prewarm stages. Only the
  Audio Sync tab is visible; its body scrolls from the outset. Waveform and
  coarse frequency textures are generated on workers and uploaded as two small
  fixed-size images, with main-thread marker movement rather than per-frame FFT.
- Draft edits do not persist until Apply/Save. Automatic proposals and manual
  controls edit the same draft. Analysis receives audio and effort settings,
  not a manual offset/rate seed. Source replacement clears obsolete profiles;
  disabling advanced mode restores the separate basic settings.
- Worker results have a map/generation owner and are consumed once. Cancellation
  waits asynchronously for native cleanup, never by joining a worker on Unity's
  UI thread. Failure/cancellation restores controls as well as success. Save
  failure retains the editor and draft. Forced teardown abandons the draft and
  releases navigation before touching scene objects, so cleanup failure cannot
  leave map selection permanently locked.
- Existing error-history/session/native logging and frontmost modal helpers are
  reused. Expected bad media/network errors do not count as internal circuit-
  breaker failures. Complex callback lifetime, quota, clock and transaction
  contracts have inline comments. Audition-stop logs include consumed frames
  and underruns; analysis logs include evidence counts and held-out residuals.

#### Focused live-test order

1. **Download and basic fallback:** download a short familiar video. Watch video
   and matching-audio stages, then play normally. Try Advanced Sync on an old
   silent download/local file: its explanatory popup must be readable and the
   switch must return Off without disabling basic playback.
2. **Open and prepare:** enable/configure a newly downloaded video on a custom
   map. Confirm the full center tab, scrolling, readable controls, progress and
   Cancel. Normal right-side preview should remain usable during preparation.
   Map/source navigation must stay disabled until the center closes.
3. **Manual audition:** test each ear/mix route, timeline seeking, both precision
   selectors, manual speed, start/end markers, Fit Markers and both end policies.
   Change timing while playing; ensure both sources restart together at the
   current common position and no old buffered audio leaks into the new result.
4. **Optional processing:** test common slowdown, pitch Off/On/Auto/fine cents,
   and Picture Off/On. Listen for clicks, long stalls or differing ear latency.
   Picture Off should actually remove video preparation load, not merely hide it.
5. **Automatic:** analyze the same song/video recording, preview the proposal,
   switch to Manual and confirm its timing/markers remain. Apply, reopen, and
   verify persistence. Try a different edit/repeated music: low confidence is
   acceptable; an unsupported confident answer is not.
6. **Persistence boundaries:** Apply without closing, Close+Save, Close+Discard,
   per-track Reset+Discard, then switch maps. No draft/status/transport should
   transfer to another map. Turn Advanced Sync Off/On and verify basic and
   advanced settings each retain their own values.
7. **Built-in song:** repeat opening on OST/DLC. If a sequential silent capture
   is necessary, verify progress and cancellation, and that reopening reuses
   its cache. This specifically tests the game-clip acquisition path rather
   than assuming custom-map file tests cover it.
8. **Cleanup and recovery:** close during preparation/analysis, reopen, leave
   the menu, and remove a video's assignment/file. Verify associated managed
   audio/settings cleanup and Storage Maintenance's unused-cache category.
   Finish with ordinary basic preview and one gameplay map to check isolation.

Outstanding measurements are live layout, actual output/capture latency,
real-song match reliability, Quest 2 CPU/RAM/underruns, and installed QMOD/source
payload behavior. The generated fixtures and build/package evidence below must
not be represented as those live results.

#### Final local validation and handoff

September 6, 2026, after integration and the final lifetime/cancellation review:

- All **27 host test groups passed**, zero failures, in **104.53 seconds**.
  Evidence: `diagnostics/audio-sync-alpha14-validation/host-tests.log` (local,
  ignored by Git). This includes profile/draft tests, operation ownership,
  generated decoder fixtures, capture/resampling, cache leases/overflow,
  automatic matching, common-clock audition/pitch, and existing regression tests.
- The exact embedded downloader-script tests also exercise companion success,
  missing audio, wrong YouTube identity, oversized audio, audio failure and
  cancellation without real network requests. They establish publication/error
  behavior, not current YouTube availability on the headset's network.
- Known-answer matching recovered offset `0.300024 s`, speed `0.979972x`,
  and approximately `0.3934 ms` maximum held-out residual for the generated
  matching fixtures, including AAC. Silence, unrelated material, repetition and
  edits are separately tested. These numbers are synthetic algorithm evidence,
  not measured speaker/video latency or a real-song accuracy guarantee.
- Canonical build-pipeline tests, Linux/Distrobox bootstrap tests and Windows
  preflight PowerShell syntax validation passed. Native compilation ran through
  the existing Linux/WSL shared path with one compile job. The final ARM64 link,
  private FFmpeg ELF-isolation checks, QMOD schema/version/dependency validation
  and deterministic ZIP payload validation passed.
- The completed `Big Screen.qmod` is **20,287,495 bytes**, identifies
  `0.7.0-alpha.14`, and contains the final native mod, the new private
  `libswresample-bigscreen44.so`, all manifest-required runtimes and Sonic
  license/notice files. The manifest stages 17 native libraries and 81 runtime
  files. This remains a development test package, not a published release.
- QMOD SHA-256:
  `286ca3c1db54034e3b5fbd07d4c65e9c018a061aa59b3392fb86deed749a4543`.
  The packaged `libbigscreen.so` matches `build/libbigscreen.so` byte-for-byte:
  `ec6634a4a25f6b0efdb1e0d98201e04109254c17190b9ce07bae41357f5c7aa9`.
- No Quest installation, runtime test, ADB access, commit or publication was
  performed in this resumed pass. The next action is installation followed by
  the focused live-test order above. Start with a newly downloaded video so its
  matching audio is present; old silent downloads intentionally remain basic-only.

User-facing operation notes are in `docs/AUDIO_SYNC.md`. Keep this evidence
separate from future headset results and append those results here after testing.

#### Authorized Quest installation

September 6, 2026: deployed the above Alpha 14 native build to the connected
Quest 2 using `scripts/copy.ps1 -UseExistingVerifiedBuild`. Dependencies and
source ownership checks passed. The complete receipt contains 98 verified
payload files; installed native SHA-256 matches the candidate recorded above.
Settings and videos were preserved.

The Windows notice staging/manifest lists initially omitted the two Sonic
notices already present in the canonical QMOD. Added those two entries and
repeated the guarded deployment. All 81 runtime destination mappings now match
the QMOD. No native code or QMOD bytes changed in this deployment correction.

The restart request was intercepted by Quest's controller-required launch
dialog (`common_system_dialog_app_launch_blocked_controller_required`), not a
new crash. No interactive feature test was performed. The user needs to wake
the controllers and launch Beat Saber. ADB was stopped after verification.
