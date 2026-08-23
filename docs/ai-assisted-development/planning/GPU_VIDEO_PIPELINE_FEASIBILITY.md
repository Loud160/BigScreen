# GPU Video Pipeline Feasibility Study

Status: Option 1 is being implemented experimentally on branch
`codex/major-feature-development`; Options 2 and 3 remain research only.

Last reviewed: August 22, 2026

## Purpose

This document records the investigation into three possible replacements for
Big Screen's CPU-side YUV-to-RGBA video path:

1. Upload decoded YUV planes and convert them to RGB with a GPU shader.
2. Send MediaCodec output to a `SurfaceTexture` / external OpenGL ES texture.
3. Import MediaCodec output through `AHardwareBuffer` into Vulkan.

The goals are to reduce decoder-worker CPU use, memory traffic, texture-upload
cost, heat, and battery use without sacrificing synchronization, software
fallback, or any of the advanced effects demonstrated by the Showcase map.

This remains the design record rather than a claim that the experimental path
is release-ready. Option 1 is default-off, retains the current RGBA backend as
its permanent per-session recovery path, leaves thumbnails on RGBA, and is
limited to 8-bit SDR 4:2:0. Quest 2 is the first acceptance target; Quest 3/3S
testing follows after that path is stable.

The first Quest 2 measurements and the recommended work for improving 60 FPS
delivery are recorded separately in the
[GPU video 60 FPS optimization plan](GPU_VIDEO_60FPS_OPTIMIZATION_PLAN.md).

## Established default Big Screen pipeline

Big Screen currently keeps MediaCodec independent from Unity's graphics
backend. MediaCodec is deliberately opened without an output Surface, so both
hardware and software decoding produce CPU-readable frames. The normal path is:

```text
FFmpeg demuxing
    -> MediaCodec or software video decoder
    -> CPU-readable NV12/YUV420 frame
    -> libswscale YUV-to-RGBA conversion
    -> reusable CPU RGBA frame
    -> Unity LoadRawTextureData()
    -> Unity Texture2D.Apply()
    -> GPU video texture
```

Container rotation metadata can also cause an additional CPU-side full-frame
rotation copy after `sws_scale()`.

The current architecture has important advantages:

- the same presentation and synchronization logic works for hardware and
  software decoding;
- the decoder worker never needs to own Unity's graphics context;
- all screens share one ordinary Unity RGBA texture;
- pause, seek, looping, Replay, preview playback, and gameplay use the same
  frame-selection model;
- mapper effects and the Showcase can operate on a normal Unity texture;
- failures can fall back to the proven RGBA implementation.

The cost is CPU conversion and memory movement on every presented frame.

## Pixel traffic at 1080p

For a 1920 x 1080 picture:

| Format | Bytes per pixel | Data per frame | 30 FPS | 60 FPS |
|---|---:|---:|---:|---:|
| RGBA32 | 4.0 | 8.29 MB | 248.8 MB/s | 497.7 MB/s |
| 8-bit YUV 4:2:0 | 1.5 | 3.11 MB | 93.3 MB/s | 186.6 MB/s |

Keeping the picture in 4:2:0 form until it reaches the GPU therefore reduces
the raw CPU-side picture stream by 62.5 percent. The practical memory-traffic
saving can be larger because the current RGBA picture is written by
`sws_scale()`, copied into Unity's texture storage, and then uploaded to the
GPU.

These numbers describe picture movement, not total Beat Saber CPU or battery
use. Display rendering, game simulation, tracking, audio, FFmpeg demuxing,
MediaCodec scheduling, and the GPU remain active. An optimization cannot save
more power than the video pipeline currently consumes.

## Verified target conditions

The Quest 2 connected during this investigation reported:

- Android 14;
- Android API level 34;
- a Vulkan Beat Saber renderer.

Existing diagnostic logs also contain Unity's Vulkan XR messages. The graphics
backend should still be detected at runtime rather than assumed, especially
before supporting another Beat Saber or Quest firmware version.

