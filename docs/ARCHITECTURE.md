# Architecture and ownership

Unity and Beat Saber objects are accessed only from game-thread lifecycle and
Update hooks. FFmpeg decoding, yt-dlp work, thumbnail requests, and storage scans
run on background workers and exchange plain data through locked mailboxes or
atomic JSON files. No worker touches a Unity texture, view, or map object.

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

Menu state is event-driven or deliberately rate-limited. Resolved map video
descriptors and video thumbnails are cached, the thumbnail cache has a bounded
LRU, storage totals are sampled at most once per second, and download status is
published and consumed at bounded rates. Terminal downloader state remains a
durable atomic write; transient progress does not fsync Android flash for every
network block.

The local-video browser follows the same boundary. Unity renders immutable
directory snapshots on the center screen, while a worker thread enumerates the
folder and opens MP4 containers through FFmpeg. Custom/WIP songs start at their
map folder; other songs start at the automatically created Video Import folder.
Navigation is confined to `/sdcard`. A selected file is referenced in place as
user-owned media, so assignment replacement and Remove Video never delete it.

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
background. Output is scaled before entering the mailbox, reducing CPU memory
traffic and Unity texture-upload cost. Fully opaque picture/background modes
write depth; alpha-blended picture or background modes use transparent queues.

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
