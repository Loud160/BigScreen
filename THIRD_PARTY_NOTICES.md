# Third-party notices

Big Screen's original source is MIT licensed. The packaged mod and build process
also use independent third-party components under their own terms:

- **CPython 3.14.6 Android ARM64** — Python Software Foundation License. The
  QMOD includes `CPYTHON-LICENSE.txt` beside the runtime.
- **yt-dlp** — Unlicense/public-domain dedication. Big Screen ships a pinned
  baseline and can retrieve official upstream stable or nightly packages.
- **certifi** — Mozilla Public License 2.0. Its CA bundle is installed as a
  physical package for Python/OpenSSL certificate validation.
- **FFmpeg libraries** (`avformat`, `avcodec`, `avutil`, `swscale`) — the
  packaged `ffmpeg-kit-speed` v4.4.1 ARM64 build identifies itself as "GPL
  version 3 or later" and contains `--enable-gpl --enable-version3` in its
  embedded configuration. The QMOD therefore includes
  `FFMPEG-GPL-3.0-OR-LATER.txt`; redistribution must honor those GPL terms.
- **beatsaber-hook, BSML, custom-types, SongCore, Paper, Scotland2, and generated
  Beat Saber CORDL headers** — separate Quest modding dependencies. Versions and
  resolved sources are recorded in `qpm.json` and `qpm.shared.json`.
- **Quest Mod Template** — Unlicense. It supplied the initial empty project
  scaffold; see `PROVENANCE.md`.

The QMOD installs this notice and the CPython, yt-dlp, certifi, and FFmpeg
license texts beside the embedded runtime under Big Screen's ModData folder.
This notice is informational and does not replace those complete terms.