This Vulkan result materially changes the value of the proposed
`SurfaceTexture` route because `SurfaceTexture` exposes a
`GL_TEXTURE_EXTERNAL_OES`, which is an OpenGL ES resource rather than a Vulkan
image.

## Required renderer contract

None of the proposed decoder paths has to remove Showcase functionality. The
safe architectural rule is:

> Every presentation backend must ultimately provide one ordinary,
> Big Screen-owned, shared RGB texture that the existing screen renderer can
> use.

Conceptually:

```text
CPU RGBA fallback -----------\
CPU YUV plane uploads --------> shared RGB presentation texture
MediaCodec GPU frame --------/
                                      |
                                      +-> normal screen
                                      +-> Cinema additional screens
                                      +-> Showcase screens
                                      +-> crack/shatter snapshot
```

The conversion or GPU copy must happen once per decoded frame, not separately
for every visible screen. This is particularly important for the Showcase,
where many very large and overlapping surfaces can be visible simultaneously.

## Option 1: YUV plane upload and GPU conversion

### Proposed path

```text
MediaCodec/software decoder
    -> CPU-readable NV12 or YUV420P
    -> Y and UV textures, or Y/U/V textures
    -> one GPU YUV-to-RGBA conversion pass
    -> shared RGBA presentation texture
```

MediaCodec commonly produces NV12-like output. Software decoders commonly
produce planar YUV420P. The implementation must support both rather than
assuming a single plane arrangement.

### Expected benefit

- 62.5 percent less CPU-side pixel data for 8-bit 4:2:0 video.
- Removes `sws_scale()` from the normal presentation path.
- Can remove the CPU container-rotation copy when rotation becomes a base UV
  transform.
- Likely reduces Big Screen decoder-worker CPU use by approximately 20 to 50
  percent for ordinary playback. This is an engineering estimate, not a
  measured result.
- May remove roughly 1 to 3 milliseconds from the current combined
  decode/conversion measurement at 1080p, depending on codec, frame rate,
  colorspace effects, and decoder output. Stage-specific instrumentation is
  required before treating that estimate as fact.
- Should improve 60 FPS stability and thermal headroom, particularly on Quest
  2.

This path does not completely eliminate CPU-to-Unity copies if the plane
textures are still updated through Unity's raw-texture APIs. It nevertheless
substantially reduces the size of those copies.

### Implementation requirements

- NV12 and YUV420P plane layouts.
- Plane row stride, pixel stride, codec padding, crop metadata, and odd sizes.
- BT.601, BT.709, and BT.2020 matrices as applicable to supported 8-bit video.
- Limited-range and full-range input.
- Correct U/V order and chroma siting.
- Reusable plane buffers with no per-frame allocation.
- A Unity/Vulkan-compatible conversion shader.
- GPU equivalents of Cinema brightness, contrast, saturation, hue, exposure,
  gamma, and vignette processing.
- A shared RGBA output texture suitable for `Graphics.CopyTexture` and ordinary
  Unity materials.
- The current CPU RGBA path as a fallback.

The August 18 development tree now contains a small, reproducible Android
AssetBundle shader project. It proves the repository can carry a pinned Unity
project and embed a custom shader, but it is not a YUV conversion pipeline and
does not establish the runtime behavior of a future GPU-video implementation.
A future proof of concept may reuse the packaging mechanics after the current
material paths complete their on-device regression pass.

### Showcase compatibility

Compatible when YUV is converted once into the shared RGBA presentation
texture.

Directly performing YUV conversion in every screen material is not recommended
for Showcase use. It would require two or three texture samples and a color
matrix for every screen pixel, multiplying that work across the vortex,
corkscrew, floating-screen, and other high-overdraw scenes.

The shared-RGBA design preserves:

- all screen motion, size, curve, rotation, deformation, splitting, and UV
  choreography;
- additional Cinema screens;
- mapper color correction and vignette;
- cracking and the frozen shatter texture;
- all current front/back and transparency behavior.

### Difficulty and risk

Difficulty: moderate to high.

Estimated release-hardening scale: approximately two to four weeks, including
shader/tooling work and testing on both Quest generations.

Primary risks:

