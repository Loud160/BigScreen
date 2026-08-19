# Mapper video metadata

Big Screen reads a documented subset of the PC Cinema JSON format as an
interoperability input. A map may provide a local MP4/WebM filename or a
supported download URL plus timing, screen, image-effect, and environment
fields. A player assignment takes precedence without changing the map;
unlinking it reveals the mapper's video. The new presentation implementation
still requires its complete Quest regression pass; see
[Current development checkpoint](KNOWN_ISSUES.md).

The recognized map-folder filenames, in priority order, are `bigscreen.json`,
`cinema-video.json`, and `video.json`. Big Screen also reads a `cinema` object
stored under a playlist song's `customData`/`_customData`. Playlist metadata is
indexed once per game launch from the standard PlaylistManager, PlaylistCore,
and `/sdcard/Playlists` locations. The index is built once per game session and
is never polled in the background. After copying an edited map or playlist file
to the Quest, select the song and press **Refresh** in the Video Library.

## Media and timing

The following Cinema fields are supported:

- `videoID`, or an HTTPS YouTube `videoUrl`;
- `videoFile`, `title`, `author`, `duration`, and `configByMapper`;
- millisecond `offset`;
- `playbackSpeed`, `loop`, and `endVideoAt` in seconds.

`endVideoAt` fades the picture over the preceding second. Looping follows the
decoded media duration. Beat Saber's song clock remains authoritative, so
pause, practice speed, seek, and Replay retain normal synchronization.

Local mapper files may use 8-bit SDR H.264/H.265 in MP4/MOV or VP8/VP9 in
WebM/Matroska, up to the 1440p short-edge tier. H.265 and content above 1080p
require Hardware Video Decoding; HDR, 10-bit video, and WebM alpha are rejected.
`videoFile` must resolve inside the map folder—absolute paths and traversal are
rejected. Downloads live in Big Screen's managed video directory; files shipped
inside a map remain map-owned and are never deleted by Big Screen.

## Respect Mapper Settings

**Screen > Respect Mapper Settings** defaults on. When enabled, authored Cinema
screen geometry, image effects, additional screens, environment selection, and
environment modifications are applied. When disabled, media identity and
timing remain intact, but Big Screen uses the player's selected layout and
visual settings instead.

This is separate from **Allow Chroma Override**. Respect Mapper Settings owns
Cinema's video presentation. Allow Chroma Override owns the broader Chroma
environment only when map-wide detection confirms actual Chroma use. A
Cinema-only map can therefore use mapper screen placement without being called
a Chroma map, while a Chroma map with an ordinary video can retain its scene
without taking over the player's screen canvas.

## Screen geometry and image effects

Big Screen supports `screenPosition`, `screenRotation`, `screenHeight`,
`screenCurvature`, `screenSubsurfaces`, and `curveYAxis`. Cinema curvature is a
0–180 degree circular arc, independent from Big Screen's signed user curve. The
main root is named `CinemaScreen` and belongs to the loaded environment scene so
existing Chroma `CinemaScreen$` lookups can find it.

`additionalScreens` supports optional `position`, `rotation`, and `scale` for
each clone. Clones use PC-compatible names (`CinemaScreen (0)`, and so on),
share the primary decoded texture and effects, and do not create another decoder
or texture upload. One config is limited to 32 additional screens as a Quest
geometry/memory safety boundary.

The following picture fields are supported:

- `colorCorrection`: `brightness`, `contrast`, `saturation`, `exposure`,
  `gamma`, and `hue`, with Cinema's documented ranges;
- `vignette`: rectangular or elliptical `type`, `radius`, and `softness`;
- `colorBlending`: parsed for compatibility but currently ignored. The
  experimental Cinema soft-additive path is preserved behind the default-off
  `BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM` build gate so a map cannot
  unexpectedly make the picture emissive or see-through;
- `transparency`, which controls Cinema's opaque light-blocking body behind the
  picture. It does not change picture opacity or the player's letterbox option.

PC Cinema's `bloom` field is parsed and retained in the normalized map data but
currently has no runtime effect. Big Screen's experimental Cinema-style glow
renderer is preserved behind that named build gate rather than deleted. Both
visible-material paths instead suppress the video's bloom-emission weight so
bloom-heavy map lighting cannot wash the picture into a solid white rectangle.
Unknown fields are ignored rather than treated as fatal configuration errors.

Color correction and vignette run on the decoder worker after FFmpeg color
conversion and container orientation. Vignette pixels outside the authored
shape have both RGB and alpha cleared, and the independent rectangular backing
is removed so an ellipse or rounded rectangle does not retain a black box.
An all-default correction object uses the normal fast path. Additional screens
reuse the one uploaded video texture. Opaque-body behavior and authored alpha
ultimately depend on the active video material and must be
verified with both selectable material paths during the Quest regression pass.

## Environment behavior

The parser and current gameplay implementation recognize `environmentName`, `disableDefaultModifications`,
`forceEnvironmentModifications`, `mergePropGroups`, and the `environment` array
are recognized. The array supports exact `name`, optional exact `parentName`,
`cloneFrom`, `active`, world `position`/`rotation`, and local `scale`. Cloned
lights are explicitly registered with Beat Saber's active light manager. Unless
`mergePropGroups` is true, clones are temporarily displaced before Chroma's
delayed prop-group pass and restored when Cinema's final mapper pass runs.

The implementation is intended to let `forceEnvironmentModifications` work without a video declaration or download.
Big Screen then creates no decoder or screen and applies only the mapper's scene
changes. Big Screen has no hidden table of PC Cinema's historical default scene
edits, so `disableDefaultModifications` is naturally satisfied: only explicit
mapper entries are applied.

`allowCustomPlatform` is parsed and exposed through the optional native C query
`bigscreen_cinema_allows_custom_platform`. PC Cinema sends this value to a
separate CustomPlatforms mod. Quest currently has no compatible platform-mod
API that Big Screen can call directly; a future Quest platform mod must consume
the query for this one inter-mod preference to take effect. No consumer means
the field is inert, never a video or map failure.

## Manual refresh and safety

Quest builds deliberately do not poll Cinema JSON or playlist files. To apply
an edited file, copy the completed file back to the Quest, open that song in the
Video Library, and press **Refresh**. This rebuilds the playlist index, forgets
the selected song's cached mapper definition, and recreates its menu preview.
Restart the map before checking gameplay changes, especially when adding or
removing cloned objects or changing `environmentName`, because Beat Saber must
load the gameplay environment as a scene.

Parser limits are deliberate Quest protections: 1 MB per config, 256
environment entries, 32 additional screens, and bounded strings. Unknown JSON
fields are ignored, so newer metadata remains playable. `bundledConfig` and
`userSettings` are PC Cinema bookkeeping/UI state rather than mapper
presentation controls and have no Quest runtime effect. Environment cloning,
requested environments, additional screens, and Chroma cooperation still need
the full on-device matrix in `KNOWN_ISSUES.md` before being called PC-equivalent.
