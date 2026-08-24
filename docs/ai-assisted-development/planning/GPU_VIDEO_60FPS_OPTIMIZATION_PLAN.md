# GPU Video 60 FPS Optimization Plan

Status: bounded read-ahead, bounded late-frame catch-up, and a default-off
packed-atlas upload comparison are implemented on the
`codex/major-feature-development` branch; Quest performance validation remains
in progress.

Last reviewed: August 23, 2026

## Purpose

This document records the next practical optimization choices after Big
Screen's first GPU Video Conversion implementation, which was default-off when
this plan was written and has since been promoted to default-on. It is a
follow-up to [GPU video-pipeline feasibility](GPU_VIDEO_PIPELINE_FEASIBILITY.md),
not a commitment to implement every option.

The current experimental path keeps decoded 8-bit SDR 4:2:0 pictures as YUV,
uploads reusable Y, U, and V textures, converts them once into a stable shared
RGB RenderTexture, and preserves the established CPU RGBA path as a per-session
fallback. The shared texture keeps the ordinary screen, Cinema screens,
Showcase choreography, deformation, crack, and shatter systems unchanged.

## Quest 2 checkpoint evidence

An informal 1080p 60 FPS comparison on Quest 2 produced approximately:

| Presentation path | Average decode | Peak decode | Video FPS average |
|---|---:|---:|---:|
| CPU RGBA conversion | 5.2 ms | 17 ms | 48.5 FPS |
| GPU YUV conversion | 2.8 ms | 10 ms | 51.5 FPS |

The two paths were visually equivalent after correcting the GPU path's
gamma-to-linear transfer. Neither run showed obvious visible stutter, and the
GPU path produced slightly fewer missed frames. These were back-to-back user
observations rather than a controlled benchmark, so they establish direction,
not a final performance claim.

The result shows that removing `sws_scale()` substantially reduces decoder
work, but decoder time is not the complete presentation latency. The live
decode measurement ends when the worker publishes a frame. It excludes the
delay until Unity's next update, the three plane texture uploads, each
`Texture2D.Apply()`, and the YUV-to-RGB `Graphics.Blit()`.

At 60 FPS, a new source-picture deadline occurs every 16.67 ms. Beat Saber on
the tested Quest 2 normally runs near 72 Hz, with game updates approximately
13.89 ms apart. The original reactive one-frame mailbox therefore had little
timing margin: a picture requested after a song-clock slot changed commonly
could not be uploaded until the following Unity update. The new experiment is
intended to test whether removing that reactive wait reduces missed deadlines.

## Measurement before further changes

Do not select an optimization from decode time alone. Controlled comparisons
should keep all of the following constant:

- the same local video and map;
- FFmpeg runtime and hardware/software decoder selection;
- 60 FPS cap and playback-rate/fit-to-song state;
- Automatic Performance disabled;
- Graphics Tweaks, Chroma, Noodle, recording, and other runtime load;
- screen layout, resolution, environment, and Quest refresh rate.

Alternate GPU conversion off/on for at least three complete runs per path.
Record average and peak decode time, average and peak main-thread presentation
time, expected/presented/missed pictures, Quest FPS, decoder CPU time, and any
automatic fallback. A first run after opening the game should be identified as
a warm-up rather than silently mixed with warm runs.

If presentation time spikes while decoder time remains low, plane uploads or
the conversion pass are the likely bottleneck. If both remain comfortably low
but deadlines are missed, the reactive request/mailbox timing is the stronger
candidate. The source's declared frame rate and actual PTS/duration cadence
must also be checked before treating a variable-frame-rate or duplicated-frame
file as a 60-unique-picture-per-second test.

## Implemented first experiment: bounded timestamped read-ahead

The GPU path now replaces the single reactive output mailbox with a
timestamp-ordered YUV queue. After startup or a seek, the decoder continues
decoding sequentially until its memory budget or internal frame ceiling is
filled. The user-facing budget ranges from 32–256 MiB in 16 MiB steps and
defaults to 64 MiB; a separate 120-frame ceiling limits very small pictures.

