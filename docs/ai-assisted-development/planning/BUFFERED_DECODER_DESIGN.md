# Buffered Decoder Design Note

## Status

This is a future design, not an implemented playback feature. Implement and
measure it only after native-resolution playback and FPS-only Automatic
Performance have been tested on Quest 2 and Quest 3. Keeping those experiments
separate makes their latency, memory, and frame-loss effects attributable.

## Goal

Absorb brief decoder, color-conversion, or worker-scheduling stalls without
letting the visible video stutter or skip a picture unless the prepared-frame
reserve is genuinely exhausted. Beat Saber's song clock remains authoritative;
buffering must never make the video run ahead, add intentional A/V delay, or
continue showing stale frames after a seek.

This is a short disruption reservoir, not a mechanism for loading an entire
video into memory and not a substitute for reducing an unsustainable workload.
If average decode/conversion throughput is slower than the required playback
cadence, the queue will eventually empty and normal missed-frame handling must
take over.

## Current behavior that must be preserved

- `PlaybackSession::PrewarmGameplay()` opens FFmpeg and requests the first
  visible frame before gameplay audio begins. This is already a one-frame
  startup prewarm and should remain.
- Beat Saber, Replay, practice mode, Fit to Song, offsets, looping, and menu
  preview supply the authoritative media time. The decoder does not own a
  free-running playback clock.
- The worker follows the newest timestamp request, decodes intermediate
  reference pictures sequentially, converts the selected picture to RGBA, and
  publishes one newest-frame mailbox.
- The mailbox deliberately replaces an obsolete unconsumed picture. This keeps
  current playback responsive after a clock jump and prevents a slow consumer
  from accumulating latency.
- The presentation FPS preference is a ceiling. A 60 FPS source under a 30 FPS
  limit must not convert or enqueue all 60 source pictures.
- MediaCodec buffers are released promptly after conversion. Holding Android
  hardware-decoder output surfaces/buffers can exhaust the codec and stall the
  pipeline.

Simply allowing the existing worker to run to end-of-stream would not create a
usable buffer. Its single mailbox would repeatedly overwrite pictures before
their presentation times and eventually contain only a much later frame.

## Proposed architecture

Replace the one-frame output mailbox with a small, timestamp-ordered queue of
fully converted `VideoFrame` objects. Keep the existing external-clock model.

1. Open and prewarm exactly as today.
2. The worker decodes sequentially and enqueues only pictures eligible under
   the effective presentation FPS limit and playback rate.
3. Each queued entry retains presentation time, duration, dimensions, and its
   owned reusable RGBA buffer.
4. The Unity thread asks for the newest queued picture whose interval is due at
   the current authoritative media time. A future picture must remain queued;
   it must never be uploaded early.
5. After consumption, return its RGBA allocation to the bounded reuse pool and
   wake the producer to refill the reserve.
6. Once the time target or memory budget is reached, the worker sleeps instead
   of decoding farther ahead.
7. On a backward seek, large forward jump, Replay discontinuity, loop wrap,
   selected-video change, playback-rate change, decoder fallback, or shutdown,
   invalidate the complete queue under one synchronization contract and refill
   from the new authoritative position.

The queue should be bounded by both media-time lead and owned memory rather than
exposing a user-facing frame-count setting. A starting experiment could target
roughly 100 milliseconds of ready video with a hard 32-48 MiB RGBA budget and a
small absolute entry limit. These are test values, not final constants.

## Memory concerns

Ready-to-upload RGBA is intentionally expensive:

| Frame size | Approximate RGBA bytes per frame | Three frames |
|---|---:|---:|
| 1280x720 | 3.5 MiB | 10.5 MiB |
| 1920x1080 | 7.9 MiB | 23.7 MiB |
| 2560x1440 | 14.1 MiB | 42.2 MiB |

The budget must include queued frames, the worker's conversion destination,
the main-thread upload frame, rotation scratch storage, and Unity's texture.
Do not count only the `deque` entries. A queue that is safe at 720p can cause
memory pressure or process termination at 1440p.

Do not retain decoded MediaCodec YUV frames merely to reduce RGBA storage.
Hardware output buffers are a limited codec resource, and retaining them can
block further decode. Converting promptly into Big Screen-owned memory and
releasing the hardware frame is the safer first implementation.

The reusable buffer pool and queue must share one total allocation policy.
Increasing queue capacity while leaving an independent pool cap can retain both
sets and silently double the intended memory budget. Resolution changes are no
longer part of normal playback, but closing or replacing a video must still
release oversized retained allocations.

## CPU, battery, and latency concerns

- Queueing should move the same required work earlier, not perform more work.
  Do not convert source pictures that the current FPS limiter will never show.
