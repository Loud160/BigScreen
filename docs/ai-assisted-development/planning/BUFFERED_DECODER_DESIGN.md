# Buffered Decoder Design Note

## Status

The first bounded implementation now exists on the experimental GPU YUV path
of `codex/major-feature-development`. It is not enabled independently: GPU Video
Conversion is now the default-on switch for both YUV transport and read-ahead.
CPU RGBA and thumbnail decoding retain their one-frame behavior. Quest 3/3S
measurement is still required before this is considered stable-release ready.
Queue-capacity and low/empty-transition diagnostics now exist in the append-only
performance log.

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
- The CPU worker follows the newest timestamp request, decodes intermediate
  reference pictures sequentially, converts the selected picture to RGBA, and
  publishes one newest-frame mailbox.
- That CPU mailbox deliberately replaces an obsolete unconsumed picture. The
  GPU path now uses the bounded queue described below.
- The presentation FPS preference is a ceiling. A 60 FPS source under a 30 FPS
  limit must not convert or enqueue all 60 source pictures.
- MediaCodec buffers are released promptly after conversion. Holding Android
  hardware-decoder output surfaces/buffers can exhaust the codec and stall the
  pipeline.

Simply allowing the existing worker to run to end-of-stream would not create a
usable buffer. Its single mailbox would repeatedly overwrite pictures before
their presentation times and eventually contain only a much later frame.

## Implemented experimental architecture

The GPU path replaces the one-frame output mailbox with a small,
timestamp-ordered queue of fully copied `VideoFrame` YUV objects while keeping
the existing external-clock model. The CPU path was deliberately not expanded
to RGBA read-ahead because its memory cost is much higher.

1. Open and prewarm exactly as today.
2. The worker decodes sequentially and enqueues only pictures eligible under
   the effective presentation FPS limit and playback rate.
3. Each queued entry retains presentation time, duration, dimensions, its
   selected future due time, and owned reusable planar YUV buffers.
4. The Unity thread normally asks for the newest queued picture whose interval
   is due at the current authoritative media time. When exactly one older due
   picture is no more than one presentation interval late, it may present that
   picture now and retain the newer due picture for the following display
   update. This is a one-frame recovery window, not permission to build lag:
   larger or older backlogs are discarded. A future picture must remain
   queued; it must never be uploaded early.
5. After consumption, return its plane allocation to the bounded reuse pool and
   wake the producer to refill the reserve.
6. Once the memory budget or independent frame safety ceiling is reached, the
   worker sleeps instead of decoding farther ahead.
7. On a backward seek, large forward jump, Replay discontinuity, loop wrap,
   selected-video change, playback-rate change, decoder fallback, or shutdown,
   invalidate the complete queue under one synchronization contract and refill
   from the new authoritative position.

The implemented queue is bounded by a user-selectable 32–256 MiB YUV memory
budget in 16 MiB steps (64 MiB by default) and an internal 120-frame safety
ceiling. Frames are allocated only as the worker fills the reserve; the full
budget is not reserved in advance. There is deliberately no user-facing raw
frame-count setting because resolution determines each picture's memory cost.

## Memory concerns

Ready-to-upload RGBA is intentionally expensive, which is why the CPU path was
not given the same queue:

| Frame size | Approximate RGBA bytes per frame | Three frames |
|---|---:|---:|
| 1280x720 | 3.5 MiB | 10.5 MiB |
| 1920x1080 | 7.9 MiB | 23.7 MiB |
| 2560x1440 | 14.1 MiB | 42.2 MiB |

The implemented tightly packed 8-bit 4:2:0 representation is smaller:

| Frame size | Approximate YUV bytes per frame | Approximate frames in 64 MiB |
|---|---:|---:|
| 1280x720 | 1.32 MiB | 48 |
| 1920x1080 | 2.97 MiB | 21 |
| 2560x1440 | 5.27 MiB | 12 |

The total process cost must still include queued frames, the worker's copy
destination, the main-thread upload frame, and Unity's textures.
Do not count only the `deque` entries. A queue that is safe at 720p can cause
memory pressure or process termination at 1440p.

Do not retain decoded MediaCodec YUV frames merely to reduce RGBA storage.
Hardware output buffers are a limited codec resource, and retaining them can
block further decode. Converting promptly into Big Screen-owned memory and
releasing the hardware frame is the safer first implementation.

The reusable pool is capped by the same byte-derived frame capacity as the
queue, and queue plus pool cannot exceed that capacity. A flushed queue is
consumed from that pool before vectors grow again. Switching between GPU YUV and CPU RGBA explicitly
releases the inactive representation's retained vector capacities so a fallback
does not keep a complete YUV reserve beside newly allocated RGBA pictures.
Closing or replacing a video releases the pool completely.

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
- Do not hide real A/V latency by presenting an unbounded chain of late frames.
  One picture within one presentation interval may be recovered when the next
  display update can consume the newer due picture. When the queue is farther
  behind, discard obsolete queued pictures and present the best remaining due
  picture, preserving synchronization over completeness.

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

## Diagnostics before enabling by default

The first low-overhead fields are sampled in memory and logged only at the
existing completed-gameplay boundary:

- selected byte budget and calculated frame capacity;
- current/final and peak queue depth;
- transitions into the lowest quarter of the reserve;
- transitions to an empty reserve.
- bounded one-frame catch-up presentations;
- forced drops of irrecoverably late pictures;
- peak number of pictures simultaneously due at a Unity update.

Possible later measurements, if the first counters do not explain results:

- minimum queue depth;
- current buffered media milliseconds;
- queue underrun count;
- obsolete frames discarded because the song clock passed them;
- frames discarded after seek/generation changes;
- high-water RGBA bytes and buffer-allocation count;
- time spent waiting for queue space versus decoding/converting;
- number of visible misses avoided while the reserve was non-empty, if this can
  be computed without guessing.

The live panel is intentionally unchanged. The append-only performance log is
the first location, and these additions do not change a CSV schema.

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
- true session-average/peak frame-preparation thread CPU time;
- average/peak worker wait time, reported separately from processing cost;
- Quest gameplay FPS;
- decoder and whole-process CPU time;
- battery current/charge consumption;
- peak memory at 1080p and 1440p;
- Replay playback, practice-speed changes, pause/resume, and rapid scrubbing.

The feature is successful only if brief stalls produce fewer visible misses
without meaningful steady-state CPU/battery growth, unsafe memory retention, or
new synchronization drift. If the queue merely postpones failure under a
sustained overload, Automatic Performance remains the correct response.
