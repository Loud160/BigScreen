# Menu gameplay-environment preview implementation record

## Current status

The earlier partial-scene experiment remains rejected, but the feature is no
longer deferred. Big Screen now implements an optional persistent menu host by
entering through Beat Saber's complete standard-level transition, following
the proven high-level approach used by Qounters rather than reconstructing an
environment scene or its dependency graph by hand.

The user-facing **Menu Environment** dropdown has four modes:

1. **No Environment** — the established unobstructed placement view.
2. **Menu Environment** — Beat Saber's stock menu scenery (the default).
3. **Map Environment** — the selected map's complete gameplay environment at
   its starting state.
4. **Map Environment + Lightshow** — the same hosted environment with beatmap
   callbacks advanced from Big Screen's Video Library preview clock.

The Quest ARM64 target and repository tests validate the implementation at
build time. The complete lifecycle still requires the headset checks recorded
below before this should be treated as release-ready.

## Why the first implementation failed

Beat Saber environment scenes are Zenject scene decorators, not self-contained
visual scenes. A standard level supplies the environment together with
`StandardGameplay`, `GameCore`, and several setup-data objects containing the
beatmap, colors, player settings, modifiers, callbacks, and timing services.

The removed experiment tried incomplete variants of that contract:

- Direct Addressables loading did not construct a usable environment.
- Initializing the environment decorator from the menu container failed on
  missing gameplay bindings such as `ColorManager`.
- A minimal custom `AppendScenes` transition still lacked the complete
  gameplay object graph and crashed during environment installation.
- Cloning or partially repurposing standard transition data retained virtual
  behavior that expected the full setup and also crashed.

Those results remain important. Do not restore the partial `AppendScenes`
host, manually initialize a gameplay decorator from the menu container, or add
missing Zenject bindings one exception at a time.

## Why the complete transition is now used

The original note rejected a full gameplay transition because it could create
competing cameras, input, audio, object spawning, and mod hooks. Reviewing the
current Qounters implementation established a practical control pattern:

- retain `MenuCore` in `GameScenesManager._neverUnloadScenes`;
- use `StandardLevelScenesTransitionSetupDataSO::Init` and
  `GameScenesManager::PushScenes` so every environment installer
  receives Beat Saber's supported dependency graph;
- disable gameplay-only roots, the local player, and the gameplay camera after
  installation;
- switch the menu keyboard/controller path to the input module created by the
  hosted scene;
- keep the stock menu environment hidden;
- pop the hosted scenes and restore the original input/environment when done.

Environment scenes do not share one required root layout. Some expose an
`Environment` child, while others such as Big Mirror can publish several
top-level roots. Big Screen therefore treats the loaded Unity scene as the
ownership boundary, retains one live object only as a scene anchor, and changes
visibility through the scene's captured renderers, lighting components, and HUD
controllers. It does not deactivate an arbitrary scene root: that root may also
own Zenject or third-party gameplay callbacks whose lifetime must remain intact
while the host is retained.

Big Screen adds stricter ownership around that pattern because it also owns a
video decoder, retained side menus, mapper presentation, and a preview song
clock.

## Big Screen host architecture

`MenuGameplayEnvironmentHost` is the sole owner. Its states are Idle, Loading,
Ready, Unloading, and Failed.

### Scene selection and persistence

- A selected map resolves a representative difficulty, preferring the hardest
  Standard chart and falling back to the last valid chart.
- Screen mapper/Chroma ownership is deliberately separate from this host.
  Environment-tab controls always apply to the hosted scene.
- Returning from the editor to the song browser does not unload the scene.
- A forced Big Mirror host is primed from a safe installed OST level when the
  menu opens and remains resident across every song selection. The selected
  song is not allowed to turn identical Big Mirror scene residency back into a
  map-by-map GameCore reload.
- With the override off, static Map Environment mode reuses a ready host when
  the next song needs the same environment. Map-specific callback data is only
  advanced while it matches the selected level; stale callbacks are never
  driven with a different song's preview clock.
- A different environment uses serialized `PopScenes` followed by `PushScenes`.
  The two complete GameCore scene sets are never overlapped because Replay and
  other mods can retain callbacks against the outgoing audio controller.

### Gameplay suppression

The host retains only the gameplay services needed to install and drive the
environment. It disables the gameplay camera and player, excludes normal note
and obstacle spawning, disables the automatic `BeatmapCallbacksUpdater`, and
filters beatmap-object callbacks. It lets Beat Saber finish constructing its
`AudioTimeSyncController`, then immediately pauses that private controller.

Existing Big Screen hooks recognize the host's exact transition setup and
audio-controller pointers. Those hooks bypass ordinary gameplay video setup,
performance sessions, Showcase changes, and gameplay teardown for the private
menu transition. A menu environment can therefore never be mistaken for a
real Play/Practice launch.

### Lightshow clock

In **Map Environment + Lightshow**, Big Screen manually advances the hosted
`BeatmapCallbacksController` from the same `previewSongTime_` that drives
video/audio synchronization. Scrubbing, pausing, playback, and looping retain
one clock. Switching back to static Map Environment rewinds callbacks to song
time zero instead of freezing an arbitrary animated state.

### Transition and UI safety

- A screen preview is deferred while an environment Push/Pop is active,
  then rebuilt after the new Environment scene owns it.
- Rapid song changes keep only the newest pending selection.
- A close, mode change, or focus loss during Push queues teardown until
  that transition completes; `GameScenesManager` is never asked to run Pop
  concurrently.
- The retained Big Screen flow is reattached after the complete gameplay scene
  transition if Beat Saber temporarily changed the youngest coordinator.
- Menu input, keyboard input, the stock environment, and the fade state are
  restored on Pop and on guarded failures.
- A host failure reports a visible error and falls back to the ordinary menu;
  it does not disable video playback or the complete mod.

## Required on-device validation

Before release, verify on Quest 2 and then Quest 3/3S:

1. Open and close Big Screen repeatedly in all four modes.
2. Enter a map editor, return to the browser, and confirm the environment is
   retained with no reload.
3. Select two maps sharing an environment in static mode and confirm reuse.
4. Select different environments and confirm exactly one controlled replace.
5. In Lightshow mode, verify play, pause, seek backward/forward, and loop reset.
6. Confirm no notes, obstacles, sabers, gameplay camera, or gameplay audio are
   active in the menu.
7. Verify the screen is recreated in the new Environment scene and uses the
   same mapper/user geometry as gameplay.
8. Press the Meta button during loading and while ready, then return.
9. Disable Big Screen and change the dropdown while a load is in flight.
10. Close the menu during loading, while ready, and during replacement.
11. Launch a real Solo, Campaign, and Showcase map afterward; confirm normal
    gameplay hooks, input, environment, video, pause, results, and exit.
12. Repeat with Chroma/Noodle installed and with maps that request different
    environments or contain malformed optional Cinema metadata.

Logs should show one Push or Replace per required change, reuse for a matching
static environment, one Pop on teardown, and no ordinary gameplay performance
session for the private menu host.

## Preserved prohibitions

- Do not use the removed partial `AppendScenes` implementation.
- Do not initialize environment decorators against the menu DI container.
- Do not invent per-environment mock gameplay dependencies.
- Do not allow two host transitions to run concurrently.
- Do not drive map lighting from a second timer.
- Do not leave MenuCore, input modules, callbacks, or stock environment state
  altered after the Big Screen flow closes.