- Prebuffering can waste work when a player exits immediately, fails early,
  scrubs, or changes songs. Stop filling promptly during teardown or focus loss.
- A producer that continuously wakes and polls will waste CPU and battery.
  Use condition variables with explicit low-water/high-water predicates.
- Avoid busy waiting on the Unity thread or decoder worker.
- Do not block the gameplay thread waiting for the buffer to fill. Prewarming
  is opportunistic; an incomplete reserve must degrade to current playback.
- A full queue must park the worker. It must not keep decoding and discarding
  pictures merely to stay a fixed distance ahead.
- Buffering cannot repair sustained overload. Automatic Performance must still
  reduce the FPS limit when the queue repeatedly drains or visible frame loss
  crosses the configured threshold.
- Do not hide real A/V latency by presenting a frame late. When the queue is
  behind the song clock, discard obsolete queued pictures and present the best
  due picture, preserving synchronization over completeness.

## Synchronization and correctness hazards

- All queue invalidation, decoder seeking, worker stop, and reusable-buffer
  transfer must have a single documented lock order. Never hold an output lock
  while joining the worker.
- Every queued frame needs a generation identifier. A frame decoded before a
  seek or video replacement must be rejected even if it arrives after the
  queue was cleared.
- Variable-frame-rate files must use real presentation timestamps and durations;
  never manufacture a constant frame sequence from nominal FPS.
- Fit to Song and mapper playback speed alter the relationship between song
  time and media time. Queue lead should be measured in the same domain used by
  `MapVideoConfig::MediaTimeForSong`.
- Pausing should stop consumption and allow filling only to the normal bound.
  Resume must not dump several prepared frames through Unity in one update.
- Replay and practice seeks must prioritize responsiveness: clear stale future
  frames immediately and refill from the new time rather than draining the old
  queue.
- Hardware-to-software fallback must create a new generation and rebuild the
  queue without allowing a frame from the failed backend to reach Unity.
- End-of-stream should park cleanly. Looping should clear the terminal state,
  seek once, and refill without mixing end and beginning timestamps.

## Automatic Performance interaction

The controller should continue evaluating throughout playback. A temporary
queue reserve may mask a short decoder spike by design, so distinguish these
signals:

- **Visible frame loss:** the authoritative user-facing failure signal.
- **Queue underrun:** no due prepared frame was available; useful as an early
  warning and diagnostic counter.
- **Queue depth/lead:** shows whether the reserve is recovering or steadily
  draining.
- **Decode throughput/latency:** identifies whether the producer can refill in
  time.

Automatic Performance should not reduce FPS merely because queue depth briefly
dips while every frame is still delivered. Sustained underruns or the existing
visible-loss threshold remain appropriate triggers. When FPS changes, discard
only future entries that are no longer eligible, preserve due frames when safe,
and refill under the new cadence without reopening FFmpeg.

## Diagnostics required before enabling by default

Add low-overhead counters that are sampled in memory and logged only at existing
safe boundaries:

- current and minimum queue depth;
- current buffered media milliseconds;
- queue underrun count;
- obsolete frames discarded because the song clock passed them;
- frames discarded after seek/generation changes;
- high-water RGBA bytes and buffer-allocation count;
- time spent waiting for queue space versus decoding/converting;
- number of visible misses avoided while the reserve was non-empty, if this can
  be computed without guessing.

The live panel should not be expanded until the measurements prove useful. The
append-only performance log is the better first location. Any CSV schema change
must use the existing legacy-file preservation behavior.

## Test plan

Host tests should cover:

- queue ordering and due-frame selection;
- no early presentation of future frames;
- generation invalidation after forward/backward seeks;
- variable frame durations;
- FPS-limited eligibility for 60-to-30 and 60-to-15 playback;
- Fit to Song rates above and below 1.0;
- queue bounds at 720p, 1080p, and 1440p;
- pause/resume, loop wrap, end-of-stream, and shutdown while full/empty;
- hardware-fallback generation replacement;
- exact buffer recycling with no allocation growth after warmup.

Quest testing should compare identical maps with buffering off/on and record:

- visible frame loss and queue underruns;
- average/peak decode-and-convert latency;
- Quest gameplay FPS;
- decoder and whole-process CPU time;
- battery current/charge consumption;
- peak memory at 1080p and 1440p;
- Replay playback, practice-speed changes, pause/resume, and rapid scrubbing.

The feature is successful only if brief stalls produce fewer visible misses
without meaningful steady-state CPU/battery growth, unsafe memory retention, or
new synchronization drift. If the queue merely postpones failure under a
sustained overload, Automatic Performance remains the correct response.
