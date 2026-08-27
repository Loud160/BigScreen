# Architecture and ownership

Unity and Beat Saber objects are accessed only from game-thread lifecycle and
Update hooks. FFmpeg decoding, yt-dlp work, mod-release checks, thumbnail requests, and storage scans
run on background workers and exchange plain data through locked mailboxes or
atomic JSON files. No worker touches a Unity texture, view, or map object.

The mod-release and yt-dlp release checkers each use dedicated workers and
status mailboxes, separate from video downloads and yt-dlp package installation.
Atomic process-lifetime guards allow each automatic check only once per Beat
Saber session; a manual check may run again but cannot overlap the same active
release checker. Only the main-thread menu refresh consumes notices and opens
BSML dialogs. Each dialog remains attached to the left, right, or center panel
that owns its action and is moved to that controller's frontmost sibling order
before presentation, preventing an invisible dialog blocker from being hidden
behind another menu. ErrorManager's stock Beat Saber prompt follows a separate
rule outside Big Screen's own flow: it is presented by the youngest stable
active flow, brought to the front, and requeued if that host changes before the
player acknowledges it. Three consecutive transfer failures can request an
additional background yt-dlp check; a successful transfer resets that streak.

The downloader embeds CPython and compiles QuickJS-NG into `libbigscreen.so`.
Big Screen registers a built-in Python module and a preferred yt-dlp JavaScript
challenge provider, so modern YouTube extraction can run without `execve`,
Termux, Node, Deno, or a writable executable. Each EJS evaluation owns an
isolated QuickJS runtime with memory, stack, source, output, and time limits.
yt-dlp updates are staged transactionally and tested with both real EJS solver
bundles, the provider API, and the engine before activation.

Accepting a yt-dlp update keeps the Update-tab modal visible for the complete
background operation. Updater Python publishes throttled byte counts, speed,
ETA, and named verification/staging phases to its atomic status file; the
existing polling worker copies that data into the ordinary download snapshot.
Only the Unity-thread menu tick moves the progress bar or changes modal text.
The terminal result replaces that progress view and still requires a fresh Beat
Saber process before the staged Python package can become active. A successful
install therefore offers a confirmed **Close Beat Saber** action. It flushes
Big Screen settings and calls Unity's normal application quit path, allowing
the game and other mods to receive their normal shutdown callbacks; it does not
force-kill the Android process or attempt an unreliable self-relaunch.

Video, map-package, URL-probe, and yt-dlp-update actions are serialized through
one persistent downloader operation worker. A menu callback only validates and
queues an action; it never joins a previous network/Python thread. Python may
publish a terminal status file before C++ has promoted the staged media, so the
public snapshot remains active until file replacement, manifest persistence,
and diagnostic publication have all returned. The manifest commit advertises
its narrow background persistence window, allowing the Video Library to retain
its previous complete view instead of waiting on the filesystem mutex.

Beat Saber's audio/song position is the only video clock. That preserves pause,
practice speed, seeking, and Replay behavior. The decoder uses each frame's
container duration when available, with nominal FPS only as a fallback, so VFR
sources do not inherit a false constant cadence. CPU RGBA output uses a
one-frame mailbox and drops superseded frames instead of blocking the game
thread. The experimental GPU YUV path instead fills a timestamped queue within
a configurable 32–256 MiB on-demand memory budget (64 MiB by default), with an
independent 120-frame safety ceiling. Unity still presents only the newest
picture whose selected song-clock slot is due, except for a bounded recovery
case: when exactly one older picture is no more than one presentation interval
late, Unity presents it now and keeps the newer due picture for the following
display update. Larger or older backlogs are discarded immediately, so this
cannot accumulate A/V delay. Both paths recycle a bounded
pool of frame vectors instead of allocating multi-megabyte buffers for every
presented frame. Gameplay transitions pre-open FFmpeg and opportunistically
fill the reserve before the song clock begins; they never block waiting for a
full queue, and Unity geometry is still created only in the gameplay scene.
Restart, scrub, map replacement, normal completion, and early exit increment
the decode epoch and synchronously empty the queue so an in-flight conversion
cannot republish stale output. Low/empty reserve transitions, bounded catch-up
presentations, forced late drops, and peak due-frame backlog are counted in
memory and appended only at the existing safe performance-log boundary.

