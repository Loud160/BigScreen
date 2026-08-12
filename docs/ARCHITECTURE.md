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
practice speed, seeking, and Replay behavior. The decoder uses a one-frame
mailbox and drops superseded frames instead of blocking the game thread.

The screen is ordinary environment-layer geometry. A frame/background mesh and
a separately clipped video-content mesh share one root transform. That split
allows rotation, zoom, pan, perspective tilt, stretching, and black or fully
transparent letterboxing without rewriting decoded pixels. Output is scaled
before entering the mailbox, reducing CPU memory traffic and Unity texture-
upload cost. Opaque mode explicitly enables depth writing; transparent mode
uses alpha blending.

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
- Updating only a UI slider value does not resize an existing decoder texture;
  the preview/session must be safely recreated for a resolution change.
