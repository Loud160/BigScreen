# Big Screen — Screen Movement Authoring: Planning Document

**What this is:** a pre-design working document for the two candidate authoring
tools for mapper-created screen choreography (the "movement file"): an
**in-mod VR authoring mode** and a **ChroMapper plugin**. It captures the
decisions that must be made, the systems each tool would touch, what already
exists in the codebase to build on, and the open questions — so a future
design conversation can start from here instead of re-establishing context.
This is NOT an implementation prompt; each section's decision lists are the
agenda for the conversation that produces one.

**Standing context (as of this writing):** the mod plays synchronized video on
one or more world-space screens; the showcase demo achieves choreographed
multi-screen movement through a hardcoded procedural timeline
(`UpDownShowcaseTimeline`, driven by song time, patterns not keyframes);
multi-surface management exists (`ShowcaseSurfaceGroup`); screens support
position/rotation/scale/curvature/opacity, with UV deformation (wave/flag),
glass-shatter, and shader effect systems specced in existing Codex prompts;
map-side config lives in `cinema-video.json` conventions parsed by
`MapVideoConfig`; the library preview can scrub audio+video together; an
undocked screen-placement editor with grab controls exists in `ScreenPreview`;
center-screen tool pages (file browser, thumbnail picker) establish the
pattern for new in-mod tools.

---

## Part 0 — The shared foundation: the movement file format

Both tools produce the same artifact, so the format must be finalized first.
It is the API; the tools are just two editors for it. Decisions needed:

### 0.1 Core model
- **Two layers, keyframes + patterns.** Keyframe tracks (Noodle-style point
  definitions `[value..., time, easing]`) for deliberate movement;
  parameterized effects/patterns (orbit, wave, bounce, shatter, glitch,
  reveal/mask, emissive) for dense procedural motion. Confirm this split and
  decide whether patterns can be layered on top of an active keyframe track
  (additive) or are exclusive per property per time range.
- **Time base:** beats (mapper-native) vs seconds (mod-native). Leaning
  beats-in-file, converted at load. Must decide how BPM changes are handled —
  use the game's own beat→time conversion for the loaded map, or require the
  file to declare a fixed BPM and refuse maps with BPM changes in v1.
- **Property list per screen** (decide the exact v1 set): position (x,y,z),
  rotation (euler? quaternion? euler is mapper-friendly), scale (uniform or
  x/y), opacity, curvature, screen visibility, video UV zoom/offset?,
  color tint? Everything not in v1 should have a reserved name.
- **Coordinate space:** absolute world coordinates vs offsets relative to the
  user's own screen placement. This is the single biggest gameplay-feel
  decision — absolute gives the choreographer full control but stomps user
  placement preferences; relative preserves user placement but constrains
  choreography. A per-file flag ("placement": "absolute" | "relative") is an
  option. Related: what happens to the user's configured screen when a
  movement file is active — is the primary screen screen id "main"?
- **Clones/extra screens:** how many additional screens may a file spawn
  (perf budget on Quest 2 — each screen is a texture upload unless cloned
  from the same decoder); "clone" (same video, same decoder, zero extra
  decode cost) vs independent source (second decoder — probably refuse in
  v1); do clones inherit the main screen's aspect/letterbox settings.
- **Groups:** named groups so one track can drive N screens with per-member
  offsets (the showcase's core trick). Decide offset semantics (index-based
  phase offset, spatial offset, both).

### 0.2 Behavior and safety
- **Precedence chain:** mapper movement file vs user's saved placement vs
  Chroma environment overrides vs the mod's own showcase. Suggest: movement
  file wins during its map only, user placement restored after, and a user
  setting to disable mapper movement entirely (accessibility/comfort).
- **Comfort limits:** max angular velocity / screen size near player /
  strobe-adjacent opacity flicker — decide whether the mod clamps, warns, or
  trusts the mapper. At minimum a global "reduce motion" user toggle.
- **Performance budget:** max simultaneous screens, max active effects,
  keyframe count limits, and what the mod does when a file exceeds them
  (refuse with error vs degrade).
- **Failure behavior:** invalid file → error shown where? (song select vs
  in-map toast vs log only). Validation must report beat numbers and JSON
  paths. Partial acceptance (skip bad track, play rest) vs all-or-nothing.
- **Versioning:** `"version": 1`, unknown-field policy (ignore), and the
  compatibility promise being made to mappers.

### 0.3 Packaging
- Filename and discovery: `bigscreen-movement.json` beside
  `cinema-video.json`? Referenced from cinema-video.json? Per-difficulty
  overrides or one file per map?
- Hot reload: file mtime watch vs explicit "Reload" control (decide where
  that control lives — it benefits BOTH tools and hand-editing).
- Documentation artifact: a public format spec page with copy-paste examples
  (also what makes LLM-assisted generation of these files practical).

---

