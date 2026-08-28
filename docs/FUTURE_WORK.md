# Future work

## TODO: Optionally inherit PC Cinema's default canvas for metadata-only configs

Big Screen currently lets **Respect Mapper Settings** claim the physical screen
canvas only when a Cinema file explicitly supplies screen geometry. A file such
as Dragula's `cinema-video.json`, which contains video identity and timing but
no placement fields, therefore keeps the player's selected Big Screen layout.

PC Cinema instead fills omitted geometry with its own defaults: position
`{x: 0, y: 12.4, z: 67.8}`, rotation `{x: -8, y: 0, z: 0}`, height `25`, one
screen, and automatic curvature when the player's Cinema curvature setting is
enabled. For closer PC parity, a future change may make **Respect Mapper
Settings** apply that complete default Cinema canvas whenever mapper-supplied
Cinema metadata is active, while leaving the present player-layout behavior
available when the switch is off.

This is deliberately deferred. Before changing it, verify the desired ownership
rule against metadata-only maps, explicit-geometry maps, local player-assigned
videos, and preview/gameplay parity. Do not treat `bundledConfig` by itself as
proof that a mapper authored custom geometry; this proposal is specifically
about emulating Cinema's documented fallback defaults.

## TODO: Split oversized source files after major behavior stabilizes

This refactor is deliberately deferred until the current playback, downloader,
runtime, and performance changes have received substantial headset testing. It
must remain visible in future release-readiness reviews so working behavior does
not become permanently trapped in a few difficult-to-test files.

Split by responsibility rather than by an arbitrary line limit:

- `VideoLibraryMenu.cpp`: library flow/navigation, catalog/filter model,
  song-list cell presenter, video-details editor, preview transport, and the
  thumbnail cache/loader.
- `SettingsMenu.cpp`: settings flow/navigation, individual tab builders, and
  screen-editor integration.
- `DownloadManager.cpp`: download coordinator, embedded CPython host, yt-dlp
  updater/rollback, thumbnail downloader, and status persistence. Move the
  embedded Python programs into normal `.py` sources and generate their C++ raw
  resources during the build so they can be linted and tested directly.
- `main.cpp`: gameplay, menu, environment, and application-lifecycle hooks.

Perform the extraction incrementally in behavior-preserving commits. Run host
tests and the Quest ARM64 build after each move, and update `ARCHITECTURE.md`,
`BUILDING.md`, and the relevant user documentation whenever ownership or runtime
behavior changes. Do not combine the entire split with unrelated feature work;
that would make regressions unnecessarily difficult to isolate.

- Evaluate the bounded timestamped decoder queue described in the archived
  [buffered-decoder planning note](ai-assisted-development/planning/BUFFERED_DECODER_DESIGN.md)
  only after native-resolution/FPS-only playback
  has been measured on-device. Keep it opportunistic and memory-bounded so a
  short decoder stall can consume prepared frames without adding persistent
  latency or converting frames that the FPS limiter would never present.
- Re-evaluate the hidden Glass Desert environment override if a future Beat
  Saber environment makes it useful; implementation support remains in source
  but the user-facing switch is intentionally omitted.
- Extend diagnostics if a future Beat Saber mode introduces a results/failure
  screen that uses neither the normal ResultsViewController nor the standard
  failed-level controller.
- The showcase-only corner-warp and flag-wave engine deliberately has no UI,
  mapper file format, or public mod API. If the proof of concept earns mapper
  interest, design a versioned, validated external timeline contract rather
  than exposing the current hard-coded Up & Down data structures directly.
- The showcase-only glass fracture engine is intentionally under the same API
  boundary. Its seeds, impact groups, freeze/live selection, shard transforms,
  and rejoin phases are programmatic proof-of-concept controls—not a mapper
  format. A future external contract should wrap validated parameters and
  deterministic song-time cues instead of serializing internal mesh buffers.
