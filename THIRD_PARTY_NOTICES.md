# Third-party notices

Big Screen first-party source is distributed under GPL-3.0-only with
additional GPLv3 section 7(b)/(c) terms and a section 7 interoperability
permission. See `LICENSE`, `LICENSE-ADDITIONAL-TERMS.md`, and `NOTICE`.

The packaged mod and build process also use independent third-party components
under their own terms. None of those components is relicensed by Big Screen.

## Runtime components included in the QMOD

- **CPython 3.14.7 Android ARM64** — Python Software Foundation License and
  the other notices reproduced in CPython's `LICENSE.txt`. The official Python
  Android package supplies `libpython3.14.so`, the standard library, extension
  modules, OpenSSL, and SQLite used by the embedded downloader. The QMOD
  includes that complete `CPYTHON-LICENSE.txt`.
- **OpenSSL 3.5.7** — Apache License 2.0. The exact version was verified from
  the official CPython Android package's `libcrypto_python.so`; OpenSSL 3.0 and
  later use Apache-2.0. The QMOD includes `OPENSSL-APACHE-2.0.txt`.
- **SQLite 3.50.4** — public domain. The exact version was verified from the
  official CPython Android package's `libsqlite3_python.so`. The QMOD includes
  `SQLITE-PUBLIC-DOMAIN.txt` with the authoritative SQLite source reference.
- **yt-dlp** — Unlicense/public-domain dedication. Big Screen ships a pinned
  baseline and can retrieve official upstream stable or nightly packages.
- **yt-dlp-ejs 0.8.0** — Unlicense/public-domain dedication, with generated
  solver bundles containing MIT-licensed astring and ISC-licensed meriyah.
  The matching package is bundled inside the official yt-dlp runtime. A
  tracked source-build recipe rebuilds it through its upstream lockfile.
- **QuickJS-NG 0.16.1** — MIT License. Big Screen compiles the engine into
  `libbigscreen.so` and uses it only for yt-dlp's official JavaScript challenge
  scripts. It is not installed or launched as a separate executable.
- **certifi 2026.7.22** — Mozilla Public License 2.0. Its CA bundle is installed
  as a physical package for Python/OpenSSL certificate validation.
- **FFmpeg 4.4.8 and 9.0.1 libraries** (`avformat`, `avcodec`, `avutil`, and
  `swscale`) — GNU Lesser General Public License 2.1 or later. Big Screen
  dynamically links two private Android ARM64 runtimes. Their checked configure
  records exclude `--enable-gpl`, `--enable-version3`, and `--enable-nonfree`.
  The builds contain only the documented software/MediaCodec decoders,
  MP4/MOV, Matroska/WebM, and MPEG-TS demuxing, local-file/JNI integration,
  and frame scaling.
  The QMOD includes the LGPL text, exact build configuration, and source-change
  records for both versions. The matching unmodified source archives are
  permanently available from the
  [Big Screen FFmpeg corresponding-source release](https://github.com/Loud160/BigScreen/releases/tag/ffmpeg-sources-4.4.8-9.0.1).
- **beatsaber-hook 6.4.2** — MIT License. The QMOD includes the resolved runtime
  library because Big Screen directly requires it.

## Build-time and separately distributed Quest dependencies

Versions and resolved sources are recorded in `qpm.json` and
`qpm.shared.json`.

- **Scotland2 0.1.7**, **SongCore 1.1.26**, and **custom-types 0.18.4** — MIT
  License in their respective upstream repositories.
- **fmt 11.0.2** — MIT License.
- **RapidJSON** — MIT License for the core library, with upstream third-party
  components retaining the notices in RapidJSON's own `license.txt`.
- **tinyxml2 10.0.0** — zlib License.
- **BSML 0.4.55**, **Paper2 Scotland2 4.8.0**, and generated **bs-cordl
  4008.0.0** game
  bindings — separately distributed Quest build/runtime interfaces. The
  resolved upstream repositories did not publish a root license file at the
  revisions inspected for this audit, so this notice deliberately makes no
  blanket license-compatibility claim for them. Big Screen's GPLv3 section 7
  interoperability permission permits Big Screen's side of the combination;
  it does not purport to grant rights in those external projects.
- **Beat Saber, Unity, Meta Quest/Android, and generated game API interfaces** —
  proprietary or platform material not licensed by Big Screen. They are not
  represented as Big Screen source. The section 7 interoperability permission
  is narrowly intended to allow the GPL-covered mod to operate with them.
- **Quest Mod Template** — Unlicense. It supplied the initial empty project
  scaffold; see `PROVENANCE.md`.

The QMOD installs this notice, Big Screen's license/additional terms/attribution,
and the CPython, OpenSSL, SQLite, yt-dlp, QuickJS-NG, certifi, and FFmpeg
license/build records beside the embedded runtime under Big Screen's ModData
folder. This summary is informational and does not replace those complete
terms.
