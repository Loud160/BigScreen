# Deferred menu gameplay-environment preview

## Status

This feature is intentionally deferred until the rest of Big Screen is stable.
Do not resume it as part of unrelated menu or playback work.

As of August 13, 2026, all runtime code, settings, UI, and public feature claims
for the attempted preview have been rolled back. Big Screen's runtime source
matches commit `8b47d01`, the stable commit created immediately before this
experiment. The experimental `MenuEnvironmentPreview` source and header no
longer exist in the working implementation.

The verified safe Quest binary at the time this work was deferred had SHA-256:

```text
20F961DA94E9B88DA4AEF5D9D608EA4C99D850063EE6B726DE566CB4A952E399
```

Do not recreate the removed `GameScenesManager::AppendScenes` implementation
merely to see whether it works after another refactor. Its dependency model is
known to be incomplete and it crashes Beat Saber 1.37.0 on Quest 2.

## Intended result

While Big Screen's menu is open, the video preview should appear in a visual
representation of the environment that the selected map will use. For an
ordinary map this normally means Big Mirror. When **Allow Chroma Override** is
enabled and the selected map requests a Chroma environment, the preview should
use that environment instead.

The preview should eventually reproduce the useful visual parts of gameplay:

- Player platform and environment geometry.
- Map lights and animated environment objects.
- Spinners, side bars, spectrogram bars, and other objects according to Big
  Screen's environment switches.
- The map's selected environment and supported Chroma changes.
- The same screen size and placement that gameplay will use.

It must not start a real song, spawn notes or sabers, replace the menu's scene,
disable the menu EventSystem, or interfere with another mod's scene lifecycle.

## Beat Saber scene contract discovered during investigation

Beat Saber's standard level transition is not just an environment scene. The
PC 1.37.1 managed assemblies show that
`StandardLevelScenesTransitionSetupDataSO` loads these three scenes, in order:

1. The selected environment scene.
2. `StandardGameplay`.
3. `GameCore`.

It also installs four setup-data objects into the new root container:

1. `EnvironmentSceneSetupData`
2. `StandardGameplaySceneSetupData`
3. `GameplayCoreSceneSetupData`
4. `GameCoreSceneSetupData`

`GameplayCoreSceneSetupData` carries the selected beatmap, audio, color scheme,
player settings, gameplay modifiers, performance preset, and transformed
beatmap data. `GameplayCoreInstaller`, located in the standard gameplay scene,
uses it to bind services such as `ColorManager` and the beatmap callback and
timing systems.

An environment scene is therefore a Zenject scene decorator, not a
self-contained visual scene. Letting its gameplay components initialize
without that complete contract is unsafe.

## Approaches already tried and why they failed

### Direct Addressables environment load

The environment scene was loaded directly without a complete gameplay host.
No usable environment appeared. The scene's serialized roots and decorator
expect Beat Saber's normal scene activation and dependency-injection sequence;
loading the address alone is not enough to make a working environment.

Do not repeat a plain `Addressables.LoadSceneAsync` followed by normal root
activation. Activating the original behavior hierarchy without its bindings is
the dangerous step.

### Manually initializing `SceneDecoratorContext`

The loaded environment's decorator context was manually initialized against a
container derived from the menu. This produced an incomplete object graph. The
observed Zenject failure was:

```text
Unable to resolve 'ColorManager' while building object with type
'SaberBurnMarkSparkles'
```

The partially initialized scene was also unsafe to tear down and could leave
the menu frozen. The menu DI container is not a substitute for the gameplay
container. Do not retry this by binding only `ColorManager`; that would merely
expose the next missing gameplay service and create a version-fragile chain of
mock dependencies.

### Base `ScenesTransitionSetupDataSO` allocation

A minimal transition containing the environment and `GameCore` was attempted.
Creating the abstract/base `ScenesTransitionSetupDataSO` through
`ScriptableObject::CreateInstance` returned null on Quest. This attempt did not
crash, but it could not create a transition and displayed no environment.

### Cloning `StandardLevelScenesTransitionSetupDataSO`

