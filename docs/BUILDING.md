# Building and packaging

## Supported target

The tracked package targets Beat Saber `1.37.0_9064817954`, ARM64 Quest, and C++20. Dependency versions are locked in `qpm.json`/`qpm.shared.json`; native code generated for another Beat Saber build must not be presented as compatible without a separate build and headset test.

## Tools

- Git
- Windows PowerShell 5.1 on Windows or PowerShell 7 (`pwsh`) on Linux
- CMake and Ninja
- QPM-RS and the Android NDK expected by the Quest modding toolchain
- Linux or WSL with `make`, `curl`, and Android NDK r27d (27.3.13750724) for the private FFmpeg build
- Python 3 only for the optional host test that extracts and tests the embedded downloader scripts
- ADB for deployment/log/tombstone helper scripts

## Fresh build

```powershell
git clone <repository-url>
cd BigScreen
qpm restore
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build.ps1 -clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/createqmod.ps1
```

## Portable repository boundary

A fresh clone contains the source, headers, tests, documentation, licenses,
QPM manifests, CI configuration, and the versioned build/fetch recipes needed
to reproduce Big Screen. It intentionally does not contain developer-specific
editor settings, CMake caches, NDK installations, restored QPM dependencies,
downloaded CPython/yt-dlp/QuickJS-NG sources, compiled FFmpeg libraries, QMOD
packages, headset captures, crash-analysis extracts, or AI review/prompt files.

Those inputs and outputs are generated beneath ignored cache, `extern`,
`build*`, and `artifacts` paths. The dependency scripts retrieve the required
upstream sources and binaries from pinned URLs and reject content that does not
match the committed SHA-256 values. Consequently, copying a local generated
library into Git is neither required nor a substitute for a reproducible build.

Before committing, use `git status --short` and `git diff --cached --name-status`
to verify that only intentional source files are staged. Do not use a forced
add to bypass `.gitignore` for generated libraries or local diagnostic files.

The current development package explicitly enables the hard-coded Up & Down
proof of concept with `BIGSCREEN_UP_DOWN_SHOWCASE=ON`. Do not rely on a cached
CMake value for this feature: an older `OFF` cache compiles its target matcher
into a permanent false result and silently produces ordinary single-screen
playback on the demonstration map.

In addition to the CPython, certifi, yt-dlp, and FFmpeg inputs, the dependency
scripts retrieve a pinned QuickJS-NG source archive. Every downloaded artifact
has a fixed SHA-256; review the upstream release, ABI, contents, and license
before changing a version or expected hash.

`scripts/fetch-quickjs-ng.ps1` retrieves the official QuickJS-NG 0.16.1
amalgamated source. CMake compiles it directly into `libbigscreen.so` without
`QJS_BUILD_LIBC`; the engine therefore has no direct file, process, or network
APIs. `QuickJsEngineTests.cpp` validates successful output, syntax errors,
interrupts, recursion/stack containment, and source/output ceilings in Release
builds before the Quest build begins.

## Rebuilding the downloader from source