The song/audio clock remains authoritative. Read-ahead must never advance what
the user sees. Unity normally selects the newest queued picture whose
presentation time is due and retains the prior picture when no replacement is
ready. A later refinement uses spare display updates to recover one slightly
late picture: if an older and newer picture are both due and the older one is
no more than one presentation interval late, it presents the older one now and
keeps the newer one for the following update. Anything older, or any larger
backlog, is discarded so audiovisual delay cannot accumulate.

Implemented safeguards:

- retain generation IDs and discard every older-generation picture;
- flush the queue on seek, scrub, restart, loop, map change, decoder reopen,
  and material/presentation failure;
- stop filling while paused and resume from the correct media time;
- bound both total YUV bytes and frame count;
- keep the worker nonblocking during Unity teardown;
- recycle plane buffers rather than allocating per picture;
- preserve newest-frame/coalescing behavior for large clock jumps while
  allowing one bounded catch-up presentation for a small scheduling slip;
- keep the existing CPU RGBA fallback behavior deterministic.

The implementation uses the selected on-demand YUV memory budget and a
120-frame ceiling. It receives the active media-time
presentation interval from PlaybackSession, so lower FPS ceilings and changed
playback rates do not cause unused source pictures to be converted merely to
fill the reserve. A queue epoch rejects conversions that finish after a seek,
restart, cadence change, effect replacement, or GPU/CPU transition.

The completed-gameplay performance log records the budget, calculated frame
capacity, peak/final reserve, transitions into low/empty reserve states,
bounded catch-up presentations, forced late drops, and peak due-frame backlog.
The same summary is written when a menu preview ends. These fields distinguish
producer starvation from Unity scheduling lateness without expanding the live
performance panel.

One tightly packed 1080p YUV420 picture is approximately 3.11 MB. The 64 MiB
default therefore fits about 21 pictures; 1440p fits about 11–12 depending on
the exact coded dimensions. The worker and Unity upload picture add overhead
beyond queued bytes, so Quest memory behavior must be measured at the upper
budgets rather than treating the slider value as total process cost.

Expected value: high. This directly removes the dependency on completing a
decode request between two narrowly spaced Unity presentation opportunities.

Risk: moderate. Incorrect queue flushing can present stale frames after
scrubbing, looping, restart, Replay, or scene transitions. This is the first
optimization to implement only because the existing generation and restart
contracts provide a clear foundation for handling those cases.

## Second change: preserve MediaCodec NV12 as two planes

MediaCodec commonly supplies NV12, but the current experimental transport
deinterleaves its chroma data into independent U and V buffers so every frame
has one normalized representation. Preserve NV12 when it is already available:

```text
Y plane  -> R8 texture
UV plane -> two-channel 8-bit texture
```

The conversion shader then samples U and V from the two channels of the second
texture. Native planar YUV420P remains supported through the current three-plane
path or a separate normalization path.

Benefits:

- removes the per-frame NV12 deinterleave loop;
- reduces three Unity texture uploads and `Apply()` calls to two;
- retains 4:2:0 transfer size and the stable RGB presentation RenderTexture;
- does not alter Showcase, Cinema, deformation, or shatter consumers.

Required verification includes Unity 2022.3 Android support for the selected
two-channel texture format on Quest Vulkan, UV byte order, row and pixel
strides, odd dimensions, crop metadata, and hardware-to-software fallback.

Expected value: moderate and especially relevant to hardware decoding.

Risk: low to moderate after texture-format support is proven. A UV-order or
stride mistake produces immediately visible color corruption, so failure must
fall back to the existing planar or CPU RGBA path rather than guessing.

## Implemented A/B experiment: one packed YUV atlas

Pack Y, U, and V into one R8 texture with a width of the luma plane and a total
height of approximately 1.5 times the luma height. U and V can occupy separate
half-width regions within the chroma rows. The shader maps each sample into its
corresponding atlas region.

This reduces presentation to one `LoadRawTextureData()` and one `Apply()` while
keeping the same 1.5 bytes per source pixel. The decoder should write directly
into the reusable packed allocation; repacking three completed vectors on
UnityMain would merely exchange API overhead for another full CPU copy.

Risks and requirements:

- exact half-texel sampling and region clamping are required with bilinear
  filtering so U/V samples cannot bleed across atlas boundaries;
