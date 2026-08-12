# Third-party notices

Big Screen's original source is MIT licensed. The packaged mod and build process
also use independent third-party components under their own terms:

- **CPython 3.14.6 Android ARM64** — Python Software Foundation License. The
  QMOD includes `CPYTHON-LICENSE.txt` beside the runtime.
- **yt-dlp** — Unlicense/public-domain dedication. Big Screen ships a pinned
  baseline and can retrieve official upstream stable or nightly packages.
- **certifi** — Mozilla Public License 2.0. Its CA bundle is installed as a
  physical package for Python/OpenSSL certificate validation.
- **FFmpeg libraries** (`avformat`, `avcodec`, `avutil`, `swscale`) — used under
  the license terms of the packaged Quest FFmpeg build. Consult that build's
  configuration and license files when redistributing binaries; optional FFmpeg
  configuration can change whether LGPL or GPL terms apply.
- **beatsaber-hook, BSML, custom-types, SongCore, Paper, Scotland2, and generated
  Beat Saber CORDL headers** — separate Quest modding dependencies. Versions and
  resolved sources are recorded in `qpm.json` and `qpm.shared.json`.
- **Quest Mod Template** — Unlicense. It supplied the initial empty project
  scaffold; see `PROVENANCE.md`.

This notice is informational and does not replace the complete license text
distributed by each dependency.
