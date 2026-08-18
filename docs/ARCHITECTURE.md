# Architecture and ownership

Unity and Beat Saber objects are accessed only from game-thread lifecycle and
Update hooks. FFmpeg decoding, yt-dlp work, mod-release checks, thumbnail requests, and storage scans
run on background workers and exchange plain data through locked mailboxes or
atomic JSON files. No worker touches a Unity texture, view, or map object.

The mod-release checker uses its own worker and status mailbox, separate from
video downloads and yt-dlp package updates. An atomic process-lifetime guard
allows the automatic check only once per Beat Saber session; a manual check may
run again but cannot overlap an active release check. Only the main-thread menu
refresh consumes notices and opens BSML dialogs.

The downloader embeds CPython and compiles QuickJS-NG into `libbigscreen.so`.
Big Screen registers a built-in Python module and a preferred yt-dlp JavaScript
challenge provider, so modern YouTube extraction can run without `execve`,
Termux, Node, Deno, or a writable executable. Each EJS evaluation owns an
isolated QuickJS runtime with memory, stack, source, output, and time limits.
yt-dlp updates are staged transactionally and tested with both real EJS solver
bundles, the provider API, and the engine before activation.

Beat Saber's audio/song position is the only video clock. That preserves pause,
practice speed, seeking, and Replay behavior. The decoder uses each frame's
container duration when available, with nominal FPS only as a fallback, so VFR
sources do not inherit a false constant cadence. It uses a one-frame mailbox,
drops superseded frames instead of blocking the game thread, and recycles a
bounded pool of RGBA vectors instead of allocating a multi-megabyte buffer for
every presented frame. Gameplay transitions pre-open and prime FFmpeg before
the song clock begins; Unity geometry is still created only in the gameplay
scene.

Playback has one FFmpeg-type-free facade and two separately linked decoder
backends. One backend is compiled against the private 4.4.8 headers and
`BIGSCREEN44_LIB*` symbols; the other is compiled against the private 9.0.1
headers and `BIGSCREEN9_LIB*` symbols. The separation is required because the
two releases expose incompatible public structures and ordinary unversioned
references placed together in `libbigscreen.so` could all bind to the first
loaded library. The facade chooses a backend only during `Open`, never while a
worker owns codec state. `VideoFrame` contains standard C++ values only, so its
reusable RGBA allocation moves to Unity without an additional A/B abstraction
copy. The Video Library's compatibility probe remains fixed to the conservative
4.4 runtime and never hands FFmpeg structures to a playback backend.

Each private backend contains LGPL software decoders for H.264, VP8, and VP9,
plus Android MediaCodec paths for H.264, H.265/HEVC, VP8, and VP9. HEVC and
content above 1080p are hardware-only; no software HEVC implementation is
compiled or shipped. The facade obtains the process
Java VM captured by Scotland2 during Android preload and passes it across the
FFmpeg-type-free boundary so each isolated libavcodec instance registers the
VM in its own internal state. MediaCodec is
opened without an Android output Surface: FFmpeg copies the decoder's NV12 or
YUV420P output into a CPU-readable frame, then the existing stride-aware
swscale/RGBA mailbox path continues unchanged. This is not zero-copy, but it
keeps curved screens, transparency, showcase panels, and every shader-facing
feature identical. A hardware worker failure is consumed by the facade, which
reopens the same runtime and file at the latest requested timestamp with
software decoding when policy permits before the session decides playback has
failed. Unsupported 10-bit, HDR, and alpha video is rejected explicitly.

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

Menu-controller singletons do not own an IL2CPP hierarchy. A MenuCore flow
recreation calls each menu's `ForgetUi` boundary before new controllers are
created, and the active coordinator is retained through `UnityW`. Per-frame UI
refreshes are gated by the active Big Screen flow. This is required because a
non-null raw pointer from a destroyed menu scene is not a valid liveness check.

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

The screen is ordinary environment-layer geometry. A frame/background mesh and
a separately clipped video-content mesh share one root transform. That split
allows rotation, zoom, pan, perspective tilt, stretching, and black or fully
transparent letterboxing without rewriting decoded pixels. Letterbox alpha and
picture opacity are independent: the background renderer can be removed while
the decoded picture stays opaque, or the picture can blend over either kind of
background. Normal playback preserves the selected file's native dimensions;
only explicitly bounded utility previews may request a smaller decoder output.
Decoded RGBA frames enter the one-frame mailbox at that selected size. Queue and
depth behavior depends on the active video material. Both selectable paths are
included in the current on-device regression matrix; see `KNOWN_ISSUES.md`.

The current Cinema interoperability implementation is normalized by
`MapVideoConfig` before it reaches
Unity. Mapper screen ownership and Chroma environment ownership are independent:
Respect Mapper Settings selects Cinema geometry/effects/environment entries,
while Allow Chroma Override yields the broader scene only after map-wide Chroma
detection. Additional Cinema screens are lightweight `ScreenSurface` instances
that share the primary `Texture2D`; they add geometry/material work but never a
second decoder or RGBA upload. Color correction and vignette run in the decoder
worker after swscale and container rotation, with default-valued metadata taking
the no-processing fast path. The worker factors color correction into cached
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
