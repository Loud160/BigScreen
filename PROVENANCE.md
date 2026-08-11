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

The template is distributed under the Unlicense. Big Screen's original source
is distributed under the MIT License in this repository.

## Runtime dependencies

Dependency versions and download locations are recorded in `qpm.json` and its
generated lock file. Each dependency remains subject to its own license. Big
Screen does not claim authorship of those external libraries or generated Beat
Saber headers.

## Compatibility inputs

Map metadata formats are treated as interoperability interfaces. Parsers in
this repository are written from the field names and values present in test
map configuration files, not from another mod's parser implementation.