Playback has one FFmpeg-type-free facade and two separately linked decoder
backends. One backend is compiled against the private 4.4.8 headers and
`BIGSCREEN44_LIB*` symbols; the other is compiled against the private 9.0.1
headers and `BIGSCREEN9_LIB*` symbols. The separation is required because the
two releases expose incompatible public structures and ordinary unversioned
references placed together in `libbigscreen.so` could all bind to the first
loaded library. The facade chooses a backend only during `Open`, never while a
worker owns codec state. `VideoFrame` contains standard C++ values only, so
either its reusable RGBA allocation, normalized Y/U/V plane allocations, or
one packed YUV-atlas allocation moves
to Unity without an additional A/B abstraction copy. The Video Library's compatibility probe remains fixed to the conservative
4.4 runtime and never hands FFmpeg structures to a playback backend.

Each private backend contains LGPL software decoders for H.264, VP8, and VP9,
plus Android MediaCodec paths for H.264, H.265/HEVC, VP8, and VP9. HEVC and
content above 1080p are hardware-only; no software HEVC implementation is
compiled or shipped. The facade obtains the process
Java VM captured by Scotland2 during Android preload and passes it across the
FFmpeg-type-free boundary so each isolated libavcodec instance registers the
VM in its own internal state. MediaCodec is
opened without an Android output Surface: FFmpeg copies the decoder's NV12 or
YUV420P output into a CPU-readable frame. The default **GPU Video Conversion**
path keeps 8-bit SDR 4:2:0 in reusable YUV allocations and places
eligible future pictures in the bounded timestamp queue. The default GPU
layout uploads separate Y/U/V planes. A second default-off consolidated mode
writes luma above side-by-side chroma regions in one R8 atlas, reducing three
Unity uploads and `Apply()` calls to one without repacking on UnityMain.
Presentation performs YUV conversion, container rotation, mapper color
correction, and vignette once into one shared RGBA RenderTexture. It is not
zero-copy decoding; it reduces transported/uploaded bytes, removes the CPU
full-frame conversion/rotation work, and gives brief worker stalls a prepared
reserve. Thumbnails retain the bounded RGBA path.
The established stride-aware swscale/RGBA mailbox remains selectable for A/B
testing and is the automatic per-session fallback described below.
If the packed shader, atlas allocation, or atlas layout cannot be used, that
playback session first returns to the established three-plane GPU path. An
unsupported YUV pixel layout or a separate failure of the three-plane Unity
resources returns the session to RGBA while preserving the shared presentation
texture and screen choreography. A hardware worker failure is consumed by the facade, which
reopens the same runtime and file at the latest requested timestamp with
software decoding when policy permits before the session decides playback has
failed. Unsupported 10-bit, HDR, and alpha video is rejected explicitly.

Stopping a decoder signals both the worker condition variable and FFmpeg's I/O
interrupt callback. Unity waits no longer than four milliseconds for ordinary
cleanup; a backend that needs longer is transferred intact to a process-lifetime
retirement worker, which owns the final join and FFmpeg/MediaCodec destruction.
Container open and stream discovery have a one-second interrupt deadline. A
finished download does not automatically open its decoder—the explicit Play
action starts preview preparation after durable publication has completed.

The URL probe publishes exact compatible source tiers through an atomic JSON
status file. Both the Video Library and song-selection modal consume that same
list and write the chosen height plus the saved FPS ceiling into the download
job. Downloads use H.264 MP4 through 1080p and VP9 WebM at 1440p. Replacement
files are downloaded to a sibling staging path; the old managed file remains in
place until the new file and manifest assignment commit successfully.

Menu state is event-driven or deliberately rate-limited. Resolved map video
descriptors and video thumbnails are cached, the thumbnail cache has a bounded
LRU, storage totals are sampled at most once per second, and download status is
published and consumed at bounded rates. Terminal downloader state remains a
durable atomic write; transient progress does not fsync Android flash for every
network block.

The optional showcase launcher is a main-thread state machine layered over
those same boundaries. It checks Chroma/Noodle capabilities through SongCore,
but its center-screen readiness view performs no network work on activation.
Map and video downloads are separate explicit actions. The map action uses the
existing CPython worker to query BeatSaver and safely extract one hash-pinned
package, then waits on SongCore's asynchronous refresh future. ZIP paths, entry
count, compressed size, expanded size, redirect hosts, map audio, and Lawless
Expert+ data are all validated before an atomic publish into Big Screen's own
`DemoLevels` root. The video action uses the ordinary mapper-video downloader.
Only after the exact hash resolves and a playable video exists can the state
machine dismiss Big Screen. It waits for MainFlowCoordinator and the stock main
view to remain stable before presenting Solo, applies the Lawless Expert+ key,
and invokes the normal single-player StartLevel path. The showcase marker is
cleared at gameplay teardown; Results, Replay, Continue, Solo dismissal, and
the user's eventual return to Big Screen all remain Beat Saber's normal path.

