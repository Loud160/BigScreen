# Current development checkpoint

> **Serious development warning:** the current feature set has substantial
> unresolved quality concerns. It is a preservation checkpoint, not a release
> candidate. Everything must be retested on Quest before release.

Last reviewed: August 18, 2026

## Video screen and Bloom status

The two selectable visible video-material paths remain available. The August
18 Cinema bloom renderer, camera hook, soft-additive map path, and two diagnostic
bloom sliders are preserved behind the default-off
`BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM` build gate and do not
run or appear in the menu. Mapper `bloom` and `colorBlending` fields are parsed
but intentionally ignored. Both active material paths instead suppress the
video's bloom-emission weight. Explicit mapper transparency/vignette and the
player's opacity/letterbox settings remain supported.

The previous experiment produced white screens, state that changed only after
moving a slider, and broken showcase backing. Do not re-enable it without a
separate branch and a complete Bloom-on/Bloom-off headset matrix.

## Cinema compatibility status

The current code contains a new Cinema-compatibility parser and implementation
for mapper geometry, additional screens, color correction, vignette, and
environment instructions. This work compiles and has host-side parser coverage,
but the complete feature set has not passed a clean on-device regression pass.

In particular:

- the PC Cinema `bloom` and `colorBlending` fields are parsed but intentionally
  have no runtime effect while the failed glow experiment's named build gate
  remains off;
- opaque/transparency presentation depends on the selected video material and
  therefore belongs in the same on-device test matrix;
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
