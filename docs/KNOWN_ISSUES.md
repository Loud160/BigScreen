# Alpha status and known limitations

Big Screen 0.7.0-alpha.13 is an alpha release candidate for Beat Saber
1.40.8 (`1.40.8_7379`). It completed the nine-stage independent code-review
remediation pass, the complete host and ARM64/QMOD validation pipeline, focused
Quest 2 regression testing, and several days of combined use with Saber Stage
and Qavatars without a reported regression. Community testing on Quest 3 and
Quest 3S has not identified a headset-specific problem.

This is sufficient for another public alpha. It is not a claim that every map,
video, mod combination, or long-running headset session has been tested.

Last reviewed: September 6, 2026

## Known limitations

- The QMOD is specific to Beat Saber 1.40.8 (`1.40.8_7379`). It must not be
  installed on another game version.
- Multiplayer integration is intentionally deferred. Big Screen does not claim
  to coordinate videos, screen state, or downloads between multiplayer users.
- Mapper `bloom` and `colorBlending` fields are parsed but intentionally have
  no runtime effect. Both active video-material paths suppress video bloom
  emission to avoid the solid-white screen failure seen during the earlier
  Cinema bloom experiment.
- The disabled Cinema bloom renderer, camera hook, soft-additive path, and
  diagnostic controls remain preserved behind the default-off
  `BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM` build gate. They do not run or
  appear in release builds and must not be re-enabled without a separate branch
  and a complete Bloom-on/Bloom-off headset matrix.
- GPU Video Conversion supports the tested 8-bit SDR YUV420P, YUVJ420P, and NV12
  paths. Unsupported pixel layouts, color matrices, or unavailable Unity GPU
  resources fall back to CPU RGBA for that playback session.
- Cinema placement, curvature, additional screens, color correction, vignette,
  environment instructions, and Chroma cooperation are implemented and have
  received focused Quest testing. Wider mapper coverage is still needed before
  Big Screen can claim complete PC Cinema visual parity.
- YouTube can change its extraction and anti-bot requirements independently of
  Big Screen. The bundled yt-dlp updater, stable/nightly channel controls, HLS
  recovery, remuxing, and last-resort transcoding reduce failures but cannot
  guarantee that every public video remains downloadable.
- Hardware decoding remains dependent on the video's codec, profile, container,
  and Quest MediaCodec support. Big Screen reports fallback or preparation
  failures and can use software paths where supported, but high-resolution or
  high-frame-rate video may still exceed a headset's practical performance.
- **Frames Skipped is a presentation-deadline measurement, not a count of
  decoder failures.** Big Screen compares actual uploaded pictures with
  source-aware deadlines derived from song time, source FPS, playback speed,
  and the active FPS cap. Beat Saber's Unity render loop runs independently of
  that video cadence, and its effective frame rate can vary during a session.
  A decoded and queued picture can therefore become available between game
  frames and miss its exact presentation opportunity before Unity can upload
  it. The cadence mismatch is more visible with 60 FPS video because its
  deadlines are only about 16.67 ms apart; they do not consistently align with
  72, 80, 90, 120, or temporarily reduced game-frame timing. This can produce a
  higher skipped-frame total even when decoder CPU time is low and playback
  appears smooth. Buffering and bounded late-frame tolerance reduce the effect,
  but Big Screen cannot eliminate it without control of Beat Saber's render
  schedule. Judge the counter together with visible smoothness, gameplay FPS,
  queue state, and decoder/presentation timing rather than treating it alone as
  proof that decoding failed.

## Validated alpha scope

The alpha-13 remediation and regression work covered:

- repeated Video Library entry, filtering, letter jumps, editor return,
  thumbnail arrival, selection reuse, and incremental large-catalog loading;
- YouTube checking and downloads, cancellation, direct-DASH validation,
  HLS/MPEG-TS recovery, remuxing, transcoding prompts, local assignment, storage,
  and error recovery;
- menu preview, natural completion and looping, seeking, offset, playback speed,
  Fit to Song, lead-in, and non-disruptive screen/timing adjustments;
- flat, curved, mapper-authored, additional, Chroma, and Showcase surfaces,
  including GPU conversion and buffered presentation paths;
- Solo and Campaign entry, restart, map completion/exit, Practice/Replay clock
  changes, and corrected performance reporting;
- both FFmpeg runtimes and hardware/software fallback behavior where compatible
  test media was available;
- first-party logging, dependency diagnostics, source deployment/removal,
  deterministic packaging, and support-log collection; and
- coexistence testing with Saber Stage and Qavatars, plus community use on
  Quest 3 and Quest 3S.

The permanent [release checklist](RELEASE_CHECKLIST.md) remains the publication
gate for each tagged build. New defects should be reported with the generated
Big Screen support archive whenever possible.