## Developer pattern: staged Unity menu prewarming

> **This is an internal lifecycle/performance design, not an advertised Big
> Screen feature.** It is documented prominently because Quest mods with large
> BSML hierarchies should consider the same ownership and scheduling problem.

Creating Unity, HMUI, or BSML objects on a background thread is unsafe. Moving
menu construction to a worker would make the visible hitch less predictable,
not solve it. Building every Big Screen page synchronously from
`MenuFlowCoordinator::DidActivate`, however, charged settings, the song browser
and editor, storage, Showcase, file browsing, thumbnail picking, and several
scene-wide caches to the first press of the menu button. On a Quest 2 that
single activation had been measured at approximately 1.6 seconds even though
later retained activations took roughly 0.1 seconds.

Big Screen therefore **prewarms its retained menu as a main-thread state
machine**:

1. Beat Saber's stock main menu must remain active and transition-free for 90
   consecutive update frames. This lets the game, MenuCore, and other mods
   finish their own startup work first; a transition resets the count.
2. Big Screen creates at most one logical page, controller group, or scene
   cache per stage. Three ordinary update frames separate the stages so Unity
   can finish layout and mesh work before the next group is allocated.
3. Prewarming is never an access gate: the Big Screen button remains usable.
   If the player enters before hidden construction finishes, `DidActivate`
   completes the remaining UI stages through the same authoritative builder.
   It then attaches the left/right controllers through HMUI's normal
   `ProvideInitialViewControllers` lifecycle.
4. Hidden construction does not activate the Video Library, claim preview
   ownership, select a map, start a decoder, or change environment visibility.
   Those are visible-activation responsibilities. Scene discovery stages only
   populate weak Unity caches; the requested settings are reconciled when Big
   Screen actually opens.
5. Every stage logs its name and elapsed time. A regression can therefore be
   attributed to one page or cache instead of appearing only as a vague
   first-open delay.

Retaining UI does not mean retaining a frozen song list. SongCore's completed
song-load event publishes a thread-safe invalidation flag, which Big Screen
consumes on Unity's update thread. The inexpensive level-pointer model is built
first; first-use video descriptors are then resolved four maps at a time while
the stock menu is idle, or eight maps at a time while Big Screen's browser is
open. Mapper JSON and managed-file discovery can touch Quest shared storage, so
this optional cache work is deliberately separate from menu readiness. A map
installed after startup can therefore expose its mapper video, download action,
and Configure Video shortcut without reconstructing the complete UI or pausing
the menu for a large library scan. If the editor is already open, catalog work
waits until the browser is visible so an active selected-level object is never
swapped underneath its callbacks.

The Solo **Configure Video** shortcut passes a stable level ID into the same
retained flow. On first activation HMUI receives that editor as the initial
right-side controller. On later activations Big Screen first lets HMUI restore
the retained browser, waits two ordinary update frames for the enclosing
presentation to finish, and then invokes the same browser-to-editor callback as
a normal song-row selection. Performing that replacement inside `DidActivate`
is not reliable: HMUI can accept the selected map and still overwrite the visible
right panel while completing its parent transition.

Menu re-entry uses lifecycle ownership rather than polling retained Unity
parents. `BackButtonWasPressed` marks dismissal in progress and HMUI's
`DidDeactivate` callback releases both the main-menu and Configure Video entry
paths. `PresentSharedMenu` still validates the actual active presenter and its
transition state on every click. A timed fail-safe handles an interrupted
dismissal only after the retained Big Screen coordinator is no longer alive;
it never dereferences destroyed parent-flow wrappers.

The master switch and safety circuit remain authoritative. Automatic prewarming
does no work while the mod is disabled or ErrorManager is recovering from a
failure. If either condition becomes true between stages, preparation pauses
without destroying the already-built inactive pages; it can resume only after
the user deliberately enables the mod again. An explicit attempt to open a
disabled Big Screen instance retains a synchronous recovery path so the master
switch cannot make its own UI permanently inaccessible.