A serialized standard-level transition object was cloned and its inherited
scene/setup arrays were replaced with the minimal environment plus `GameCore`
data. This crashed when the transition ran. The standard-level class retains
its virtual asynchronous preparation behavior, which expects a valid
`GameplayCoreSceneSetupData` and a complete standard gameplay transition.

Do not use a cloned `StandardLevelScenesTransitionSetupDataSO` as a generic
transition descriptor.

### Custom transition plus `GameScenesManager::AppendScenes`

A concrete Big Screen subclass of `ScenesTransitionSetupDataSO` successfully
allocated. It supplied only:

- Environment scene plus `EnvironmentSceneSetupData`.
- `GameCore` scene plus `GameCoreSceneSetupData`.

`GameScenesManager::AppendScenes` activated `GameCore`, changed the active
scene, and allowed environment installation to begin. Beat Saber then threw the
same missing-`ColorManager` Zenject exception while constructing
`SaberBurnMarkSparkles`, immediately closing the menu/game.

This confirmed that the descriptor type was not the fundamental problem. The
partial gameplay host itself was invalid. It also confirmed that mutating Beat
Saber's global scene stack from Main Menu is too invasive for this feature.

### Loading the complete gameplay transition behind the menu

This was investigated but deliberately not implemented. A correct complete
transition needs real beatmap/audio/player data and initializes most of
gameplay. Running that beside Main Menu would create competing active scenes,
UI EventSystems, audio/timing systems, global state, Chroma hooks, and other mod
hooks. It would effectively run a second game mode behind the menu and would
not be a release-quality Quest solution.

## Recommended implementation: render-only environment proxy

The next implementation should treat the menu environment as a Big
Screen-owned visual simulation, not as a gameplay session.

### Phase 1: prove a safe inactive scene load

1. Load the requested environment through Addressables without using
   `GameScenesManager` and without changing Unity's active scene.
2. Keep the source environment roots inactive. Never initialize or manually
   run its `SceneDecoratorContext`.
3. Log the scene handle, root count, root active states, and relevant component
   inventory before creating anything visible.
4. If Unity or the Addressables version will not expose the hierarchy without
   activating its behavior roots, stop this approach rather than activating
   them experimentally in the normal build.
5. Preserve the Addressables handle until every proxy object has been removed,
   then unload it through the same handle.

This phase should initially be diagnostic-only and guarded by a separate
compile-time development flag. It should not replace the current safety guard
until repeated menu-open and menu-close tests pass.

### Phase 2: construct a visual-only proxy

Create a dedicated Big Screen preview root in the menu scene. Reproduce only a
strict whitelist of visual data from the inactive source hierarchy:

- `Transform`
- `MeshFilter`
- `MeshRenderer`
- `SkinnedMeshRenderer`, only if required and verified safe
- `Light`
- `ParticleSystem`, after verifying that it has no injected controller
- `Animator`, only for animations proven to operate without gameplay scripts

Do not clone arbitrary `MonoBehaviour` components. In particular, do not copy
Zenject contexts, installers, beatmap event listeners, audio controllers,
camera controllers, saber effects, or gameplay managers.

Keep the environment Addressables handle alive while the proxy exists so its
meshes and materials cannot be released underneath the clones. Destroy the
proxy before releasing the handle.

Implement Big Mirror first. Confirm platform scale, player origin, screen
alignment, materials, and render layers before supporting other environments.

### Phase 3: Big Screen-owned motion and lighting

Drive the proxy from the selected map's preview time rather than enabling the
environment's gameplay scripts:

- Parse the map's lighting events independently.
- Map supported light/event channels to proxy lights and materials.
- Drive validated rotation and animation components from preview time.
- Apply the existing Big Screen controls for map lighting, rear light-channel
  pairs, spinner, side bars, spectrogram bars, rotation/motion, and other
  blockers.
- Stop all preview timing immediately when the menu closes or the selected map
  changes.

This may initially support a conservative subset of environment events. An
unsupported event should be ignored and logged, never allowed to crash or
freeze the menu.

### Phase 4: Chroma integration

When **Allow Chroma Override** is enabled:

