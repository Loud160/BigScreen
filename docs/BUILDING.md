# Building and packaging

## Supported target

The tracked package targets Beat Saber `1.37.0_9064817954`, ARM64 Quest, and C++20. Dependency versions are locked in `qpm.json`/`qpm.shared.json`; native code generated for another Beat Saber build must not be presented as compatible without a separate build and headset test.

## Tools

- Git
- Windows PowerShell 5.1 on Windows or PowerShell 7 (`pwsh`) on Linux
- CMake and Ninja
- QPM-RS and the Android NDK expected by the Quest modding toolchain
- Linux or WSL with `make`, `curl`, and Android NDK r27c for the private FFmpeg build
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

`scripts/build-ffmpeg-lgpl.sh` builds FFmpeg 4.4.8 from source for Android ARM64. Set `ANDROID_NDK_ROOT` to a Linux NDK r27c directory when it is not installed at the script's documented default. The script enables only H.264 decoding, MP4/MOV demuxing, the local-file protocol, and `libswscale`; it explicitly omits GPL, version-3-only, and nonfree components. Its outputs use `-bigscreen` SONAMEs and `BIGSCREEN_LIB*` symbol versions so Android cannot resolve them to another mod's FFmpeg libraries.

For LGPL corresponding-source redistribution, publish the unmodified FFmpeg 4.4.8 archive identified in `extern/ffmpeg-lgpl/BUILD-INFO.txt` alongside the QMOD. The QMOD itself includes the exact build record and generated source diff. The upstream archive SHA-256 and all transformations are recorded in `scripts/build-ffmpeg-lgpl.sh`.

## Host tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/test.ps1
```

The test suite covers deterministic library keys/path rules, quality fallback, error-circuit timing, URL allowlisting, Cinema metadata parsing, Chroma detection, and the embedded Python downloader scripts. Host tests do not replace an ARM64 Quest build or VR regression testing.

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
- Big Screen, QuickJS-NG, and other third-party notices/license texts;
- all four private LGPL FFmpeg shared libraries and their build/source-change records.

Inspect the archive before release:

```powershell
tar -tf './Big Screen.qmod'
```

The generated `mod.json` must report the same version as `qpm.json`, `qpm.shared.json`, and `mod.template.json`, the exact package version, all required libraries, and every runtime file copy.

## Deployment to a test headset

With the intended Quest connected and authorized:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/copy.ps1
```

Deployment scripts are development conveniences, not part of ordinary end-user installation. Confirm the active Beat Saber package/version before replacing a mod binary.

When the controller launch gate prevents unattended Beat Saber startup, compile `tests/android_ffmpeg_smoke.c` with the same Android NDK and link it against the four staged `*-bigscreen.so` files. Running it through ADB against a real H.264 MP4 verifies Android dynamic loading, private symbol versions, demuxing, native H.264 decoding, and RGBA scaling without bypassing the Quest safety screen.

## CI

`core-tests.yml` runs host tests, verifies public documentation files, and uses
pinned Node/pnpm tooling to rebuild yt-dlp plus yt-dlp-ejs from source and
compare the full payload with the shipped release. `build-ndk.yml` restores QPM
dependencies, uses the Quest NDK build, creates a validated QMOD, and uploads
artifacts. Third-party actions are pinned to commit SHAs. The local canary-NDK
composite action fixes the release URL/version; changing it requires a clean
package build.

## Threading rules

Unity and Beat Saber objects belong to the main thread. FFmpeg decoding and CPython/yt-dlp work run on dedicated native workers and communicate through synchronized mailboxes or atomic JSON status files. New code must not call Unity APIs from those workers. Long filesystem/network/decode work must not be added to a per-frame UI callback.

## Error behavior

Expected content/network failures report a readable error but do not trip the internal safety circuit. Internal exceptions are guarded at lifecycle boundaries. During gameplay, dialogs are deferred until the map ends. A second internal error within three minutes disables Big Screen to stop repeated failures while leaving its menu available.
