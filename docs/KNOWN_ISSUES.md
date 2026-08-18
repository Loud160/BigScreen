# Current development checkpoint

> **Serious development warning:** the current feature set has substantial
> unresolved quality concerns. It is a preservation checkpoint, not a release
> candidate. Everything must be retested on Quest before release.

Last reviewed: August 18, 2026

## Video screen and Bloom status

The current code contains two selectable video-material paths and recent fixes
intended to make them compatible with Quest stereo rendering and Beat Saber's
Bloom pipeline. This documentation audit did not independently run the current
build on the headset, so it does not claim that either path currently succeeds
or fails. The presence of the embedded shader AssetBundle, a successful native
build, an XR/multiview shader variant, or a logged shader tier proves only that
the implementation is present and selected. Visual behavior must be established
by a fresh test matrix with Bloom on and off.

## Cinema compatibility status

The current code contains a new Cinema-compatibility parser and implementation
for mapper geometry, additional screens, color correction, vignette, and
environment instructions. This work compiles and has host-side parser coverage,
but the complete feature set has not passed a clean on-device regression pass.

In particular:

- the PC Cinema `bloom` field is now parsed and drives the Cinema-style
  frame-glow pre-pass (CinemaBloomRenderer); its visual result still needs
  the on-device Bloom matrix below;
- `colorBlending` and opaque/transparency presentation depend on the selected
  video material and therefore belong in the same on-device test matrix;
- environment cloning, requested environments, additional screens, Chroma
  cooperation, and the Respect Mapper Settings switch all require wider map
  testing before their behavior can be called compatible with PC Cinema.

Unknown mapper fields are ignored rather than treated as fatal errors, so the
absence of a particular Cinema feature should not by itself prevent ordinary
media/timing metadata from loading.

## Required retest scope

At minimum, retest all of the following before this checkpoint is advanced:

- Video Library preview and gameplay with Bloom on and off;
- the normal and embedded material selections;
- flat, curved, undocked, mapper-authored, and additional screens;
- opacity, letterboxing, lead-in, vignette, color correction, and
  `colorBlending`;
- OST, DLC, custom, WIP, campaign, Showcase, Chroma/Noodle, and Replay paths;
- repeated menu entry, map restart, map exit/failure, and decoder fallback;
- both FFmpeg runtimes and hardware/software decoding where supported;
- YouTube download, local assignment, refresh, unlink/delete, storage, reset,
  error recovery, and settings migration.

Host tests and a successful Quest build remain necessary, but they cannot
replace the visual and lifecycle checks above.