Normal builds package the pinned official yt-dlp zipimport release after its
SHA-256 and required yt-dlp-ejs files are verified. The complete independent
source path is also tracked in this repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build-downloader-from-source.ps1
```

The script downloads the pinned yt-dlp 2026.07.04 and yt-dlp-ejs 0.8.0 source
archives, verifies their hashes, invokes yt-dlp-ejs's own `hatch_build.py` and
committed lockfile, checks the resulting solver hashes, and packages yt-dlp's
Python sources with those solvers. It then compares every archive entry and
byte with the official release before executing the rebuilt runtime's version
command. Output is written to `build/downloader-source/yt-dlp-source-built`.

The source rebuild requires Python 3 plus one package manager/runtime supported
by that yt-dlp-ejs release (pnpm, Deno, Bun, or npm/Node). Upstream sources and
installed JavaScript dependencies remain under ignored `extern`/build paths;
the reproducible recipes, URLs, versions, expected source hashes, expected
solver hashes, and packaging logic are committed to Git. Use `-Force` after a
reviewed version change to replace the versioned extraction.

The dependency scripts download pinned Android CPython, certifi, yt-dlp, FFmpeg source, and supporting artifacts. Each expected artifact has a fixed SHA-256 in the script; a mismatch stops the build. Do not “fix” a mismatch by changing only the hash—review the upstream release, ABI, contents, and license first.

The CPython fetch step verifies a complete extraction, not merely the presence
of its main shared library, so an interrupted extraction is repaired on the
next build. CMake links only `libpython3.14.so` directly. The packaged SSL,
crypto, and SQLite libraries are runtime dependencies of CPython extension
modules and must not appear as direct `DT_NEEDED` entries in
`libbigscreen.so`. The build script stages them for packaging without placing
them in QPM's recursively linked input directory.

`scripts/build.ps1` stages both FFmpeg 4.4.8 and FFmpeg 9.0.1 by invoking `scripts/build-ffmpeg-lgpl.sh` for each pinned source release. Set `ANDROID_NDK_ROOT` to a Linux NDK r27d directory when it is not installed at the script's documented default, or run `scripts/install-pinned-ndk.sh` to fetch and hash-check the official r27d archive. Each build enables software H.264, VP8, and VP9 plus Android MediaCodec H.264, H.265/HEVC, VP8, and VP9; JNI/MediaCodec integration; MP4/MOV and Matroska/WebM demuxing; the local-file protocol; and `libswscale`. It explicitly omits GPL, version-3-only, and nonfree components. The build fails if configure silently drops any required decoder, demuxer, or JNI/MediaCodec support. Matroska support is required because the 1440p downloader deliberately stores VP9 in its native WebM container.

The outputs use separate `-bigscreen44` / `-bigscreen9` SONAME suffixes and `BIGSCREEN44_LIB*` / `BIGSCREEN9_LIB*` symbol-version namespaces. The matching decoder implementation is also linked as `libbigscreen-ffmpeg44-backend.so` or `libbigscreen-ffmpeg9-backend.so`. This separate-backend boundary matters: putting two ordinary FFmpeg call sites directly in one shared object would let both bind to the first runtime despite matching headers. Each backend instead records hard versioned-symbol requirements for exactly one runtime, while an FFmpeg-type-free facade chooses the backend and moves the existing reusable RGBA buffers without an extra frame copy.

For LGPL corresponding-source redistribution, publish both unmodified archives identified in the packaged `FFMPEG-4.4.8-BUILD-INFO.txt` and `FFMPEG-9.0.1-BUILD-INFO.txt` records alongside the QMOD. The QMOD includes both exact build records and generated source diffs. Both upstream archive SHA-256 values and all transformations are recorded in `scripts/build-ffmpeg-lgpl.sh`.

To make a clean dual-runtime build and retain a clearly named comparison QMOD,
native library, and both reproducibility records, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build-ffmpeg-comparison.ps1
```

Install that single QMOD, then use **Misc > Performance > Use FFmpeg 9** to
switch between the bundled runtimes and **Hardware Video Decoding** to switch
between supported software decoders and MediaCodec. Both switches default off. An
active Video Library preview is recreated at its retained song position, while
gameplay uses the selection when the next map opens. Compare the same map,
screen resolution, FPS cap, headset charge/thermal state, and playback
interval. The performance overlay and results summary identify the runtime
that actually opened the runtime/backend and report presented-frame loss, decode
delay, automatic reductions, and RGBA allocation count. Keep 4.4.8 as the
default until 9.0.1 is at least equivalent in repeated Quest 2 and Quest 3
tests.

