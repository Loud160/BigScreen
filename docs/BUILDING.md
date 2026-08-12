# Building and packaging

## Supported target

The tracked package targets Beat Saber `1.37.0_9064817954`, ARM64 Quest, and C++20. Dependency versions are locked in `qpm.json`/`qpm.shared.json`; native code generated for another Beat Saber build must not be presented as compatible without a separate build and headset test.

## Tools

- Git
- Windows PowerShell 5.1 on Windows or PowerShell 7 (`pwsh`) on Linux
- CMake and Ninja
- QPM-RS and the Android NDK expected by the Quest modding toolchain
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

The dependency scripts download pinned Android CPython, certifi, yt-dlp, FFmpeg headers/libraries, and supporting artifacts. Each expected artifact has a fixed SHA-256 in the script; a mismatch stops the build. Do not “fix” a mismatch by changing only the hash—review the upstream release, ABI, contents, and license first.

## Host tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./scripts/test.ps1
```

The test suite covers deterministic library keys/path rules, quality fallback, error-circuit timing, URL allowlisting, Cinema metadata parsing, Chroma detection, and the embedded Python downloader scripts. Host tests do not replace an ARM64 Quest build or VR regression testing.

## QMOD contents

`scripts/createqmod.ps1` regenerates `mod.json`, validates required runtime files, and packages:

- `libbigscreen.so` and declared native dependencies;
- the CPython standard library and Android extension modules;
- the immutable shipped yt-dlp baseline and certifi CA bundle;
- runtime integrity manifest;
- Big Screen and third-party notices/license texts.

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

## CI

`core-tests.yml` runs host tests and verifies public documentation files. `build-ndk.yml` restores QPM dependencies, uses the Quest NDK build, creates a QMOD, and uploads artifacts. Third-party actions are pinned to commit SHAs. The local canary-NDK composite action fixes the release URL/version; changing it requires a clean package build.

## Threading rules

Unity and Beat Saber objects belong to the main thread. FFmpeg decoding and CPython/yt-dlp work run on dedicated native workers and communicate through synchronized mailboxes or atomic JSON status files. New code must not call Unity APIs from those workers. Long filesystem/network/decode work must not be added to a per-frame UI callback.

## Error behavior

Expected content/network failures report a readable error but do not trip the internal safety circuit. Internal exceptions are guarded at lifecycle boundaries. During gameplay, dialogs are deferred until the map ends. A second internal error within three minutes disables Big Screen to stop repeated failures while leaving its menu available.
