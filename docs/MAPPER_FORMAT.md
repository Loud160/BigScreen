# Mapper video metadata

Big Screen reads Cinema-compatible video metadata as an interoperability input.
A map may provide a local MP4/WebM filename or a downloadable video URL plus timing
and screen placement fields. User assignments take precedence without changing
the map, and removing a user assignment reveals the mapper's video again.

Local mapper video files may use 8-bit SDR H.264/H.265 in MP4 or VP8/VP9 in
WebM, up to the 1440p short-edge tier. H.265 and content above 1080p require
Hardware Video Decoding; HDR/10-bit and WebM alpha are rejected. Big Screen
rejects absolute paths and nested/traversal paths;
the configured filename must resolve directly inside the map directory.

Downloaded mapper videos are stored separately from user overrides so replacing
or removing one cannot destroy the other. Mapper files placed in the map folder
are user/map-owned and are never deleted by Big Screen.

## Cinema and Chroma presentation compatibility

When the player enables Allow Chroma Override, Big Screen recognizes Cinema's
`screenPosition`, `screenRotation`, `screenHeight`, `screenCurvature`,
`screenSubsurfaces`, `curveYAxis`, `transparency`, `environmentName`,
`disableDefaultModifications`, `forceEnvironmentModifications`, and
`environment` presentation fields. Cinema curvature is interpreted as circular
arc degrees, independently from Big Screen's signed user curve slider.

The gameplay video surface is named `CinemaScreen` and moved into the loaded
environment scene before Quest Chroma's delayed environment pass, allowing
compatible `CinemaScreen$` scene lookups to identify it. Installed Quest Chroma
remains responsible for `_environment` data inside the selected difficulty.
Big Screen detects Chroma requirements/suggestions and non-empty Chroma
environment arrays across the entire map folder. This separate detection also
applies when the active video is a user download, import, or map-folder MP4,
so adding a video never causes Big Screen to force an environment over Chroma.
Big Screen then applies Cinema's separate `environment` array as a final
one-time scene pass after the environment and video surface exist.
Environment and screen ownership are evaluated separately. Chroma/Cinema can
retain the intended environment without disabling the player's screen layout.
The authored screen canvas wins only when Chroma is detected and the video
metadata contains custom position, rotation, size, or curvature. The selected
layout still owns picture-only controls inside that canvas, including video
pan/zoom/rotation/tilt, stretch, opacity, and letterbox transparency. The legacy
Cinema `transparency` value is parsed for compatibility but does not silently
override those visible player settings.
Exact-name selection, optional parent-name selection,
active state, world position/rotation, local scale, and object cloning are
supported. Unsupported PC-only shaders or asset bundles are ignored by this
interoperability layer rather than making the map unplayable.