- incorrect colors or range;
- green/dark edges caused by decoder stride or crop mistakes;
- visual differences between software and hardware fallback;
- moving too much work to the GPU during already GPU-heavy Showcase scenes;
- shader compatibility and asset packaging across game updates.

This is the recommended first major pipeline optimization.

## Option 2: MediaCodec SurfaceTexture / external OES

### Proposed path

```text
MediaCodec
    -> Android Surface
    -> SurfaceTexture
    -> GL_TEXTURE_EXTERNAL_OES
    -> GPU conversion/blit
    -> shared RGB presentation texture
```

Android MediaCodec supports rendering selected output buffers to an output
Surface. FFmpeg exposes APIs for initializing MediaCodec with a Surface and for
rendering or discarding specific decoded output buffers. This means the
existing song-time selection concept can survive, although the mailbox becomes
a decoded-buffer/presentation state machine rather than a mailbox of RGBA byte
arrays.

### The Vulkan mismatch

`SurfaceTexture` is fundamentally an OpenGL ES consumer. Its texture target is
`GL_TEXTURE_EXTERNAL_OES`, and `updateTexImage()` must run on a thread owning
the appropriate GLES context.

Because the current Beat Saber build is using Vulkan, the literal proposal
would require a separate EGL/GLES context followed by cross-API sharing or a
copy into a Vulkan-compatible image:

```text
MediaCodec Surface
    -> external GLES image
    -> EGL/GLES rendering and synchronization
    -> shared/intermediate Android buffer
    -> Vulkan image
    -> Unity
```

That is no longer the simple zero-copy route described by the proposal. The
additional bridge can reduce or eliminate its advantage and adds serious
lifecycle complexity.

### Expected benefit

On an OpenGL ES game, this route could remove most CPU pixel conversion and
upload work, potentially reducing Big Screen's CPU-side video work by 50 to 80
percent.

On the current Vulkan game, no comparable gain should be assumed. An extra
GLES-to-Vulkan bridge could make it no better than Option 1 and could even add
GPU or synchronization overhead.

### Showcase compatibility

A direct external-OES texture is not a drop-in replacement for the current
Unity RGBA texture. It would complicate or break:

- fracture snapshots;
- mapper-authored alpha/vignette;
- Cinema color correction;
- standard Unity material use;
- texture ownership during scene teardown.

The Showcase remains possible only if the external frame is converted or
blitted once into a normal shared RGB texture. That remains GPU-only but is no
longer strict zero-copy.

### Difficulty and risk

Difficulty: high to very high.

Estimated release-hardening scale: approximately one to two months.

Primary risks:

- EGL context ownership and multithreaded Unity rendering;
- cross-API fences and frame lifetime;
- Surface loss during menu/game scene changes;
- pause, seek, loop, Replay, restart, and decoder-flush behavior;
- native crashes during scene teardown;
- a new graphics bridge that is specific to a backend the game is not using.

Recommendation: do not prioritize this route for the current Vulkan Beat Saber
build. It would become relevant only if a supported game version actually uses
OpenGL ES.

## Option 3: AHardwareBuffer imported into Vulkan

### Preferred conceptual path

```text
FFmpeg demuxing
    -> MediaCodec with an AImageReader output Surface
    -> AImage / AHardwareBuffer
    -> imported Vulkan YUV image
    -> one Vulkan YUV-to-RGBA pass
    -> shared Big Screen RGBA presentation texture
```

Android hardware buffers are designed for sharing images between hardware
components. They can be marked for GPU sampling and imported as Vulkan external
memory using `VK_ANDROID_external_memory_android_hardware_buffer`.

### Two possible acquisition mechanisms

#### MediaCodec block model

Android API 30 added `MediaCodec.OutputFrame.getHardwareBuffer()`. It requires
MediaCodec's block model. The block model disables the ordinary synchronous
input/output dequeue API and uses callback-driven queue requests.

Using it would therefore replace a large part of Big Screen's current
FFmpeg/MediaCodec worker state machine. It is technically possible on the
connected API 34 Quest, but it is not the preferred first prototype.

#### AImageReader output Surface

