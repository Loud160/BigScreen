# Big Screen documentation

This index separates ordinary player instructions from mapper and developer material.

## Players

- [Installation and first run](INSTALLATION.md) — compatible game build, QMOD installation, first launch, and safe removal.
- [Settings reference](SETTINGS.md) — every visible control, its default, its effect, and its interactions.
- [Video Library guide](USER_GUIDE.md) — finding songs, YouTube downloads, local files, timing, playback preview, removal, and storage.
- [Troubleshooting](TROUBLESHOOTING.md) — logs, common download/playback failures, recovery, and the safety circuit.
- [Privacy and network access](PRIVACY.md) — exactly when Big Screen contacts YouTube, GitHub, or thumbnail servers.

## Mappers

- [Mapper video metadata](MAPPER_FORMAT.md) — implemented Cinema media/timing and experimental presentation compatibility, including unsupported-field boundaries.

## Developers and distributors

- [Architecture](ARCHITECTURE.md) — lifecycle, main-thread ownership, decoder/downloader workers, persistence, and failure containment.
- [Downloader security](DOWNLOADER_SECURITY.md) — pinned runtime, checksum verification, update activation, rejection, and rollback.
- [Build dependencies and network downloads](DEPENDENCIES.md) — required host tools, exact automatic downloads, cache behavior, integrity checks, and equivalent manual commands.
- [Building and packaging](BUILDING.md) — toolchain, host tests, Quest build, QMOD contents, and CI.
- [Release checklist](RELEASE_CHECKLIST.md) — the minimum checks before publishing a build.
- [External code-review resolution](CODE_REVIEW_RESOLUTION.md) — accepted fixes, rejected findings, and explicitly deferred follow-up from the August 2026 audit.
- [Current development checkpoint](KNOWN_ISSUES.md) — unverified behavior and the required Quest retest matrix for the current preservation checkpoint.
- [Self-updating core feasibility](SELF_UPDATING_FEASIBILITY.md) — analysis of a signed A/B core updater, startup health confirmation, automatic rollback, Android restrictions, and QMOD compatibility. This feature is not implemented.
- [Quest and PC dual-platform feasibility](DUAL_PLATFORM_ARCHITECTURE_FEASIBILITY.md) — recommended shared C++ core, Quest and PC host boundaries, PC decoding pipeline, and why a Rust rewrite is not recommended.
- [GPU video-pipeline feasibility](GPU_VIDEO_PIPELINE_FEASIBILITY.md) — research into YUV upload, SurfaceTexture, and AHardwareBuffer/Vulkan paths. No replacement pipeline is implemented.
- [Diagnostic-session logging and source-removal workflow](MENU_DOWNLOAD_SESSION_LOGGING_AND_SOURCE_INSTALL_REMOVAL_WORKFLOW.md) — original requirements and investigation scope for future interaction logs, support bundles, and source-install cleanup. No implementation is authorized by this document.
- [Logging writer and MBF conflict analysis](RESOLVE_LOGGING_WRITER_DESIGN_AND_REPRODUCE_SOURCE_DEPLOY_MBF_CONFLICT.md) — follow-up comparison of synchronous, queued, and hybrid logging plus the controlled source-deploy/MBF ownership investigation.
- [Mixed MBF/source ownership policy](RESOLVE_MIXED_MBF_SOURCE_OWNERSHIP_AND_LEGACY_SOURCE_INSTALLS.md) — final design questions for receipts, legacy source installs, conservative removal, and transitions back to MBF. These deployment changes remain unimplemented.
- [Future work](FUTURE_WORK.md) — intentionally deferred ideas and known areas for wider testing.
- [Third-party notices](../THIRD_PARTY_NOTICES.md) and [provenance](../PROVENANCE.md).
