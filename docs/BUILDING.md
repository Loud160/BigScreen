# Building and packaging

## Supported target

The tracked package targets Beat Saber `1.40.8_7379`, ARM64 Quest, and C++20. Dependency versions are locked in `qpm.json`/`qpm.shared.json`; native code generated for another Beat Saber build must not be presented as compatible without a separate build and headset test. This port remains alpha until it has completed on-headset regression testing against that exact APK.

## Tools

The concise checklist is below. The authoritative version/source/cache table
is [Build dependencies and network downloads](DEPENDENCIES.md).

### Required for a clean Windows build

- **Windows 10 version 2004/build 19041 or newer, or Windows 11**, on x86-64.
- **Windows PowerShell 5.1**, included with supported Windows releases, only
  for the Windows prerequisite audit and optional ADB deployment wrapper.
- **WSL 2 with x86-64 Ubuntu 22.04 or 24.04**. The root BAT audits and can
  install this after naming the exact UAC-elevated component and receiving
  approval. Manual setup is documented below.
- The Ubuntu host packages listed in the manual setup section. The BAT installs
  only the packages its read-only audit marked missing.
- Internet access for missing pinned project inputs and several gigabytes of
  free space for the first toolchain and dual-FFmpeg build.

Visual Studio, Git, PowerShell 7, Windows QPM, a Windows Android NDK, Docker,
and 7-Zip are not required by the supported Windows launcher. The complete
build runs inside WSL and uses the same Bash/Python implementation and pinned
Linux QPM/NDK path as native Linux and CI.

### Required for a clean Linux build

- An **x86-64 Linux** host. Ubuntu 24.04, Debian 13, and Linux Mint 22.x
  build natively; immutable or unsupported hosts use the launcher's managed
  Ubuntu 24.04 Distrobox.
- `build-essential`, CMake 3.22+, Ninja, curl, XZ/unzip support, Python 3,
  pkg-config, and the host FFmpeg command/development libraries used by the
  real decoder tests.
- Internet access and several gigabytes of free space for the first clean
  toolchain and dual-FFmpeg build.

Run `bash ./Build-QMOD-Linux.sh` for the supported QMOD-only path, or execute
`./Build-And-Deploy-Linux.sh` to perform that complete build and then directly
install an ownership-tracked development copy on an authorized Quest. The root
launchers audit native prerequisites and automatically select a reusable Ubuntu
Distrobox on immutable or unsupported hosts. They list exact missing packages
and request approval before any host package manager or `sudo` use. Pinned QPM
and NDK inputs remain in user caches. See
  [Building Big Screen on Linux](BUILDING-LINUX.md) for the complete package,
  portable-tool, Bazzite/Distrobox, cache, and troubleshooting instructions.

Linux ARM64 is not a native supported host because the locked NDK r27d and QPM
1.5.11 Linux host tools are x86-64. The resulting mod remains Quest ARM64; host
and target architectures are separate concerns.

### Optional tools

- **Android platform-tools/ADB or SideQuest** for direct deployment, logging,
  and tombstone helpers. Neither is required to compile or create a QMOD.
- **Node.js 24 and pnpm 11.16.0** only for independently rebuilding the pinned
  yt-dlp/yt-dlp-ejs payload from source. A normal build uses the verified
  official payload and does not require Node.js.
- **Unity Editor 2022.3.33f1** only when rebuilding the experimental embedded
  video shader after changing files in its tracked project. Ordinary clean
  `Build-QMOD-Linux.sh` runs embed the committed
  `assets/bigscreen_video_shader` and do not launch Unity. The supported
  Windows and Linux root launchers also deploy that verified build without
  invoking Unity. Only the advanced manual `scripts/copy.ps1` path can request
  a shader rebuild when it is asked to build after a tracked shader input has
  changed. The repository includes the authored `Packages`, `ProjectSettings`,
  XR configuration, shader, and editor build script needed for reproduction;
  only Unity's generated caches remain untracked.

### One-time Windows/WSL setup

Both root Windows BAT entrypoints first perform a read-only audit. Every
prerequisite and cached project input is printed as `READY` or `MISSING` before
the consent prompt. If elevation is required, the audit names **Windows
Subsystem for Linux** and/or **Ubuntu 24.04 for WSL** as the exact component
that will open a Windows UAC prompt. Linux packages installed inside WSL and
repository-local downloads are explicitly labelled as not requiring Windows
UAC.

