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
- [Current development checkpoint](KNOWN_ISSUES.md) — unverified behavior and the required Quest retest matrix for the current preservation checkpoint.
- [AI-assisted development records](ai-assisted-development/README.md) — retained prompts, implementation contracts, planning notes, and external reviews. These are historical engineering records rather than canonical runtime documentation.
- [Future work](FUTURE_WORK.md) — intentionally deferred ideas and known areas for wider testing.
- [Third-party notices](../THIRD_PARTY_NOTICES.md) and [provenance](../PROVENANCE.md).