## Part 1 — In-mod VR authoring mode

**Concept:** an authoring page inside Big Screen's menu (or a special mode
entered from it) where the mapper scrubs the song, physically places screens,
captures keyframes, applies effects to beat ranges, and writes the JSON into
the map folder. WYSIWYG in the true environment: the real renderer, the real
video, the real scale.

### 1.1 What already exists to build on
- **Placement editing:** `ScreenPreview`'s undocked editor — grab/move/rotate
  a world screen with controllers, with docked numeric controls as fallback.
  Needs generalizing from "edit the one saved placement" to "edit screen N's
  pose at the current time."
- **Timeline scrubbing:** the library preview scrubs audio+video together
  (`VideoLibraryMenu` scrubber + `PlaybackSession` external clock). An
  authoring mode needs the same scrub bound to the map's audio, plus
  play/pause and "play from here."
- **Multi-screen management:** `ShowcaseSurfaceGroup` already manages spawned
  surface sets sharing one decoder.
- **Tool-page pattern:** center-screen pages (file browser, thumbnail picker)
  show how to host a tool UI; the performance panel / floating controls show
  movable in-world panels.
- **Persistence and atomic writes:** manifest-style tmp+rename JSON writing
  is established; same pattern for the movement file, with rolling backups
  of the mapper's work file.

### 1.2 What must be designed (the discussion agenda)
- **Mode entry and scope:** where does authoring start — a button on the map
  in the Video Library? A separate "Choreography" page? Does it run in the
  menu environment with the map's audio, or actually inside the map
  (Practice-mode-like)? Menu-environment authoring is far simpler and reuses
  the preview clock; in-map authoring is truer (lighting, obstacles) but
  fights the game for input and UI. Recommend menu-first; decide if in-map
  *preview* (not editing) is needed for v1.
- **Timeline representation in VR:** a full keyframe timeline UI in VR is the
  hardest part. Options, cheapest first: (a) no visible timeline — just
  "keyframe list" text page + scrubber; (b) a horizontal beat-ruler strip
  with keyframe ticks per selected screen (seek by pointing); (c) full
  multi-track lane view. Decide the v1 bar — (b) is probably the sweet spot.
- **Keyframe capture loop:** scrub → grab screen → "Set Keyframe" (captures
  which properties? all, or only ones that changed?) → repeat. Decisions:
  per-property vs whole-pose keyframes; default easing applied on capture and
  how to change easing afterward in VR (cycle button per keyframe?); snapping
  (beat snap divisions like 1/1, 1/2, 1/4 — mappers will expect snap);
  nudge controls for fine position work (grab is coarse); copy/paste of
  keyframes and mirroring (left wing ↔ right wing).
- **Screen management UI:** add/remove clone screens, name them, assign to
  groups, select which screen is being edited (laser-click the screen itself?
  highlight selected screen with outline?).
- **Effects authoring:** effects are beat-ranges with parameters, not poses —
  they need a form-style panel (type dropdown, from/to beat, parameter
  sliders) plus live preview while scrubbing through the range. Decide
  whether effect parameters can themselves be keyframed in v1 (suggest no).
- **Preview fidelity:** during authoring, do effects render fully (shatter
  mid-edit)? Probably yes for ranges being scrubbed, with a "solo screen"
  toggle to reduce clutter.
- **Undo:** at minimum N-step undo for destructive actions (delete keyframe,
  delete screen). Decide depth and what's undoable.
- **Output and round-trip:** writes the shared JSON; MUST also *load* an
  existing file for editing — including one hand-written or LLM-generated
  with constructs the VR editor can't create (dense procedural data). Decide
  the preservation rule: the editor must round-trip unknown/unsupported
  fields untouched (edit what it understands, preserve the rest) or it will
  destroy hand-tuned files. This rule shapes the whole data model.
- **File targeting:** which map folder to write into (custom/WIP maps only?);
  interaction with the map being unsaved/re-zipped by the mapper afterward.
- **Comfort while authoring:** long sessions in headset; decide session aids
  (autosave interval, "continue where I left off").

### 1.3 Risks / honest costs
- VR UI for timeline+forms is the majority of the work; the playback/pose
  machinery is largely done. Text/number entry in VR stays clumsy — lean on
  snap, nudge, and hot-reload-assisted PC hand-editing for precision.
- Menu-environment authoring means lighting/context differs from in-map.
- Quest-only mappers get full authoring (unique selling point); PC mappers
  may still prefer desktop editing — hot reload serves them either way.

### 1.4 Rough shape of v1 (for scoping discussion)
Scrub + play-from-here; screen add/clone/select; whole-pose keyframe capture
with beat snap and default easing; keyframe list page (edit time/easing,
delete); effect range editor for the shipped effect types; group assignment
with index phase offset; save/load with unknown-field preservation; reload
button. Everything else (lane timeline, in-map preview, mirroring, undo depth)
is v1.5+ triage.