This design intentionally combines three properties that are easy to lose in
an otherwise reasonable optimization: Unity-thread ownership, bounded work per
frame, and lifecycle cancellation. Retaining only the first two can keep doing
unwanted work after a circuit breaker trips; retaining only the last two can
tempt a mod to create Unity objects from an unsafe worker. Other Quest mods
with substantial first-open UI should measure their own construction stages
and apply the pattern only after their normal menu hierarchy is stable.

Menu-controller singletons do not own an IL2CPP hierarchy. A MenuCore flow
recreation calls each menu's `ForgetUi` boundary before new controllers are
created, and the active coordinator is retained through `UnityW`. Per-frame UI
refreshes are gated by the active Big Screen flow. This is required because a
non-null raw pointer from a destroyed menu scene is not a valid liveness check.

Every Big Screen flow dialog remains attached to the controller that owns the
action that opened it. Immediately before and after `Show`, its transform is
moved to that controller's final sibling position, keeping the visible dialog
and its input blocker together above the left, right, or center panel. The stock
song-selection resolution dialog remains owned by Beat Saber's center
song-detail controller because it is outside Big Screen's flow. Internal-error
prompts use Beat Saber's shared prompt only after the youngest active flow and
its top controller are stable. If a transition dismisses that prompt or replaces
its host, ErrorManager restores the active message to the queue instead of
leaving an invisible blocker or silently losing the error. The presenting flow
and prompt are held by Unity-safe GC roots until their dismissal is confirmed;
if an immediate dismissal races a transition, ErrorManager keeps ownership,
keeps the prompt frontmost, and retries rather than orphaning its blocker.

The local-video browser follows the same boundary. Unity renders immutable
directory snapshots on the center screen, while a worker thread enumerates the
folder and opens compatible MP4/MOV or Matroska/WebM containers through FFmpeg. Custom/WIP songs start at their
map folder; other songs start at the automatically created Video Import folder.
Navigation is confined to `/sdcard`. A selected file is referenced in place as
user-owned media. Replacing or unlinking an assignment never deletes it; the
separate red **Delete File** confirmation action may remove it intentionally
after the library revalidates the path and supported media extension.

Power benchmarking follows the same real-time boundary. The gameplay hook
samples Android `BatteryManager`, a monotonic process CPU clock, and the
decoder worker's thread CPU clock at most once per second, then retains those
plain values in pre-reserved memory. CSV creation and append operations happen
only during gameplay teardown. Unsupported Quest fuel-gauge properties remain
empty optionals all the way to disk, preventing unavailable readings from being
mistaken for zero consumption.

Shared dependency diagnosis is deliberately outside every recurring runtime
path. Scotland2's already-loaded package versions are inspected once at the
start of `late_load`, before Big Screen calls BSML, SongCore, or Custom Types.
Compatible versions produce one compact version snapshot in the log. A present
version below Big Screen's declared minimum produces a plain-language error
and queues the existing frontmost Beat Saber dialog; other mismatch classes
remain log-only. If Android or Scotland2 rejects the dependency chain before
`libbigscreen.so` reaches `setup`, an in-process logger or dialog is impossible.
The Windows and Linux support collectors independently re-evaluate package
registrations and payload files and write `DEPENDENCY-DIAGNOSIS.txt` for that
hard-failure boundary.

General logging also has a strict real-time boundary. Every call site targets
the project-owned `BigScreenLogger` facade, which formats a record once and
routes it to Big Screen's private native backend. Paper2 may remain loaded for
other shared dependencies, but Big Screen neither initializes nor calls it.

The native backend writes logcat directly only when Paper is not already doing
so, then moves file work to one owned writer thread. Producers take a short
queue lock and never open, rotate, append, or flush a file themselves. Both
bytes and entries are bounded; warnings and errors have a reserved margin and
may evict older lower-severity records under pressure. Dropped records are
counted and summarized by the writer rather than recursively logging a logger
failure. The active general log is limited to 5 MiB with one previous 5 MiB
rotation. File failures leave logcat available and are retried after a bounded
backoff. Every completed writer batch is flushed from the C++ stream into the
OS before its sequences are acknowledged; this is deliberately not an
`fsync()`, so it improves abrupt-process crash-tail retention without forcing
Quest flash storage for every batch. The writer has no periodic idle timer: it
blocks until a record, explicit flush, dropped-record notice, or shutdown gives
it work. Critical errors request a short bounded completion barrier, while the
synchronous `error-history.log` remains the durable path for user-visible
failures.