1. Resolve the environment requested by the map.
2. Apply supported Chroma colors and environment-object transformations to the
   proxy.
3. Keep unsupported Chroma commands isolated and non-fatal.
4. Fall back to the standard Big Mirror proxy if the requested environment
   cannot be constructed safely.

Exact parity with arbitrary Chroma scripts should not be promised until it is
verified. Chroma normally modifies a live gameplay environment through scene
and beatmap hooks; those hooks will not automatically control a render-only
copy. A future explicit Big Screen/Chroma integration or public preview API may
be required for full parity.

## Approaches that should remain rejected

- Do not call `GameScenesManager::AppendScenes` from Big Screen's menu.
- Do not clone and repurpose the standard-level transition asset.
- Do not activate an environment's original behavior hierarchy with a partial
  or menu-derived Zenject container.
- Do not fix missing dependencies one exception at a time.
- Do not run a complete standard gameplay session concurrently with Main Menu.
- Do not manipulate Beat Saber's global EventSystem or leave another scene set
  as the active scene.
- Do not restore the removed host or its former safety-constant bypass without
  a new architecture and on-device lifecycle testing.

## Lifecycle and failure requirements

The replacement must have one owner and an explicit state machine covering
loading, ready, changing environment, and unloading. It must:

- Debounce rapid song selection changes.
- Cancel or supersede an obsolete pending load safely.
- Never have two preview environments active simultaneously.
- Restore the normal menu environment on every failure path.
- Tear down on menu close, mod disable, application focus loss, and gameplay
  transition.
- Avoid blocking the Unity UI thread while loading or scanning assets.
- Use timeouts around asynchronous operations.
- Catch and log managed exceptions at every callback boundary.
- Disable only the environment-preview feature after an error; it must not
  disable video playback or strand the user inside Big Screen's menu.
- Avoid `Resources.UnloadUnusedAssets` and forced garbage collection during an
  interactive menu transition unless profiling proves they are necessary.

## Required validation before enabling the feature

At minimum, test all of the following on Quest 2 before removing the safety
guard:

- Open and close Big Screen at least 20 times in one Beat Saber process.
- Leave and re-enter the menu without selecting a song.
- Rapidly change selected songs and letter-jump positions.
- Switch between ordinary, WIP, DLC, and Chroma maps.
- Toggle the menu gameplay environment and Chroma override repeatedly.
- Disable and re-enable Big Screen while its menu is open.
- Enter gameplay, complete or exit the map, and reopen Big Screen.
- Suspend with the Meta button and resume.
- Test with Chroma, Noodle Extensions, Replay, QCounters, and the user's normal
  mod set enabled.
- Confirm video preview, preview audio, screen editing, downloads, and storage
  menus remain functional.
- Compare memory before opening the menu, while the proxy is loaded, and after
  closing it to detect leaked scenes, materials, or objects.
- Confirm the active Unity scene and EventSystem are unchanged throughout.
- Review both Big Screen's log and PaperLog after the complete repetition test.

Passing host tests and compiling are not sufficient verification for this
feature. The final visual behavior and repeated lifecycle stability require
on-headset testing.

## Evidence to inspect when resuming

- `tmp-inspect/Main-decompiled/StandardLevelScenesTransitionSetupDataSO.cs`
- `tmp-inspect/Main-decompiled/GameplayCoreSceneSetupData.cs`
- `tmp-inspect/Main-decompiled/GameplayCoreInstaller.cs`
- `tmp-inspect/bigscreen-crash-host.log`
- `tmp-inspect/bigscreen-crash-host2.log`
- `tmp-inspect/PaperLog-crash-host.log`
- `tmp-inspect/PaperLog-crash-host2.log`

The diagnostic captures under `tmp-inspect` are local development evidence and
do not need to be included in a public release. If those files are removed,
retain this document because the experimental source and its inline comments
were intentionally removed during rollback.

The obsolete custom transition type, callbacks, setup-data fields, update
driver, settings/UI integration, and `AppendScenes`/`RemoveScenes`
implementation were all removed rather than leaving an unreachable second
architecture in production code.
