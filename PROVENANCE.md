# Big Screen provenance

Big Screen is an independent Quest-native Beat Saber mod. Its implementation
starts from an empty generated Quest mod project and is based on public game
headers, public modding APIs, documented media-library APIs, observable runtime
behavior, and map configuration files supplied for testing.

No source files, assets, build files, embedded data, class layouts, or commit
history were copied from earlier Beat Saber video-player mods into this
repository.

## Project scaffold

The initial build scaffold was generated on 2026-08-11 from Lauriethefish's
public-domain Quest Mod Template at commit
`98cf232af91035c3613e235773ab081af4bd44d9`:

<https://github.com/Lauriethefish/quest-mod-template>

The template is distributed under the Unlicense. That permissive/public-domain
origin remains documented here and is not represented as having originated
with Big Screen. Big Screen's first-party source and Big Screen's distribution
of the combined project are licensed under GPL-3.0-only with the additional
terms and permission described in `LICENSE-ADDITIONAL-TERMS.md`.

## Runtime dependencies

Dependency versions and download locations are recorded in `qpm.json` and its
generated lock file. Each dependency remains subject to its own license. Big
Screen does not claim authorship of those external libraries or generated Beat
Saber headers.

The downloader additionally compiles the official QuickJS-NG 0.16.1
amalgamated source into Big Screen under its MIT license. The exact release URL
and SHA-256 are pinned in `scripts/fetch-quickjs-ng.ps1`; none of that upstream
engine source is represented as original Big Screen code.

The shipped yt-dlp nightly 2026.08.18.122307 zipimport runtime includes
yt-dlp-ejs 0.8.0. This pinned upstream nightly temporarily replaces stable
2026.07.04 because upstream's Android-VR client regression made public media
downloads fail with HTTP 403.
`scripts/build-downloader-from-source.ps1` and
`scripts/build_downloader_runtime.py` provide an independent, hash-pinned
rebuild from the corresponding upstream source archives. The process rebuilds
the JavaScript through the upstream package-manager lockfile and compares all
1,053 packaged files with the official release. These recipes do not claim
authorship of yt-dlp, yt-dlp-ejs, or their bundled astring/meriyah components.

## Compatibility inputs

Map metadata formats are treated as interoperability interfaces. Parsers in
this repository are written from the field names and values present in test
map configuration files, not from another mod's parser implementation.
