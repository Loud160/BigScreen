// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace BigScreen {
    /// Decoder output-height convention shared by both private FFmpeg
    /// backends. Zero (and any negative value supplied by a test/tool) means
    /// preserve the source resolution; positive values remain available for
    /// deliberately bounded utility previews such as the thumbnail picker.
    inline constexpr int UncappedOutputHeight = 0;


    enum class VideoFrameStorage : std::uint8_t {
        Rgba32,
        Yuv420Planar,
        Yuv420PackedAtlas
    };

    enum class GpuYuvUploadLayout : std::uint8_t {
        ThreePlane,
        PackedAtlas
    };

    enum class VideoColorMatrix : std::uint8_t {
        Bt601,
        Bt709,
        Fcc,
        Smpte240,
        Bt2020
    };

    /// ABI-neutral Cinema picture processing passed into either FFmpeg
    /// backend. The CPU path applies it after RGBA conversion; the GPU path
    /// carries the same snapshot with each frame and evaluates it during the
    /// single YUV-to-RGBA conversion pass.
    struct FrameVisualEffects {
        bool enabled = false;
        float brightness = 1.0f;
        float contrast = 1.0f;
        float saturation = 1.0f;
        float hue = 0.0f;
        float exposure = 1.0f;
        float gamma = 1.0f;
        bool vignetteEnabled = false;
        bool vignetteElliptical = false;
        float vignetteRadius = 1.0f;
        float vignetteSoftness = 0.005f;
    };

    /// An ABI-neutral decoded image ready for either the established RGBA
    /// Texture2D upload or the experimental GPU YUV conversion path. The YUV
    /// representation is tightly packed 8-bit 4:2:0. The established layout
    /// uses independent Y/U/V planes; the optional atlas places Y above
    /// side-by-side U/V regions so Unity needs one upload rather than three.
    /// Both are written by the decoder worker and expose no FFmpeg structures
    /// across the separately linked backend boundary.
    struct VideoFrame {
        VideoFrameStorage storage = VideoFrameStorage::Rgba32;
        std::vector<std::uint8_t> rgba;
        std::vector<std::uint8_t> y;
        std::vector<std::uint8_t> u;
        std::vector<std::uint8_t> v;
        std::vector<std::uint8_t> packedYuv;
        int width = 0;
        int height = 0;
        int sourceWidth = 0;
        int sourceHeight = 0;
        int displayQuarterTurns = 0;
        VideoColorMatrix colorMatrix = VideoColorMatrix::Bt601;
        bool fullRange = false;
        FrameVisualEffects visualEffects{};
        double presentationSeconds = 0.0;
        // The container-provided duration is used when available so variable-
        // frame-rate video does not inherit one guessed duration for every
        // frame. nominalFrameSeconds_ remains the fallback for incomplete MP4
        // timing data.
        double durationSeconds = 0.0;
        // Incremented by an explicit Restart. PlaybackSession rejects output
        // from an older generation so a frame that finished decoding while
        // the loop request was being posted cannot satisfy new-loop readiness.
        std::uint64_t generation = 0;
    };

    /// Snapshot of the experimental GPU YUV queue. Counts describe reserve
    /// transitions rather than Unity presentation misses: they let the
    /// append-only performance log show whether a larger memory budget kept
    /// decoded pictures available during short decoder stalls.
    struct DecoderReadAheadDiagnostics {
        std::uint64_t byteBudget = 0;
        std::uint64_t frameCapacity = 0;
        std::uint64_t currentQueuedFrames = 0;
        std::uint64_t peakQueuedFrames = 0;
        std::uint64_t lowReserveEvents = 0;
        std::uint64_t emptyQueueEvents = 0;
        std::uint64_t catchUpPresentations = 0;
        std::uint64_t forcedLateDrops = 0;
        std::uint64_t peakDueFrameBacklog = 0;
    };

    /// Session totals for one prepared output picture. CPU time covers the
    /// decoder worker's demux/decode/copy work. Wait time is wall time not
    /// charged to that worker, including asynchronous MediaCodec waits and
    /// thread descheduling. Keeping totals instead of an EMA lets the facade
    /// merge a handled hardware-to-software fallback without distorting the
    /// true session average or peak.
    struct FramePreparationDiagnostics {
        std::uint64_t sampleCount = 0;
        std::uint64_t cpuNanoseconds = 0;
        std::uint64_t peakCpuNanoseconds = 0;
        std::uint64_t waitNanoseconds = 0;
        std::uint64_t peakWaitNanoseconds = 0;
    };

    /// ABI-neutral contract shared by the separately compiled FFmpeg 4.4 and
    /// FFmpeg 9 decoder implementations. No FFmpeg type crosses this boundary:
    /// each implementation is compiled with the exact headers matching its
    /// own uniquely named runtime libraries.
    class FrameDecoderBackend {
    public:
        virtual ~FrameDecoderBackend() = default;
        virtual bool Open(
            const std::filesystem::path& videoPath,
            int maximumOutputHeight,
            bool preferHardwareDecoding,
            bool preferGpuConversion,
            GpuYuvUploadLayout gpuYuvUploadLayout,
            std::size_t readAheadByteBudget,
            void* javaVm,
            const FrameVisualEffects& visualEffects,
            std::string& error) = 0;
        virtual void Close() = 0;
        /// Signals the decoder without waiting. The facade uses this before a
        /// short bounded wait so Unity never performs an unbounded worker join.
        virtual void RequestStop() = 0;
        /// Waits only for the worker-exited signal; resource destruction still
        /// belongs to Close() on either the caller or retirement worker.
        virtual bool WaitForWorkerStop(
            std::chrono::milliseconds timeout) = 0;
        virtual void Request(
            double mediaSeconds,
            double presentationIntervalSeconds) = 0;
        /// Starts a new presentation pass at the requested media position.
        /// Unlike an ordinary clock request, this clears any completed-frame
        /// output and forces the worker to flush/seek even when the new time
        /// happens to fall inside the last decoded frame. Library preview
        /// looping uses this after EOF so stale completion state cannot be
        /// mistaken for a newly decoded opening picture.
        virtual std::uint64_t Restart(double mediaSeconds) = 0;
        /// Replaces CPU-side Cinema picture processing without reopening the
        /// stream. The decoder worker snapshots the small settings structure
        /// before touching a frame, so a ten-second test phase can change
        /// color correction or vignette safely while MediaCodec/FFmpeg stays
        /// warm.
        virtual void UpdateVisualEffects(
            const FrameVisualEffects& visualEffects) = 0;
        /// Enables or disables future YUV-plane output without reopening the
        /// codec. Disabling also discards a queued YUV frame so a failed Unity
        /// GPU setup can request a fresh RGBA frame through the proven path.
        virtual void SetGpuConversionEnabled(bool enabled) = 0;
        virtual void SetGpuYuvUploadLayout(
            GpuYuvUploadLayout layout) = 0;
        virtual GpuYuvUploadLayout ActiveGpuYuvUploadLayout() const = 0;
        virtual std::string GpuYuvUploadFallbackReason() const = 0;
        virtual bool GpuConversionOutputEnabled() const = 0;
        virtual std::string GpuConversionFallbackReason() const = 0;
        virtual bool TryTake(
            double mediaSeconds,
            VideoFrame& destination) = 0;
        virtual void Recycle(VideoFrame&& frame) = 0;
        virtual std::optional<std::string> TakeError() = 0;
        virtual bool IsOpen() const = 0;
        virtual int Width() const = 0;
        virtual int Height() const = 0;
        virtual int SourceWidth() const = 0;
        virtual int SourceHeight() const = 0;
        virtual double SourceFramesPerSecond() const = 0;
        virtual FramePreparationDiagnostics PreparationDiagnostics() const = 0;
        virtual void ResetPreparationDiagnostics() = 0;
        virtual double WorkerCpuMilliseconds() const = 0;
        virtual double DurationSeconds() const = 0;
        virtual std::uint64_t BufferAllocations() const = 0;
        virtual DecoderReadAheadDiagnostics ReadAheadDiagnostics() const = 0;
        virtual const char* RuntimeVersion() const = 0;
        virtual const char* CodecName() const = 0;
        /// Reports what actually decoded the current file. This deliberately
        /// does not mirror the preference setting because MediaCodec can fall
        /// back to software for unsupported or failing content.
        virtual bool UsingHardwareDecoder() const = 0;
        /// Explains a handled startup fallback. An empty value means hardware
        /// was not requested or it opened successfully.
        virtual std::string HardwareFallbackReason() const = 0;
        /// False for HEVC and for every source above the 1080p tier. The
        /// facade consults this after a mid-stream MediaCodec failure so it
        /// never reopens a file through a prohibited software decoder.
        virtual bool SoftwareFallbackAllowed() const = 0;
        virtual std::string SoftwareFallbackBlockedReason() const = 0;
    };

