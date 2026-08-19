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

## Independent reviews

- [Claude Code Opus 4.8 review](reviews/BigScreen-Code-Review_1.md) — external
  review input retained for provenance; its findings are guidance and are not
  automatically accepted as current repository truth.