An `AImageReader` can expose an `ANativeWindow` as its producer Surface and
provide GPU-readable private images through `AImage_getHardwareBuffer()`.
MediaCodec can render output into that Surface.

This route is more compatible with Big Screen's current design because FFmpeg
can potentially continue to:

- demux packets;
- configure MediaCodec;
- expose decoded output timing;
- let Big Screen select, render, or discard a decoded output buffer.

The AImageReader then receives the selected rendered frame for Vulkan import.
This is the first Vulkan zero/near-zero-copy route that should be prototyped.

Qualcomm codec support for the selected AImageReader format, usage, resolution,
and frame rate must be tested on Quest 2 and Quest 3. API availability alone
does not prove that every vendor decoder accepts every Surface configuration.

### Expected benefit

- Eliminates CPU YUV retrieval for the hardware backend.
- Eliminates `sws_scale()`.
- Eliminates CPU RGBA frame storage and rotation.
- Eliminates `LoadRawTextureData()` and the CPU-to-GPU picture upload.
- Potentially removes 50 to 85 percent of Big Screen's CPU-side pixel work.
- Offers the best thermal and battery headroom of the three approaches.

The GPU still has to sample/convert YUV and write the shared RGBA presentation
texture. FFmpeg demuxing, compressed packet submission, MediaCodec operation,
synchronization, and rendering also remain.

The current combined decode statistic would need to be split into hardware
decode/delivery, conversion, upload, and presentation measurements before the
improvement can be evaluated correctly.

### Vulkan and Unity requirements

- Verify the required Vulkan external-memory extensions are both available and
  enabled on Unity's already-created Vulkan device.
- Import and cache images belonging to MediaCodec's reusable buffer pool.
- Query Android external-format properties.
- Use Vulkan YCbCr sampler conversion or a custom conversion shader.
- Handle color matrix, range, crop, and chroma position.
- Synchronize decoder completion, Vulkan reads, and buffer return to
  MediaCodec.
- Prevent a decoder buffer from being recycled while the GPU still samples it.
- Submit Vulkan work through Unity's render thread/native plug-in interface.
- Convert once into a normal Unity-compatible RGBA image.
- Tear down every imported image safely on seek, restart, menu exit, map exit,
  decoder fallback, graphics-device loss, and mod shutdown.

Unity's external-texture API can wrap a Vulkan `VkImage`, but wrapping alone
does not perform YUV conversion, resource-state transitions, queue ownership,
or synchronization.

The project currently compiles against Android API level 24, while
`AHardwareBuffer` begins at API 26. Future code would need runtime-gated dynamic
loading or a carefully evaluated increase to the native Android platform
target.

### Showcase compatibility

Fully compatible if the imported frame is converted once into Big Screen's
shared RGBA presentation texture.

Directly exposing the opaque external YUV image to existing screen materials is
not sufficient. It would require reimplementing snapshots, vignette alpha,
color correction, and several current Unity texture assumptions.

The shared-RGBA normalization pass keeps the decoder backend invisible to the
Showcase renderer and preserves every current choreography and screen effect.

### Difficulty and risk

Difficulty: very high.

Estimated scale: several weeks for a proof of concept and potentially two to
three months for safe fallbacks, synchronization, teardown, Quest 2/3 testing,
and production hardening.

Primary risks:

- vendor decoder incompatibility with the requested ImageReader Surface;
- Vulkan extensions not enabled by Unity;
- incorrect buffer and fence lifetime;
- render-thread or queue-ownership violations;
- GPU use-after-free during scene changes;
- device-loss or native SIGSEGV failures that C++ exceptions cannot recover;
- substantially more maintenance across Beat Saber Unity upgrades.

This is the best long-term hardware path if a contained prototype validates the
entire buffer and synchronization chain.

## Independent optimization: container rotation in UVs

Container display-matrix rotation should be considered separately from the
three presentation backends.

The current RGBA backend physically copies every pixel for 90, 180, or 270
degree container orientation. Big Screen already has extensive UV transforms
for user rotation, zoom, offsets, splitting, cropping, and Showcase animation.
Container orientation can become the base UV transform, with user and mapper
transforms layered on top.