#if defined(__GNUC__) || defined(__clang__)
#define BIGSCREEN_FFMPEG_BACKEND_EXPORT __attribute__((visibility("default")))
#else
#define BIGSCREEN_FFMPEG_BACKEND_EXPORT
#endif

    // These are the only symbols exported by the two decoder backend shared
    // libraries. The object is destroyed through its virtual destructor, so
    // no FFmpeg-owned allocation or public structure crosses the boundary.
    BIGSCREEN_FFMPEG_BACKEND_EXPORT
    std::unique_ptr<FrameDecoderBackend> CreateFrameDecoder44Backend();
    BIGSCREEN_FFMPEG_BACKEND_EXPORT
    std::unique_ptr<FrameDecoderBackend> CreateFrameDecoder9Backend();

#if defined(BIGSCREEN_NATIVE_FFMPEG_IMPLEMENTATION) || \
    defined(BIGSCREEN_SINGLE_FFMPEG)

    /// Externally-clocked FFmpeg decoder.
    ///
    /// Big Screen intentionally does not run its own playback clock. Beat Saber
    /// supplies a requested media timestamp every frame; Replay supplies the
    /// same timestamps while rendering at a fixed simulation step. CPU RGBA
    /// output retains a one-frame mailbox. The experimental GPU YUV path keeps
    /// a short bounded queue whose pictures remain gated by the same external
    /// clock, so neither path can run ahead visibly or block gameplay.
    class FrameDecoder final : public FrameDecoderBackend {
    public:
        FrameDecoder() = default;
        ~FrameDecoder() override;

        FrameDecoder(const FrameDecoder&) = delete;
        FrameDecoder& operator=(const FrameDecoder&) = delete;

        /// Opens the source and converts decoded frames to no more than the
        /// requested output height. A non-positive height preserves the native
        /// source dimensions; sources below a positive bound are never enlarged.
        bool Open(
            const std::filesystem::path& videoPath,
            int maximumOutputHeight,
            bool preferHardwareDecoding,
            bool preferGpuConversion,
            GpuYuvUploadLayout gpuYuvUploadLayout,
            std::size_t readAheadByteBudget,
            void* javaVm,
            const FrameVisualEffects& visualEffects,
            std::string& error) override;
        /// Host fixture tests exercise the proven software path without an
        /// Android VM. Keep their call site explicit and source-compatible.
        bool Open(
            const std::filesystem::path& videoPath,
            int maximumOutputHeight,
            std::string& error)
        {
            return Open(
                videoPath,
                maximumOutputHeight,
                false,
                false,
                GpuYuvUploadLayout::ThreePlane,
                64u * 1024u * 1024u,
                nullptr,
                {},
                error);
        }
        void Close() override;
        void RequestStop() override;
        bool WaitForWorkerStop(std::chrono::milliseconds timeout) override;

        /// Publishes the newest externally-clocked target. The worker may
        /// intentionally coalesce obsolete targets so playback stays current.
        void Request(
            double mediaSeconds,
            double presentationIntervalSeconds) override;
        std::uint64_t Restart(double mediaSeconds) override;
        void UpdateVisualEffects(
            const FrameVisualEffects& visualEffects) override;
        void SetGpuConversionEnabled(bool enabled) override;
        void SetGpuYuvUploadLayout(
            GpuYuvUploadLayout layout) override;
        GpuYuvUploadLayout ActiveGpuYuvUploadLayout() const override {
            return gpuYuvUploadLayout_.load();
        }
        std::string GpuYuvUploadFallbackReason() const override {
            std::scoped_lock lock(gpuConversionStatusMutex_);
            return gpuYuvUploadFallbackReason_;
        }
        bool GpuConversionOutputEnabled() const override {
            return gpuConversionEnabled_.load();
        }
        std::string GpuConversionFallbackReason() const override {
            std::scoped_lock lock(gpuConversionStatusMutex_);
            return gpuConversionFallbackReason_;
        }
        bool TryTake(
            double mediaSeconds,
            VideoFrame& destination) override;
        /// Returns consumed RGBA or plane allocations to the decoder. Keeping
        /// a small pool avoids allocating and freeing multi-megabyte frame
        /// buffers for every presented picture.
        void Recycle(VideoFrame&& frame) override;
        /// Moves the first decoder-worker failure to the main thread. The
        /// caller can stop only the video and defer UI until gameplay ends.
        std::optional<std::string> TakeError() override;

        bool IsOpen() const override { return open_.load(); }
        int Width() const override { return width_; }
        int Height() const override { return height_; }
        int SourceWidth() const override { return sourceWidth_; }
        int SourceHeight() const override { return sourceHeight_; }
        double SourceFramesPerSecond() const override {
            return nominalFrameSeconds_ > 0.0 ? 1.0 / nominalFrameSeconds_ : 0.0;
        }
        FramePreparationDiagnostics PreparationDiagnostics() const override;
        void ResetPreparationDiagnostics() override;
        /// CPU time consumed by Big Screen's owned decoder worker since this
        /// decoder was opened. Unlike wall-clock decode latency, sleeping while
        /// waiting for a timestamp does not increase this value.
        double WorkerCpuMilliseconds() const override {
            return workerCpuNanoseconds_.load() / 1'000'000.0;
        }
        double DurationSeconds() const override { return durationSeconds_; }
        std::uint64_t BufferAllocations() const override { return bufferAllocations_.load(); }
        DecoderReadAheadDiagnostics ReadAheadDiagnostics() const override;
        const char* RuntimeVersion() const override;
        const char* CodecName() const override { return codecName_.c_str(); }
        bool UsingHardwareDecoder() const override { return usingHardwareDecoder_; }
        std::string HardwareFallbackReason() const override {
            return hardwareFallbackReason_;
        }
        bool SoftwareFallbackAllowed() const override {
            return softwareFallbackAllowed_;
        }
        std::string SoftwareFallbackBlockedReason() const override {
            return softwareFallbackBlockedReason_;
        }

    private:
        void WorkerMain() noexcept;
        static int InterruptFfmpegIo(void* opaque);
        void WorkerLoop(std::uint64_t cpuStartedNanoseconds);
        void SetWorkerError(std::string message);
        /// Reads one decoded picture and distinguishes ordinary end-of-stream
        /// from a codec/demux failure. The worker uses the EOF signal to park
        /// beyond the final frame instead of repeatedly seeking and decoding
        /// the final GOP for every later song-time request.
        bool ReadDecodedFrame(bool& reachedEndOfStream);
        bool SeekNear(double mediaSeconds, std::string& error);
        double CurrentFrameTime() const;
        double CurrentFrameDuration() const;
        bool ConvertCurrentFrame(VideoFrame& destination, std::string& error);
        bool CopyCurrentFrameAsYuv420(VideoFrame& destination);
        bool CopyCurrentFrameAsPackedYuv420(VideoFrame& destination);
        void FinalizeYuvFrame(
            VideoFrame& destination,
            VideoFrameStorage storage);
        void ApplyVisualEffects(VideoFrame& destination);
        void RebuildVisualEffectCache(
            const FrameVisualEffects& effects,
            int width,
            int height);
        void Publish(
            VideoFrame&& frame,
            double dueMediaSeconds,
            std::uint64_t decodeEpoch);
        VideoFrame AcquireOutputFrame();
        void RecycleFrameLocked(VideoFrame&& frame);
        void FlushQueuedFramesLocked();
        std::size_t ReadAheadQueueLimit(const VideoFrame& frame) const;
        std::size_t ReadAheadQueueLimitForBytes(std::size_t bytes) const;
        void UpdateReadAheadHealthAfterConsumeLocked();

        AVFormatContext* format_ = nullptr;
        AVCodecContext* codec_ = nullptr;
        AVFrame* decoded_ = nullptr;
        AVPacket* packet_ = nullptr;
        SwsContext* converter_ = nullptr;
        int converterSourceWidth_ = 0;
        int converterSourceHeight_ = 0;
        int converterSourceFormat_ = -1;
        int converterColorSpace_ = -1;
        int converterColorRange_ = -1;

        int videoStream_ = -1;
        int width_ = 0;
        int height_ = 0;
        int sourceWidth_ = 0;
        int sourceHeight_ = 0;
        int conversionWidth_ = 0;
        int conversionHeight_ = 0;
        int displayQuarterTurns_ = 0;
        std::vector<std::uint8_t> rotationScratch_;
        bool gpuConversionRequested_ = false;
        std::atomic<bool> gpuConversionEnabled_{false};
        std::atomic<bool> gpuConversionRejected_{false};
        std::atomic<GpuYuvUploadLayout> gpuYuvUploadLayout_{
            GpuYuvUploadLayout::ThreePlane};
        mutable std::mutex gpuConversionStatusMutex_;
        std::string gpuConversionFallbackReason_;
        std::string gpuYuvUploadFallbackReason_;
        double streamTimeBase_ = 0.0;
        double nominalFrameSeconds_ = 1.0 / 30.0;
        double durationSeconds_ = 0.0;
        // avcodec_send_packet() returning EAGAIN means the packet was not
        // consumed. Keep that AVPacket referenced across ReadDecodedFrame()
        // calls until libavcodec accepts it; dropping it creates a permanent
        // hole in the video and can make MediaCodec appear to end early.
        bool compressedPacketPending_ = false;
        // Once the demuxer reaches EOF, a single null packet starts codec
        // draining. Continue receiving until AVERROR_EOF instead of treating
        // an asynchronous MediaCodec EAGAIN as the end of the video.
        bool decoderDraining_ = false;

        std::thread worker_;
        std::atomic<bool> open_{false};
        std::atomic<bool> stopWorker_{false};
        std::atomic<std::int64_t> openDeadlineNanoseconds_{0};
        std::atomic<bool> workerExited_{true};
        std::mutex workerExitMutex_;
        std::condition_variable workerExitedChanged_;
        // True session totals for prepared output pictures. Unlike the old
        // exponential wall-clock average, these counters cannot be dominated
        // by one late MediaCodec wait at the end of a map.
        mutable std::mutex preparationDiagnosticsMutex_;
        std::uint64_t preparationSampleCount_ = 0;
        std::uint64_t preparationCpuNanoseconds_ = 0;
        std::uint64_t peakPreparationCpuNanoseconds_ = 0;
        std::uint64_t preparationWaitNanoseconds_ = 0;
        std::uint64_t peakPreparationWaitNanoseconds_ = 0;
        std::atomic<std::uint64_t> workerCpuNanoseconds_{0};
        // Counts reusable output frame sets whose backing storage grows, not
        // ordinary reuse. Planar Y/U/V growth counts once for the set so this
        // remains comparable with RGBA and packed-atlas output.
        std::atomic<std::uint64_t> bufferAllocations_{0};
        std::size_t readAheadByteBudget_ = 64u * 1024u * 1024u;
        std::mutex requestMutex_;
        std::condition_variable requestChanged_;
        double requestedSeconds_ = 0.0;
        // Media-time spacing between presentation slots. PlaybackSession
        // derives this from playback speed and the active FPS ceiling so the
        // read-ahead worker does not prepare pictures the limiter will skip.
        double requestedPresentationIntervalSeconds_ = 0.0;
        std::uint64_t requestVersion_ = 0;
        std::uint64_t presentationGeneration_ = 0;
        // Any seek, restart, output-format transition, or CPU-side effect
        // replacement increments this epoch. A conversion already in flight
        // may finish, but Publish rejects it instead of leaking stale output
        // into the replacement queue.
        std::atomic<std::uint64_t> decodeEpoch_{0};
        // Protected by requestMutex_. It remains set across ordinary Request
        // calls until the worker consumes it, so a fast Unity Tick cannot
        // overwrite the mandatory EOF rewind before the worker wakes.
        bool restartPending_ = false;
        mutable std::mutex outputMutex_;
        struct QueuedFrame {
            VideoFrame frame;
            // The source PTS remains in frame.presentationSeconds. This
            // separate due time preserves the active presentation cap: a
            // prefetched source picture cannot be uploaded before the future
            // song-clock slot for which it was selected.
            double dueMediaSeconds = 0.0;
            std::uint64_t decodeEpoch = 0;
        };
        std::deque<QueuedFrame> queuedFrames_;
        std::vector<VideoFrame> recycledFrames_;
        bool allowEarlyOpeningFrame_ = true;
        std::size_t lastReadAheadFrameBytes_ = 0;
        std::size_t readAheadFrameCapacity_ = 0;
        std::size_t readAheadPeakQueuedFrames_ = 0;
        std::uint64_t readAheadLowReserveEvents_ = 0;
        std::uint64_t readAheadEmptyQueueEvents_ = 0;
        std::uint64_t readAheadCatchUpPresentations_ = 0;
        std::uint64_t readAheadForcedLateDrops_ = 0;
        std::size_t readAheadPeakDueFrameBacklog_ = 0;
        bool readAheadPresentationStarted_ = false;
        bool readAheadLowReserveActive_ = false;
        bool readAheadEmptyActive_ = false;

        std::mutex errorMutex_;
        std::optional<std::string> workerError_;
        bool usingHardwareDecoder_ = false;
        std::string hardwareFallbackReason_;
        bool softwareFallbackAllowed_ = true;
        std::string softwareFallbackBlockedReason_;
        std::string codecName_ = "unknown";
        mutable std::mutex visualEffectsMutex_;
        FrameVisualEffects visualEffects_{};

        /// Worker-owned lookup data for mapper color correction and vignette.
        /// Cinema evaluates these operations in a GPU shader on PC. Quest does
        /// not currently ship Big Screen's own shader bundle, so doing the
        /// equivalent work on decoded RGBA pictures remains the compatible
        /// fallback. Building the expensive gamma and vignette curves only
        /// when settings or dimensions change prevents each video frame from
        /// repeating millions of pow(), sqrt(), and smooth-step operations.
        struct VisualEffectCache {
            bool valid = false;
            bool colorCorrection = false;
            bool vignette = false;
            int width = 0;
            int height = 0;
            FrameVisualEffects effects{};
            std::array<std::array<std::array<std::int32_t, 256>, 3>, 3>
                colorContribution{};
            std::array<std::int32_t, 3> colorBias{};
            std::array<std::uint8_t, 4097> gamma{};
            std::vector<std::uint8_t> vignetteMask;
        } visualEffectCache_;
    };
