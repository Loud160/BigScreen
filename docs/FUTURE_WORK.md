# Future work

## TODO: Rebuild the deferred menu gameplay-environment preview safely

The attempted partial gameplay host was removed after repeatable Quest menu
crashes. Do not restore or repeat that scene-stack implementation. The full
failure history, Beat Saber scene contract, rejected approaches, recommended
render-only proxy design, and required headset validation are recorded in
[`MENU_ENVIRONMENT_PREVIEW_DEFERRED.md`](MENU_ENVIRONMENT_PREVIEW_DEFERRED.md).

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

- Consider a separate download-quality preference only if testing shows that
  retaining one source-quality download and downscaling during playback causes
  unacceptable gameplay cost. Avoid automatically accumulating multiple copies
  of the same video at different resolutions.
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
