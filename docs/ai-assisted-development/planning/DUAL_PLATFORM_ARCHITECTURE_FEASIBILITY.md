# Quest and PC Dual-Platform Architecture Feasibility

Status: research only; no PC port or language migration is implemented by this
document.

Last reviewed: August 18, 2026

## Conclusion

Building a PC version of Big Screen is feasible, and sharing a substantial
amount of behavior between Quest and PC is worthwhile. The recommended design
is not a Rust rewrite. Big Screen should progressively extract a portable C++20
core while retaining a C++ Quest host and adding a C# PC host.

```text
                     Portable Big Screen C++ core
          timing, synchronization, configuration, Cinema parsing,
         choreography, geometry, diagnostics, and storage policy
                          /                   \
                         /                     \
              Quest C++ host                  PC C# host
       Scotland2, Cordl, Quest BSML      BSIPA, Harmony, PC BSML
       Android and Quest lifecycle       managed Unity integration
                    |                           |
          Quest media backend          Windows media backend
       FFmpeg/MediaCodec/RGBA        FFmpeg/D3D11VA/Unity texture
```

This structure reuses mature and tested code without forcing inherently
platform-specific Beat Saber integration into an artificial common layer.

## Expected amount of reuse

The current repository contains a considerable amount of Quest-specific UI,
Unity/IL2CPP integration, lifecycle handling, downloader integration, and song
selection behavior. For that reason, estimates that 55 to 70 percent of the
current source could immediately compile unchanged on both platforms are too
optimistic.

Reasonable planning estimates are:

- approximately 15 to 30 percent literal shared source in the current layout;
- approximately 35 to 50 percent literal shared source after deliberate
  extraction;
- approximately 65 to 80 percent shared behavior and feature logic.

The difference matters. Quest and PC can behave consistently even when their
menu, Unity, lifecycle, and graphics integration code is necessarily different.

## Code that should be shared

Good candidates for a platform-neutral core include:

- playback clock and synchronization calculations;
- playback offset, speed, Fit to Song, and frame-rate cap behavior;
- Automatic Performance policy;
- screen layout state and geometry calculations;
- Showcase choreography and interpolation;
- deformation and glass-fracture calculations;
- Cinema metadata parsing and normalization;
- video-library models and metadata;
- download and storage policy;
- performance-statistics calculations;
- error codes and diagnostic formatting.

The current host tests already demonstrate that several of these components can
build without Unity. Future extraction should preserve or expand those tests so
Quest and PC can run identical conformance fixtures.

## Code that should remain platform-specific

The following responsibilities should remain in thin platform hosts:

- Beat Saber hooks and game lifecycle;
- Quest BSML versus PC BSML user interfaces;
- Unity object discovery and creation;
- song-selection, pause, campaign, and results-screen integration;
- Android filesystem and downloader runtime integration;
- Windows filesystem and downloader integration;
- hardware decoder initialization;
- native texture presentation and graphics-device recovery.

Trying to share these implementations literally would increase coupling and
make both versions harder to maintain.

## Recommended repository structure

A single repository is preferable to two independent forks:

```text
core/
    playback/
    configuration/
    cinema/
    choreography/
    geometry/
    diagnostics/

media/
    common/
    quest/
    windows/

platforms/
    quest/
    pc/

tests/
    core/
    media/
    conformance/
```

Quest and PC should produce separate release packages but share schemas,
behavioral test vectors, and core tests. Given identical song time and settings,
both hosts should calculate the same playback target and screen state.

## PC host and native interop

The PC host should be a normal C# Beat Saber plugin using BSIPA, Harmony, the PC
BSML ecosystem, and managed Unity APIs. It should call the shared C++ core
through a small, versioned C ABI rather than exporting C++ classes.

The boundary should use:

- opaque handles;
- fixed-width plain data structures;
- explicit create and destroy functions;
- explicit buffer ownership and release calls;
- no STL objects or C++ exceptions across the boundary;
- no direct callbacks from decoder workers into Unity;
- main-thread polling or controlled message queues for host notifications.

This keeps compiler-specific C++ ABI details out of C# and makes ownership
auditable.