`Build-QMOD.bat` selects the QMOD-only workflow immediately; the combined
`Build-And-Deploy.bat` asks whether to build `Big Screen.qmod` only or also
deploy it directly. QMOD-only mode omits ADB from the prerequisite list, does
not access a Quest, and leaves the completed package for MBF or another
compatible QMOD installer such as SideQuest. Direct-deployment mode includes
ADB and preserves the existing device, shared-dependency, QMOD-registration,
mixed-ownership,
and source-install receipt safeguards; it refuses to overlap Big Screen when a
standard package record from MBF, SideQuest, or another QMOD manager exists.

For unattended QMOD-only validation, `Build-QMOD.bat --yes` approves only the
prerequisites disclosed by that launcher's read-only audit. It does not enable
deployment, relax validation, start ADB, or access a Quest. This mirrors the
Linux `Build-QMOD-Linux.sh --yes` test path while preserving the normal
double-click confirmation workflow.

After approval, only missing operating-system prerequisites are installed.
Project-specific tools remain handled by their existing pinned download and
SHA-256 validation scripts. A newly enabled WSL installation may require one
Windows restart, after which the same BAT can be run again.

For manual setup, use Microsoft's supported WSL installer from an Administrator
PowerShell window:

```powershell
wsl --install -d Ubuntu-24.04
```