The writer is joinable and its state has process lifetime. Reinitialization
first stops and joins the old writer; shutdown stops acceptance, drains queued
records, flushes, closes the file, and joins before owned state can disappear.
Late calls fail open and do not touch destroyed state. Quest validation must
still confirm shutdown behavior and measure native-only versus Paper-only
overhead before the external Paper dependency is removed.

Frame-preparation timing uses cumulative session counters, not an exponential
moving average. Each prepared picture records decoder-worker thread CPU time
separately from elapsed wall time not charged to that thread. The live panel
and results card show the true preparation-CPU average and session peak; the
append-only performance log additionally records average/peak worker wait,
which can include asynchronous MediaCodec waits and thread descheduling. The
measurement interval is reset once after preview/gameplay prewarm and is not
periodically reset by live-panel refreshes.

The screen is ordinary environment-layer geometry. A frame/background mesh and
a separately clipped video-content mesh share one root transform. That split
allows rotation, zoom, pan, perspective tilt, stretching, and black or fully
transparent letterboxing without rewriting decoded pixels. Letterbox alpha and
picture opacity are independent: the background renderer can be removed while
the decoded picture stays opaque, or the picture can blend over either kind of
background. Normal playback preserves the selected file's native dimensions;
only explicitly bounded utility previews may request a smaller decoder output.
Decoded RGBA frames enter the one-frame mailbox at that selected size. GPU YUV
frames may enter the bounded read-ahead queue; its due-time metadata is separate
from source PTS so the FPS ceiling does not prepare or expose skipped pictures.
The reusable pool may retain enough YUV frame sets to refill the configured
queue after a scrub or restart, but queue plus pool is bounded by the same
byte-derived frame capacity. Planar Y/U/V growth is counted as one frame-set
allocation rather than three independent plane allocations. Queue and depth behavior depends on the active video material. Both selectable
paths are included in the current on-device regression matrix; see
`KNOWN_ISSUES.md`.

Video Library looping is an explicit decoder transition, not an ordinary clock
seek. `Restart` clears all prepared decoder output, invalidates the previous pass's
first-frame readiness, flushes the codec, and seeks before any last-frame or EOF
fast path can accept the request. Initial Play and each loop hold the external song
clock stationary for a non-blocking 250 ms decoder pre-roll; Unity keeps ticking
the worker during that interval. The song-preview channel remains stopped until
both that deadline and a picture from the new pass have reached Unity. A Quest
MediaCodec backend that does not produce a frame after the bounded restart
interval is reopened once without recreating the Unity screen; there is no
repeated reopen loop.

The embedded video shader has process lifetime rather than scene lifetime. Its
`AssetBundle` and `Shader` wrappers are retained through `SafePtrUnity` handles,
which are real IL2CPP GC roots. Caching only a raw `Shader*` is unsafe: the
address can remain non-null after scene/GC teardown while Unity's native shader
has already been reclaimed, causing `Material::CreateWithShader` to crash.
The shader also carries `DontUnloadUnusedAsset`, and the small shader-only bundle
remains loaded as its explicit owner. SafePtr liveness is checked on every
screen creation: if Unity nevertheless invalidates a resource after gameplay,
Big Screen first reloads the shader from the retained bundle, then falls back to
a clean embedded-bundle reload. A genuine first-load failure is still remembered
so a hot presentation path cannot retry and spam the log every frame.

The current Cinema interoperability implementation is normalized by
`MapVideoConfig` before it reaches
Unity. Mapper screen ownership and Chroma environment ownership are independent:
Respect Mapper Settings selects Cinema geometry/effects/environment entries,
while Allow Chroma Override yields the broader scene only after map-wide Chroma
detection. Additional Cinema screens are lightweight `ScreenSurface` instances
that share the primary presentation `Texture`; they add geometry/material work
but never a second decoder or frame upload. On the default path, color
correction and vignette run in the decoder worker after swscale and container
rotation. On the experimental GPU path, the same operations run in the one YUV
conversion pass. Default-valued metadata takes the no-processing fast path. The
CPU worker factors color correction into cached
byte-contribution and gamma lookup tables and builds each resolution-specific
vignette mask only when its settings change; later frames do not repeat the
original full-picture `pow`, ellipse-distance, or smooth-step calculations.
Authored vignette alpha removes the independent rectangular backing and requires
the active video material to consume the generated alpha. Additional Cinema
screens reuse the primary screen's uploaded video texture without another
decode or texture upload. The parser/worker paths have host coverage, but the
complete presentation path still requires the on-device checks listed in
`KNOWN_ISSUES.md`.