Benefits:

- removes an entire CPU frame copy for rotated media;
- works with the current RGBA backend and every proposed future backend;
- does not prevent any Showcase effect;
- has much lower implementation risk than changing decoder presentation.

Care is still required for 90/270-degree width, height, aspect, crop, and
letterbox calculations.

## Recommended implementation order

1. Add stage-specific timing for MediaCodec delivery, software decode,
   `sws_scale`, visual effects, Unity upload, and GPU presentation.
2. Move container display rotation into the base UV transform.
3. Implement YUV plane upload with one GPU conversion into a shared RGBA
   texture.
4. Keep the current RGBA path as the permanent compatibility fallback.
5. Compare identical maps and videos on Quest 2 and Quest 3 at 30 and 60 FPS,
   with normal and Showcase screen loads.
6. Build a small AImageReader/AHardwareBuffer/Vulkan proof of concept that does
   not initially replace normal playback.
7. Verify codec compatibility, Unity Vulkan extension availability, image
   import, synchronization, seeking, looping, and repeated scene teardown.
8. Promote the Vulkan hardware-buffer backend only if it is measurably better
   and at least as stable as YUV-plane upload.
9. Do not build the external-OES bridge unless Big Screen targets a verified
   OpenGL ES game configuration.

## Benchmark requirements

Any comparison must use the same:

- Quest model and firmware;
- headset refresh rate;
- Beat Saber version and mod list;
- map, difficulty, modifiers, and environment;
- video file, codec, resolution, source FPS, and playback speed;
- Big Screen FPS cap;
- headset temperature and battery/charging state.

Record at minimum:

- whole-game CPU time;
- Big Screen decoder-worker CPU time;
- MediaCodec delivery time;
- conversion/effect time;
- Unity or native texture-upload time;
- average and peak decode/presentation latency;
- presented and missed video frames;
- gameplay minimum/average/maximum FPS;
- GPU frame time where safely available;
- charge/current measurements;
- allocations and buffer-pool growth;
- behavior after repeated seek, loop, restart, menu entry, and map exit.

Performance should be evaluated on ordinary single-screen maps and on the
Showcase. A method that improves one flat screen but substantially increases
GPU cost for many giant overlapping screens is not a complete improvement for
Big Screen.

## API references

- Android MediaCodec output Surface and buffer presentation:
  <https://developer.android.com/reference/android/media/MediaCodec>
- Android SurfaceTexture and `GL_TEXTURE_EXTERNAL_OES` restrictions:
  <https://developer.android.com/reference/android/graphics/SurfaceTexture>
- Android NDK AHardwareBuffer:
  <https://developer.android.com/ndk/reference/group/a-hardware-buffer>
- Android NDK AImageReader and MediaCodec APIs:
  <https://developer.android.com/ndk/reference/group/media>
- Android MediaCodec block model and OutputFrame:
  <https://developer.android.com/reference/android/media/MediaCodec.OutputFrame>
- FFmpeg MediaCodec Surface integration:
  <https://ffmpeg.org/doxygen/7.0/mediacodec_8c.html>
- Vulkan external Android hardware-buffer support:
  <https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html>
- Unity external textures:
  <https://docs.unity3d.com/ScriptReference/Texture2D.CreateExternalTexture.html>
- Unity native rendering plug-in interface:
  <https://docs.unity3d.com/Manual/native-plugin-interface.html>

## Final assessment

| Method | Likely benefit | Difficulty | Risk | Showcase-safe design |
|---|---|---|---|---|
| YUV planes plus one GPU conversion | High for its complexity | Moderate-high | Medium | Convert once to shared RGBA |
| SurfaceTexture / external OES | High on GLES; uncertain on current Vulkan build | High-very high | High | GLES-to-RGBA/Vulkan bridge required |
| AHardwareBuffer imported into Vulkan | Highest potential | Very high | Very high | Vulkan conversion to shared RGBA |

The recommended near-term investigation is Option 1. The recommended long-term
hardware research path is Option 3 through an AImageReader output Surface.
Option 2 should not be prioritized for the current Vulkan Beat Saber build.