- Official source: [Install WSL](https://learn.microsoft.com/windows/wsl/install)
- If Windows requests a restart, complete it before continuing.

Inside Ubuntu, install the host compiler, build utilities, tests, and FFmpeg
development libraries:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential ca-certificates cmake curl ffmpeg \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  ninja-build pkg-config python3 unzip xz-utils
```

The normal build downloads its own pinned Linux QPM 1.5.11 and Android NDK
r27d rather than requiring system-wide installations. Windows/WSL and native
Linux execute the same `scripts/build_pipeline.py` implementation; the Windows
BAT only audits/prepares and enters WSL. Their official URLs, cache locations,
and committed integrity checks are listed in
[Build dependencies and network downloads](DEPENDENCIES.md).

## Fresh build

```bash
git clone https://github.com/Loud160/BigScreen.git
cd BigScreen
bash ./Build-QMOD-Linux.sh --clean
```

From Windows, the build-only counterpart is:

```bat
Build-QMOD.bat
```

The combined build/direct-deployment launcher is:

```bat
Build-And-Deploy.bat
```

### Reproducible Windows and Linux packages

With the same commit and pinned dependency inputs, clean Windows and native
Linux builds produce a byte-identical `Big Screen.qmod` with the same SHA-256.
The build normalizes embedded source paths, omits the host-sensitive linker
build ID, sorts source and archive entries, fixes ZIP metadata, serializes JSON
consistently, and regenerates QPM's host-specific links when the checkout moves
between operating systems.

The deterministic archive uses standard ZIP DEFLATE from the same tracked
miniz 3.1.2 source on both hosts. A small host utility fixes ordering and ZIP
metadata in one canonical form. This keeps the QMOD in its normal 19–20 MB
range and fully compatible with MBF while avoiding Windows/Linux compressor
drift. It is compiled with the C/CMake toolchain already required for a source
build; users do not install or download a separate archive program. The test
suite verifies compression, ordering, timestamps, content, and duplicate-entry
rules.

## Portable repository boundary

A fresh clone contains the source, headers, tests, documentation, licenses,
QPM manifests, CI configuration, and the versioned build/fetch recipes needed
to reproduce Big Screen. It intentionally does not contain developer-specific
editor settings, CMake caches, NDK installations, restored QPM dependencies,
downloaded CPython/yt-dlp/QuickJS-NG sources, compiled FFmpeg libraries, QMOD
packages, headset captures, crash-analysis extracts, or temporary AI working
files outside the documented archive described below.

Retained AI-assisted engineering records are intentionally versioned under
`docs/ai-assisted-development`. Temporary prompts, local review exports, and
other AI working files outside that documented archive remain excluded.

Those inputs and outputs are generated beneath ignored cache, `extern`,
`build*`, and `artifacts` paths. The dependency scripts retrieve the required
upstream sources and binaries from pinned URLs and reject content that does not
match the committed SHA-256 values. Consequently, copying a local generated
library into Git is neither required nor a substitute for a reproducible build.

The root BAT is intentionally transparent about this boundary. It lists the
possible downloads and waits for approval, while each fetch script reports
whether it reused a cache or downloaded a named artifact from a named source.
See [DEPENDENCIES.md](DEPENDENCIES.md) for the exact inventory and commands to
perform every step without the BAT.

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

`scripts/build_pipeline.py` retrieves the official QuickJS-NG 0.16.1
amalgamated source when the verified cache is missing. CMake compiles it
directly into `libbigscreen.so` without
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

The script downloads the pinned stable yt-dlp 2026.08.19 and
yt-dlp-ejs 0.8.0 source
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

`scripts/build-linux.sh` stages both FFmpeg 4.4.8 and FFmpeg 9.0.1 by invoking `scripts/build-ffmpeg-lgpl.sh` for each pinned source release. `scripts/bootstrap-linux.sh` fetches and hash-checks Android NDK r27d when its verified cache is missing. Each build enables software H.264, VP8, and VP9 plus Android MediaCodec H.264, H.265/HEVC, VP8, and VP9; JNI/MediaCodec integration; MP4/MOV, Matroska/WebM, and MPEG-TS demuxing; the MP4 muxer; the local-file protocol; and `libswscale`. It explicitly omits encoders, GPL, version-3-only, and nonfree components. The build fails if configure silently drops any required decoder, demuxer, muxer, or JNI/MediaCodec support. Matroska support is required because the 1440p downloader deliberately stores VP9 in its native WebM container. MPEG-TS demuxing and MP4 muxing let the background downloader normalize an HLS payload by copying its existing H.264 packets into a seek-safe MP4 without re-encoding.

A clean native build can take several minutes. Ninja's final progress item
combines the main `libbigscreen.so` link, ThinLTO optimization, debug-symbol
handling, stripping, and runtime-library staging. The progress counter may stay
on that final line without changing while the linker is still working. The
build script prints an explicit wait message before this step; do not close the
window unless the command reports an error or returns to the prompt. While the
native build remains active, an elapsed-time heartbeat is printed every 15
seconds. The linker does not expose a meaningful internal percentage, so this
heartbeat intentionally reports activity and elapsed time instead of inventing
an inaccurate progress value.

The FFmpeg build suppresses compiler warnings originating in its pinned
third-party C sources. FFmpeg 4 predates the current Android NDK by several
years and otherwise emits a large set of diagnostics for valid legacy code,
including deprecated internal APIs, qualifier differences, macro-generated
locals, and constant conversions. Configure failures, missing-feature checks,
compiler errors, link failures, hash verification, and ELF validation all
remain active. The `-w` flag applies only while compiling FFmpeg; Big Screen
continues to compile with its strict `-Wall`, `-Wextra`, and `-Wpedantic`
policy. The staged configuration records the flag, and `scripts/build-linux.sh`
automatically rebuilds older cached runtimes that do not contain it.

The outputs use separate `-bigscreen44` / `-bigscreen9` SONAME suffixes and `BIGSCREEN44_LIB*` / `BIGSCREEN9_LIB*` symbol-version namespaces. The matching decoder implementation is also linked as `libbigscreen-ffmpeg44-backend.so` or `libbigscreen-ffmpeg9-backend.so`. This separate-backend boundary matters: putting two ordinary FFmpeg call sites directly in one shared object would let both bind to the first runtime despite matching headers. Each backend instead records hard versioned-symbol requirements for exactly one runtime, while an FFmpeg-type-free facade chooses the backend and moves the existing reusable RGBA or Y/U/V frame buffers without an extra frame copy.

For LGPL corresponding-source redistribution, both unmodified archives
identified in the packaged `FFMPEG-4.4.8-BUILD-INFO.txt` and
`FFMPEG-9.0.1-BUILD-INFO.txt` records are maintained in the
[permanent FFmpeg corresponding-source release](https://github.com/Loud160/BigScreen/releases/tag/ffmpeg-sources-4.4.8-9.0.1).
Every public QMOD release must link directly to that source release while it
distributes these exact FFmpeg builds. The QMOD includes both exact build
records and generated source diffs. Both upstream archive SHA-256 values and
all transformations are recorded in `scripts/build-ffmpeg-lgpl.sh`.

To make a clean dual-runtime build and retain a clearly named comparison QMOD,
native library, and both reproducibility records, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/build-ffmpeg-comparison.ps1
```

Install that single QMOD, then use **Misc > Performance > Use FFmpeg 9** to
switch between the bundled runtimes and **Hardware Video Decoding** to switch
between supported software decoders and MediaCodec. Both switches default on.
**GPU Video Conversion** defaults on and compares reusable 8-bit Y/U/V plane
uploads plus one GPU conversion pass with the normal swscale/RGBA fallback. An
active Video Library preview is recreated at its retained song position, while
gameplay uses the selection when the next map opens. Compare the same map,
screen resolution, FPS cap, headset charge/thermal state, and playback
interval. The performance overlay and results summary identify the runtime
that actually opened the backend and report presented-frame loss, true
session-average/peak frame-preparation CPU time, automatic reductions,
presentation method/time, and reusable frame-buffer allocation count. The
append-only performance log separately records worker wait time so asynchronous
MediaCodec waits and thread descheduling are not misreported as decode cost.
FFmpeg 9.0.1 is the current default; FFmpeg 4.4.8 remains available for
compatibility comparisons.

## Host tests

```bash
bash ./scripts/test-linux.sh
```

On Windows, the historical `scripts/test.ps1` command is only a thin WSL
forwarder to that same Bash script; it contains no separate test recipe.

The test suite covers deterministic library keys/path rules, source-aware frame
expectations, quality fallback, error-circuit timing, URL allowlisting, Cinema
metadata parsing, Chroma detection, embedded Python downloader behavior, and
cross-file toolchain/licensing/settings invariants. Linux CI additionally
generates landscape and portrait H.264 fixtures and exercises the real FFmpeg
worker, timestamps, scaling, shutdown, reusable RGBA/YUV plane buffers, normalized YUV420
plane transport, and live fallback from YUV transport to RGBA. Host tests do
not replace an ARM64 Quest build or VR regression testing.

### First-party logger

Every build uses Big Screen's private asynchronous logger. Paper2 can still be
restored transitively because beatsaber-hook and other shared dependencies use
it, but `libbigscreen.so` does not link to Paper2 and the generated QMOD does
not declare it as a Big Screen dependency. The canonical ELF validation rejects
a build that regains a Paper2 `DT_NEEDED` entry or exposes the private
beatsaber-hook abort bridge.

## QMOD contents

`scripts/build_pipeline.py` regenerates `mod.json`, validates required runtime
files, creates a fresh archive without update-mode residue, verifies that its
unique entries exactly match the manifest, and only then atomically replaces
an older QMOD. Its tracked miniz-based deterministic ZIP writer produces the
same archive bytes through WSL and native Linux. It packages:

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
The canonical `scripts/build_pipeline.py` ELF audit runs after every normal
build and again before packaging. It verifies both backend dependencies,
rejects cross-version FFmpeg libraries/symbols, and requires each backend's
exported factory. `scripts/validate-ffmpeg-elf.ps1` remains available as a
Windows-facing diagnostic wrapper for the same release boundary.

The generated `mod.json` must report the same version as `qpm.json`, `qpm.shared.json`, and `mod.template.json`, the exact package version, all required libraries, and every runtime file copy.

## Source deployment ownership and QMOD managers

`scripts/copy.ps1` writes a planned
`BigScreen/SourceInstall/source-install.partial.json` before changing any
deployment destination. Every copied file is SHA-256 verified and marked
complete before that receipt is atomically promoted to `source-install.json`.
Later source deploys preserve the original pre-development baseline rather
than treating the previous source build as user-owned content.

Do not source-deploy Big Screen over a registered QMOD package. The script
searches the active version's package manifests by `id: bigscreen` and refuses
both installed and dormant registrations without modifying Quest files. Remove
Big Screen through MBF, SideQuest, or the QMOD manager that registered it first.

To switch a source deployment back to QMOD-manager ownership, run
`Remove-BigScreen.bat` on Windows or
`./Remove-BigScreen-Linux.sh` on Linux. It force-stops Beat Saber,
uses the receipt to identify and remove exact Big Screen-exclusive files even
when their hashes changed, never removes shared dependencies merely because Big
Screen used them, and preserves thumbnails, library data, logs, maps, and
choreography. Settings and Big Screen-managed downloaded videos each have
a separate confirmation that defaults to No. Pre-receipt installs use a
one-time guided cleanup limited to exact Big Screen-exclusive paths; ambiguous
files are preserved and reported.

## Deployment to a test headset

The normal Windows entrypoint is `Build-And-Deploy.bat`; select **Deploy** when
prompted. On native x86-64 Linux, run:

```bash
./Build-And-Deploy-Linux.sh
```

Both perform the canonical build and package validation before any Quest files
are changed. Direct-deployment mode also checks for an authorized Quest before
the long build begins. If USB debugging has not been accepted, the launcher
shows the headset authorization steps and offers **Retry** or **Cancel** without
discarding prepared dependencies. An advanced Windows developer who has already completed that
verified build can deploy it without rebuilding:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/ensure-adb.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/copy.ps1 -UseExistingVerifiedBuild
```

The deployment selector ignores authorized Android phones and tablets. It
verifies that a target identifies as a Meta/Oculus Quest and has Beat Saber
installed before copying anything. If more than one matching Quest is
connected, the script lists each headset by model and serial number and asks
which numbered device should receive the build. Automated/noninteractive use
fails instead of guessing when multiple Quests match. The Windows and Linux
removal and support-log launchers use the same target policy, so none can
accidentally operate on an attached phone; all offer the same numbered choice
when multiple Quests are present.

On Windows, [Build-And-Deploy.bat](../Build-And-Deploy.bat) audits and prepares
WSL, runs the same Linux build used by native Linux and CI, then hands the
verified output to `scripts/copy.ps1` for Windows ADB dependency checks,
ownership-safe copying, and restart. On native x86-64 Linux,
[Build-And-Deploy-Linux.sh](../Build-And-Deploy-Linux.sh) runs that same build
and then uses `scripts/quest_tool.py` for the equivalent dependency,
Quest-selection, ownership-receipt, copy-verification, and restart policy.
Neither launcher maintains a second compilation or packaging recipe.

Deployment scripts are development conveniences, not part of ordinary end-user installation. Confirm the active Beat Saber package/version before replacing a mod binary.

When the controller launch gate prevents unattended Beat Saber startup, compile `tests/android_ffmpeg_smoke.c` twice with the same Android NDK: once against the four `*-bigscreen44.so` files and once against the four `*-bigscreen9.so` files. Running each binary through ADB against the same real H.264 MP4 verifies Android dynamic loading, private symbol versions, demuxing, native H.264 decoding, and RGBA scaling without bypassing the Quest safety screen. The complete mod build and ELF audit are still required to verify the two backend libraries' exact `DT_NEEDED` and symbol-version bindings.

## CI

`core-tests.yml` runs the canonical Linux host tests, verifies public
documentation files, and uses pinned Node/pnpm tooling to rebuild yt-dlp plus
yt-dlp-ejs from source and compare the full payload with the shipped release.
`build-ndk.yml` invokes `Build-QMOD-Linux.sh`, which restores the pinned QPM and
NDK inputs, creates the validated deterministic QMOD, and uploads artifacts.
Third-party actions are pinned to commit SHAs. Changing the NDK requires
synchronized QPM metadata, CI, FFmpeg, one clean dual-runtime build, and
headset regression testing.

## Threading rules

Unity and Beat Saber objects belong to the main thread. FFmpeg decoding and CPython/yt-dlp work run on dedicated native workers and communicate through synchronized mailboxes or atomic JSON status files. New code must not call Unity APIs from those workers. Long filesystem/network/decode work must not be added to a per-frame UI callback.

## Error behavior

Expected content/network failures report a readable error but do not trip the internal safety circuit. Internal exceptions are guarded at lifecycle boundaries. During gameplay, dialogs are deferred until the map ends. A second internal error within three minutes disables Big Screen to stop repeated failures while leaving its menu available.