Cinema environment clones are created before Chroma's delayed prop-group pass,
temporarily offset unless `mergePropGroups` requests merging, then transformed
in a final mapper pass. Cloned Beat Saber lights are explicitly registered with
the active `LightWithIdManager`. Environment-only
`forceEnvironmentModifications` sessions skip FFmpeg and screen creation
entirely. Big Screen does not poll map or playlist files during playback. The
Video Library's explicit Refresh action rebuilds the one-per-session playlist
index and invalidates the selected song's cached mapper definition after an
edited file has been copied back to the Quest.

The bundled Up & Down showcase can additionally deform its shared-texture
surfaces without changing the normal screen path or decoder. Each showcase
panel preallocates a 64-column video mesh and reusable Unity vertex/UV arrays.
Per-frame evaluation composes normalized coordinates, bilinear four-corner
warp, the existing signed/circular curvature, and an optional anchored flag
wave. Song-time waves remain deterministic through pause, seek, practice, and
Replay; a separately selectable real-time clock supports deliberate ambient
motion. Stretch-to-fill preserves the original UVs, while auto-cover crops
inward to avoid blank or edge-clamped pixels after deformation. The unlit
material does not need recalculated normals. The large vertex, UV, and index
buffers are allocated when showcase surfaces are created and reused while they
animate; small control/state containers may still change outside the hot loop.

The same showcase-only vertex path includes a deterministic glass-fracture
system. `CoreLogic` uses a fixed seeded PRNG, radial site placement, rectangular
Voronoi clipping, convex fan triangulation, and impact-proximity reveal groups;
no Unity random state participates. The Up & Down timeline uses the system in
one authored sequence only: eighteen low-point impacts progressively reveal a
single precomputed 200-cell web from about 2:03 through 2:15. An intact surface
renders the revealed edges as one textured seam mesh. At the final break, the
video renderer switches to the triangle-expanded shard mesh and copies the
current shared texture once with `Graphics.CopyTexture`, preserving the same
frozen frame across every falling piece while the following live formation
appears behind it. Bulk motion is calculated once per shard, and a bounded
optional override list can address selected shards without exposing mesh
buffers. Shattered geometry excludes ongoing corner warp and flag-wave updates:
the pane is captured at the break instant, then only deterministic gravity,
tumble, and separation transforms run. The fracture material does not need
normals, so active animation updates only vertices and bounds and performs no
per-frame C++ or managed-array allocation.

Undocked placement is an explicit edit transaction. BSML's controller-tested
floating-screen handle supplies move/rotation tracking, a second handle drives
width/height, and only Save writes the active layout. The temporary canvases,
colliders, and raycasters are destroyed on save, cancel, focus loss, layout or
setting changes, menu exit, and mod disable; the normal playback screen is mesh
only and therefore cannot block menu controller rays.

Some tempting approaches are deliberately not used:

- Android's external executable/process model is not assumed; `/sdcard` is
  mounted `noexec`, and API-29+ apps cannot execute code copied to writable app
  storage. CPython is embedded and QuickJS-NG runs in-process instead.
- Disabling a rotating-laser component did not hide Big Mirror's visible side
  structures; live inspection showed those were separate NearBuilding roots.
- Quantizing FFmpeg timestamps to the FPS limit caused irregular source-frame
  boundaries and stalls, so presentation requests are limited instead.
- Calling the generated FlowCoordinator base DidActivate wrapper recursively
  dispatches into the override and can crash; custom coordinator lifecycle code
  therefore owns activation directly.
- Animating dismissal after HMUI has already deactivated a child controller can
  leave Beat Saber's parent flow without a responsive center view. Big Screen's
  Back/error exits therefore use the immediate dismissal overload and perform
  their own preview/foveation cleanup in DidDeactivate.
- Updating only a UI slider value does not resize an existing decoder texture;
  the preview/session must be safely recreated for a resolution change.
- Recursive traversal of an arbitrary beatmap JSON tree is avoided. Chroma
  detection uses an explicit work stack and caches its result from map-file
  metadata for the current session, preventing repeated large synchronous
  parses and native stack exhaustion.