- odd dimensions and padded decoder strides must remain correct;
- shader UV rotation and vignette/color processing must remain one pass;
- debugging is less intuitive than explicit planes;
- it overlaps with the NV12 optimization and should be compared against it,
  not implemented simultaneously without measurements.

Expected value: moderate when Unity call overhead dominates.

Risk: moderate because sampling mistakes can create seams or chroma bleeding.

The implementation is independently selectable beneath GPU Video Conversion.
The worker writes one reusable atlas directly; Unity performs one upload and
one conversion blit into the same shared RGB RenderTexture. Half-texel region
mapping prevents bilinear samples from crossing atlas boundaries, and padded
atlas width supports odd coded dimensions. Failure switches first to the
existing three-plane GPU path. The performance panel and log identify the
active layout so an A/B run cannot silently measure the fallback.

## Optional upload stabilization: double-buffer plane textures

Alternate between two complete upload texture sets. The conversion blit reads
one set while Unity prepares the next frame in the other. This may avoid an
occasional `Texture2D.Apply()` synchronization stall caused by rewriting a
texture the GPU has not finished sampling.

The shared RGB RenderTexture must remain stable; only the private input planes
alternate. The extra YUV set costs approximately 3.11 MB at 1080p and more at
1440p. Diagnostics should prove that `Apply()` or presentation peaks improve
before this memory is retained permanently.

Expected value: low to moderate and dependent on measured GPU synchronization.

Risk: low when lifetime and active-set switching are main-thread-only, but it
should not be added speculatively.

## Conditional shader optimization

The corrected conversion shader performs an explicit sRGB-to-linear transfer
after the YUV matrix so its picture matches the CPU sRGB texture path. The
exact transfer uses a per-channel power operation and may contribute GPU work
at 1080p60, although current evidence does not identify it as the bottleneck.

Only after GPU timing implicates the shader should this be replaced with one of:

- a visually validated branchless approximation;
- a small lookup texture with sufficient precision;
- a carefully controlled sRGB render-target/write-state path that delegates
  the transfer to hardware.

Every alternative must be compared against the CPU path with dark gradients,
skin tones, saturated highlights, limited/full range, BT.601, and BT.709 test
content. An arbitrary brightness multiplier is not an acceptable optimization.

Expected value: unknown until GPU profiling.

Risk: moderate to high because the first Quest test already proved that a
transfer mismatch can look acceptable in motion while materially changing the
picture.

## Recommended implementation order

1. Capture repeatable off/on baselines including presentation timing.
2. Add the bounded timestamped read-ahead queue and performance-log queue
   diagnostics (implemented experimentally).
3. Retest ordinary preview, gameplay, Replay, restart, looping, scrubbing,
   Chroma/Cinema placement, and the full Showcase.
4. Preserve NV12 as two planes if hardware-decoder presentation remains a
   measurable constraint.
5. Compare a packed atlas only if Unity upload-call overhead remains dominant.
6. Add double buffering only if traces show upload synchronization stalls.
7. Optimize the shader transfer only when GPU measurements justify its visual
   risk.

Do not proceed directly to MediaCodec `SurfaceTexture`, external OES, or
`AHardwareBuffer`/Vulkan import merely to chase 60 FPS. Those paths have much
higher renderer, synchronization, fallback, and game-update risk and remain
separate research options in the parent feasibility document.

## Acceptance criteria

- GPU conversion defaults on after the Quest 2 visual and performance pass;
  Quest 3/3S validation remains required for stable-release acceptance.
- GPU and CPU pictures remain visually equivalent for supported SDR content.
- A CFR 59.94/60 FPS source approaches its actual source cadence without
  lowering Quest gameplay FPS or increasing visible judder.
- No stale picture appears after pause, seek, Replay, loop, restart, map exit,
  or decoder fallback.
- No unbounded frame queue, per-frame allocation growth, or retained old-
  resolution buffers appear in diagnostics.
- Ordinary, curved, Cinema, shared Showcase, deformation, crack, and shatter
  surfaces continue consuming one stable shared RGB presentation texture.
- Unsupported layouts and any Unity resource failure still select the proven
  CPU RGBA path once for the rest of that playback session.
