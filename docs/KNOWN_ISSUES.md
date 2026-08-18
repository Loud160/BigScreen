# Current development checkpoint

> **Serious development warning:** the current feature set has substantial
> unresolved quality concerns. It is a preservation checkpoint, not a release
> candidate. Everything must be retested on Quest before release.

Last reviewed: August 18, 2026

## Video screen and Bloom status

The current code contains two selectable visible video-material paths, a new
alpha-only guard for the Unity UI path, and a dedicated mono-safe material for
capturing the video into Cinema's bloom pre-pass. This arrangement is intended
to combine the two partial results observed on Quest: a visible embedded-shader
picture and the glow produced when the Unity material populated the pre-pass.
The current combined result has not yet been tested on the headset. The presence
of the AssetBundle, a successful native build, an XR/multiview shader variant,
or a logged shader tier proves only that the implementation is present and
selected. Visual behavior must be established by a fresh test matrix with Bloom
on and off and with both visible-material selections.

The August 18 port also corrects a callback-context difference from PC Cinema:
the Quest renderer explicitly loads the view matrix returned by Beat Saber's
bloom renderer before drawing the world-space screen. A successful pass writes
one diagnostic line naming the visible shader, capture shader, render-target
size, and calculated boost so a blank capture can be distinguished from a
material-selection problem in field logs.

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