## PC decoding and presentation

The intended high-performance PC pipeline should eventually be:

```text
FFmpeg demuxing
    -> D3D11VA hardware decode
    -> NV12 or P010 GPU surface
    -> D3D11 video processor or shader conversion
    -> Unity external RGBA texture
    -> one shared texture used by every Big Screen surface
```

D3D11VA is an appropriate production target, but should not block the first PC
prototype. Hardware frames commonly arrive as NV12 or P010 rather than an
ordinary RGBA Unity texture. A production implementation must therefore handle
GPU color conversion, Unity render-thread synchronization, resource ownership,
device loss, resolution changes, and a safe fallback.

The safer sequence is:

1. Use FFmpeg software decoding and the existing reusable RGBA mailbox.
2. Prove lifecycle, synchronization, screen geometry, Cinema metadata, and UI.
3. Introduce a media-presentation interface.
4. Add D3D11VA and GPU-native presentation behind that interface.
5. Retain software decoding as a compatibility and recovery path.

The Showcase is compatible with either implementation. Its additional surfaces
share the same video texture and manipulate transforms, meshes, and presentation
state rather than requesting separate video decodes.

## Rust evaluation

Rust can produce native libraries for Android and Windows, so a Rust shared core
is technically possible. It is not recommended for the current Big Screen
codebase.

A rewrite would create a three-language system:

- C++ for the Quest integration shim;
- Rust for the shared core;
- C# for the PC plugin.

It would also require recreating mature behavior and tests, adding Cargo to the
Android NDK and Windows toolchains, and maintaining FFI boundaries on both
platforms instead of only on PC.

Most of Big Screen's highest-risk operations would remain unsafe foreign
boundaries even after a Rust rewrite:

- Unity and IL2CPP objects;
- Quest hooks;
- FFmpeg;
- MediaCodec and JNI;
- D3D11;
- native Unity textures.

Rust can improve memory safety inside a genuinely safe, isolated subsystem, but
it cannot automatically make stale Unity pointers or incorrectly owned native
media resources safe. It also provides no inherent decoding or rendering speed
advantage over the current C++ implementation.

Rust may be worth reconsidering for a new isolated component, such as a future
external choreography parser or library database, when that component has a
specific safety or ecosystem benefit. It should not be introduced merely to
make the project appear cross-platform.

## Approaches considered

| Approach | Main advantage | Main disadvantage | Recommendation |
|---|---|---|---|
| Separate C# PC reimplementation | Fastest initial prototype | Maximum duplication and behavioral drift | Avoid for the full product |
| Shared C++ core, C++ Quest host, C# PC host | Reuses mature logic and fits both mod ecosystems | Requires one carefully designed C ABI | Recommended |
| Rust core with C++ and C# hosts | Attractive greenfield separation | Large rewrite and two FFI boundaries | Do not use for the current port |
| Entirely native C++ PC plugin | One implementation language | Poor fit for BSIPA, BSML, and managed Unity | Avoid |

## Suggested development sequence

1. Identify platform-neutral code without changing behavior.
2. Add explicit interfaces around playback time, media decoding, screen output,
   filesystem access, and UI notifications.
3. Extract the portable C++ core while continuously building and testing Quest.
4. Add Windows builds and tests for the extracted core.
5. Create a minimal C# PC plugin that loads one local video.
6. Validate synchronization first through software decoding.
7. Add ordinary flat and curved screens and configuration.
8. Add Cinema compatibility and Chroma cooperation.
9. Port the video library and downloader UI.
10. Add D3D11VA and GPU-native presentation.
11. Add Showcase choreography and advanced visual effects.
12. Maintain cross-platform conformance tests for all shared behavior.

## Schedule expectations

A proof of concept that merely displays a video on a PC Unity surface might be
possible in several focused days. A useful PC alpha is more reasonably a
multi-week effort. Complete feature parity will take longer because Big Screen's
current UI, Beat Saber integration, lifecycle recovery, downloading, Cinema
compatibility, and advanced effects are substantial parts of the product.

The PC port should therefore be developed incrementally without destabilizing
the existing Quest implementation.