## Host tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/test.ps1
```

The test suite covers deterministic library keys/path rules, source-aware frame
expectations, quality fallback, error-circuit timing, URL allowlisting, Cinema
metadata parsing, Chroma detection, embedded Python downloader behavior, and
cross-file toolchain/licensing/settings invariants. Linux CI additionally
generates landscape and portrait H.264 fixtures and exercises the real FFmpeg
worker, timestamps, scaling, shutdown, and reusable RGBA buffers. Host tests do
not replace an ARM64 Quest build or VR regression testing.

## QMOD contents

`scripts/createqmod.ps1` regenerates `mod.json`, validates required runtime
files, creates a fresh archive without update-mode residue, verifies that its
unique entries exactly match the manifest, and only then atomically replaces
an older QMOD. It packages:

- `libbigscreen.so` and declared native dependencies;
- the CPython standard library and Android extension modules;
- the immutable shipped yt-dlp baseline containing yt-dlp-ejs, Big Screen
  challenge-provider module, and certifi CA bundle;
- runtime integrity manifest;
- Big Screen's GPLv3 text, section 7 terms, attribution notice, and the
  QuickJS-NG, CPython, OpenSSL, SQLite, certifi, yt-dlp, and FFmpeg notices;
- both private LGPL FFmpeg sets (eight FFmpeg libraries), two decoder backend
  libraries, and both versions' build/source-change records.

Inspect the archive before release:

```powershell
tar -tf './Big Screen.qmod'
```

Before distributing the QMOD publicly, publish the complete matching Big
Screen Corresponding Source and both FFmpeg source versions/build changes, then
identify <https://github.com/Loud160/BigScreen> in the repository/release
metadata and `NOTICE`. The repository may remain private during development,
but its matching release source must be publicly accessible when the QMOD is
publicly distributed. The BSQMods repository consumed by ModsBeforeFriday
accepts QMOD release metadata, but that packaging mechanism does not replace
GPL/LGPL source availability obligations.

For an ELF dependency audit, use the NDK copy of `llvm-readelf` and verify that
the direct Python dependency list contains `libpython3.14.so` but not
`libssl_python.so`, `libcrypto_python.so`, or `libsqlite3_python.so`.
`scripts/validate-ffmpeg-elf.ps1` is run after every normal build and again
before packaging. It verifies both backend dependencies, rejects cross-version
FFmpeg libraries/symbols, and requires each backend's exported factory.

The generated `mod.json` must report the same version as `qpm.json`, `qpm.shared.json`, and `mod.template.json`, the exact package version, all required libraries, and every runtime file copy.

## Deployment to a test headset

With the intended Quest connected and authorized:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/copy.ps1
```

On Windows, [Build-And-Deploy.bat](../Build-And-Deploy.bat) provides the same
workflow as a double-clickable launcher. It keeps the console open afterward,
prints a clear success or failure result, and preserves the underlying build or
ADB error output for diagnosis. The batch file does not maintain a second copy
of the deployment logic; it calls `scripts/copy.ps1`, which remains the single
authoritative build, validation, complete-runtime deployment, and restart path.

Deployment scripts are development conveniences, not part of ordinary end-user installation. Confirm the active Beat Saber package/version before replacing a mod binary.

When the controller launch gate prevents unattended Beat Saber startup, compile `tests/android_ffmpeg_smoke.c` twice with the same Android NDK: once against the four `*-bigscreen44.so` files and once against the four `*-bigscreen9.so` files. Running each binary through ADB against the same real H.264 MP4 verifies Android dynamic loading, private symbol versions, demuxing, native H.264 decoding, and RGBA scaling without bypassing the Quest safety screen. The complete mod build and ELF audit are still required to verify the two backend libraries' exact `DT_NEEDED` and symbol-version bindings.

## CI

`core-tests.yml` runs host tests, verifies public documentation files, and uses
pinned Node/pnpm tooling to rebuild yt-dlp plus yt-dlp-ejs from source and
compare the full payload with the shipped release. `build-ndk.yml` restores QPM
dependencies, installs the pinned official Android NDK r27d, creates a
validated QMOD, and uploads artifacts. Third-party actions are pinned to commit
SHAs. Changing the NDK requires synchronized QPM metadata, CI, FFmpeg, clean
one clean dual-runtime build, and headset regression testing.

## Threading rules

Unity and Beat Saber objects belong to the main thread. FFmpeg decoding and CPython/yt-dlp work run on dedicated native workers and communicate through synchronized mailboxes or atomic JSON status files. New code must not call Unity APIs from those workers. Long filesystem/network/decode work must not be added to a per-frame UI callback.

## Error behavior

Expected content/network failures report a readable error but do not trip the internal safety circuit. Internal exceptions are guarded at lifecycle boundaries. During gameplay, dialogs are deferred until the map ends. A second internal error within three minutes disables Big Screen to stop repeated failures while leaving its menu available.