---

## Part 2 — ChroMapper plugin

**Concept:** a C# plugin for ChroMapper (the community's standard PC map
editor) adding a Big Screen panel/track so mappers author movement alongside
their notes and lighting, on the same beat grid and timeline they already use.

### 2.1 Facts to verify before any design conversation (may be outdated)
- ChroMapper plugin API surface today: plugin loading (Plugins folder DLLs),
  what UI extension points exist (ExtensionButtons, custom panels), what map
  data and playhead/timeline events are exposed to plugins, and whether
  plugins can draw custom 3D objects in the editor's world view.
- ChroMapper's update cadence and how badly plugins break across updates
  (maintenance burden estimate depends on this).
- Whether CM's licensing/plugin norms allow shipping a closed-source plugin —
  Big Screen is GPL-3.0-only now, so the plugin would be too; confirm that's
  compatible with CM's ecosystem expectations (CM itself is open source).
- Prior art: existing CM plugins that add side-files to the map folder
  (anything Cinema- or Vivify-adjacent) — steal their UX conventions.

### 2.2 What the plugin would need to provide
- **Timeline integration:** Big Screen keyframes/effect-ranges displayed on
  CM's beat timeline (as a lane or marker strip), placed/moved/deleted with
  CM's editing idioms, snapping to CM's precision settings. This is the
  entire reason to be in CM — if the plugin can't ride the real timeline and
  is just a form window, a standalone tool would serve equally well.
- **3D preview problem (the hard one):** CM will never run Big Screen's
  renderer. Decide the preview bar: (a) proxy quads in CM's world view that
  follow the movement data (pose/scale/opacity only — no video, no effects);
  (b) proxy quads with a static thumbnail texture; (c) no 3D preview, numbers
  only. Effects (shatter/wave) would at best be labeled ranges, not previews.
  The real preview loop remains "copy to Quest / hot reload" — the plugin
  should have a one-click "push to Quest via adb" convenience if feasible.
- **Editing forms:** per-keyframe numeric entry (precise, the thing VR is bad
  at), easing picker, group/clone management, effect parameter forms with
  min/max validation matching the mod's limits.
- **File lifecycle:** read/write the shared JSON in the map folder CM has
  open; same unknown-field preservation rule as the in-mod editor; write on
  CM save vs its own save button; conflict story if the mapper also edited
  in-headset (last-writer-wins + backup is probably enough — decide).
- **Coordinate/unit agreement:** CM's world units and axes vs the mod's; one
  documented mapping, tested with a round-trip fixture file.
- **Validation parity:** the plugin should embed the same validation rules as
  the mod (ideally generated from one shared schema/spec so they can't
  drift) and show errors at authoring time.

### 2.3 Risks / honest costs
- New toolchain (C#/Unity/CM API) with a real learning curve, plus ongoing
  maintenance chained to CM releases — this is a second product, not a
  feature.
- Preview fidelity will disappoint without careful expectation-setting;
  the proxy-quad approach must be framed as "blocking, not preview."
- PC-only by nature; excludes Quest-only creators (the in-mod tool's
  audience) — the two tools are complementary, not competing.

### 2.4 Rough shape of a v1 plugin (for scoping discussion)
Marker lane on CM's timeline for keyframes/effect ranges; property editing
form; proxy-quad world-view ghosts for pose; JSON read/write with
preservation; shared-schema validation; optional adb push. No effect
rendering, no video.

---

## Part 3 — Sequencing recommendation (to pressure-test later)

1. **Format spec + loader + hot reload + validation errors** — ships value
   alone (hand-authoring and LLM-generated files become possible) and both
   tools depend on it. Nothing else should start before this is frozen.
2. **In-mod authoring mode** — most existing code reused, serves Quest-only
   mappers, dogfoods the format, and its capture workflow (scrub → pose →
   keyframe) covers the majority of real choreography needs.
3. **ChroMapper plugin** — only if PC mappers ask for it once files exist in
   the wild; by then the format is proven and the plugin is "just" an editor.

## Part 4 — Consolidated open-question checklist

Format: property set v1 · beats + BPM-change policy · absolute vs relative
placement · clone limits/decoder policy · group offset semantics · precedence
vs user placement · comfort clamps and reduce-motion toggle · perf budget +
over-budget behavior · partial vs all-or-nothing load · filename/discovery ·
per-difficulty? · spec doc home.
In-mod: entry point · menu vs in-map authoring · timeline UI tier (a/b/c) ·
whole-pose vs per-property capture · beat snap divisions · easing editing UX ·
screen selection UX · effect panel scope · undo depth · round-trip
preservation rule · autosave.
ChroMapper: verify plugin API/update-breakage/licensing · timeline lane
feasibility · preview tier (proxy/static/none) · save lifecycle + conflict
rule · unit mapping · shared validation schema · adb push.
