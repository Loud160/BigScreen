# AI-assisted development records

This archive preserves selected prompts, planning notes, and independent review
reports used during Big Screen's development. They are retained deliberately to
show how detailed requirements, explicit safety boundaries, repository history,
runtime evidence, and human review can be used to produce disciplined
AI-assisted changes instead of unverified patch-on-patch output.

These files are historical development records, not the canonical description
of the current release. Runtime behavior is defined by the source and the
maintained player, mapper, architecture, build, and troubleshooting documents
linked from the main [documentation index](../README.md). Some proposals in
this archive were changed, deferred, rejected, or superseded after testing.

## Prompts and implementation contracts

- [Professional code-review prompt](prompts/Full%20Professional%20Code%20Review%20of%20Big%20Screen.md)
- [Menu-download logging and source-removal workflow](prompts/MENU_DOWNLOAD_SESSION_LOGGING_AND_SOURCE_INSTALL_REMOVAL_WORKFLOW.md)
- [Logging-writer and MBF-conflict investigation](prompts/RESOLVE_LOGGING_WRITER_DESIGN_AND_REPRODUCE_SOURCE_DEPLOY_MBF_CONFLICT.md)
- [Mixed MBF/source ownership policy](prompts/RESOLVE_MIXED_MBF_SOURCE_OWNERSHIP_AND_LEGACY_SOURCE_INSTALLS.md)
- [Diagnostic logging and ownership-safe deployment contract](prompts/DETAILED_DIAGNOSTIC_SESSION_LOGGING_AND_OWNERSHIP_SAFE_SOURCE_DEPLOY_REMOVAL.md)

## Planning records

- [Movement-authoring planning](planning/bigscreen-movement-authoring-planning.md)
- [Buffered decoder design](planning/BUFFERED_DECODER_DESIGN.md) — deferred
  bounded-frame-queue proposal with memory, synchronization, and measurement
  requirements; it is not an implemented playback feature.
- [Self-updating core feasibility](planning/SELF_UPDATING_FEASIBILITY.md) —
  research into a signed A/B core updater and automatic rollback.
- [Quest and PC dual-platform feasibility](planning/DUAL_PLATFORM_ARCHITECTURE_FEASIBILITY.md) —
  research into a shared core and separate platform hosts.
- [GPU video-pipeline feasibility](planning/GPU_VIDEO_PIPELINE_FEASIBILITY.md) —
  research into YUV upload, SurfaceTexture, and AHardwareBuffer/Vulkan paths.
- [GPU video 60 FPS optimization plan](planning/GPU_VIDEO_60FPS_OPTIMIZATION_PLAN.md) —
  Quest 2 checkpoint measurements and the recommended read-ahead, NV12, upload,
  and presentation-profiling sequence for improving 60 FPS delivery.
- [Deferred menu environment preview](planning/MENU_ENVIRONMENT_PREVIEW_DEFERRED.md) —
  failed approaches, crash evidence, and requirements for a future safe design.

## Independent reviews

- [Claude Code Opus 4.8 review](reviews/BigScreen-Code-Review_1.md) — external
  review input retained for provenance; its findings are guidance and are not
  automatically accepted as current repository truth.
- [External code-review resolution](reviews/CODE_REVIEW_RESOLUTION.md) — the
  recorded evaluation of accepted, rejected, and deferred review findings.