#else
    /// Runtime-selecting facade used by gameplay and menu previews. Selecting
    /// another FFmpeg version affects the next Open call; no decoder is ever
    /// replaced while its worker owns codec state.
    class FrameDecoder final {
    public:
        FrameDecoder() = default;
        ~FrameDecoder();
        FrameDecoder(const FrameDecoder&) = delete;
        FrameDecoder& operator=(const FrameDecoder&) = delete;

        bool Open(
            const std::filesystem::path& videoPath,
            int maximumOutputHeight,
            bool preferGpuConversion,
            const FrameVisualEffects& visualEffects,
            std::string& error);
        bool Open(
            const std::filesystem::path& videoPath,
            int maximumOutputHeight,
            std::string& error)
        {
            return Open(videoPath, maximumOutputHeight, false, {}, error);
        }
        void Close();
        void Request(
            double mediaSeconds,
            double presentationIntervalSeconds = 0.0);
        /// Clears stale decoded output and forces the active backend to begin
        /// a fresh pass at mediaSeconds without replacing the Unity surface.
        std::uint64_t Restart(double mediaSeconds);
        void UpdateVisualEffects(const FrameVisualEffects& visualEffects);
        void SetGpuConversionEnabled(bool enabled);
        void SetGpuYuvUploadLayout(GpuYuvUploadLayout layout);
        GpuYuvUploadLayout ActiveGpuYuvUploadLayout() const;
        std::string GpuYuvUploadFallbackReason() const;
        bool GpuConversionOutputEnabled() const;
        std::string GpuConversionFallbackReason() const;
        bool TryTake(VideoFrame& destination);
        bool TryTake(double mediaSeconds, VideoFrame& destination);
        void Recycle(VideoFrame&& frame);
        std::optional<std::string> TakeError();

        bool IsOpen() const;
        int Width() const;
        int Height() const;
        int SourceWidth() const;
        int SourceHeight() const;
        double SourceFramesPerSecond() const;
        double AverageDecodeMilliseconds() const;
        double PeakDecodeMilliseconds() const;
        double AverageWorkerWaitMilliseconds() const;
        double PeakWorkerWaitMilliseconds() const;
        /// Starts a new measurement interval after decoder prewarm. Unlike the
        /// former live-panel peak reset, this is called once at session start;
        /// the reported average and peak then cover the complete interval.
        void ResetPreparationDiagnostics();
        /// Wall-clock time spent selecting and opening the active backend.
        /// Persisted only to performance-history.log; it is intentionally not
        /// part of the live panel because startup is a one-time event.
        double OpenMilliseconds() const { return openMilliseconds_; }
        double WorkerCpuMilliseconds() const;
        double DurationSeconds() const;
        std::uint64_t BufferAllocations() const;
        DecoderReadAheadDiagnostics ReadAheadDiagnostics() const;
        const char* RuntimeVersion() const;
        const char* CodecName() const;
        bool UsingHardwareDecoder() const;
        /// Reports how pictures are decoded (MediaCodec or CPU software). This
        /// is distinct from RuntimeVersion(), which identifies FFmpeg 4.4/9.
        const char* DecodeMethodName() const;

    private:
        std::unique_ptr<FrameDecoderBackend> CreateSelectedBackend() const;
        void CloseAndRetainBackendMetrics();
        bool ReopenWithSoftwareAfterHardwareFailure(
            const std::string& hardwareError,
            std::string& recoveryError);

        std::unique_ptr<FrameDecoderBackend> backend_;
        std::filesystem::path videoPath_;
        int maximumOutputHeight_ = 0;
        double lastRequestedSeconds_ = 0.0;
        double lastPresentationIntervalSeconds_ = 0.0;
        std::size_t readAheadByteBudget_ = 64u * 1024u * 1024u;
        bool useFfmpeg9_ = false;
        bool hardwareRequested_ = false;
        bool gpuConversionRequested_ = false;
        GpuYuvUploadLayout gpuYuvUploadLayout_ =
            GpuYuvUploadLayout::ThreePlane;
        bool hardwareFallbackAttempted_ = false;
        void* javaVm_ = nullptr;
        FrameVisualEffects visualEffects_{};
        double accumulatedWorkerCpuMilliseconds_ = 0.0;
        std::uint64_t accumulatedBufferAllocations_ = 0;
        DecoderReadAheadDiagnostics retainedReadAheadDiagnostics_{};
        FramePreparationDiagnostics retainedPreparationDiagnostics_{};
        double openMilliseconds_ = 0.0;
    };
#endif
}

#undef BIGSCREEN_FFMPEG_BACKEND_EXPORT
