#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
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

    /// A decoded image ready for Unity's RGBA32 Texture2D upload path.
    struct VideoFrame {
        std::vector<std::uint8_t> rgba;
        int width = 0;
        int height = 0;
        double presentationSeconds = 0.0;
    };

    /// Externally-clocked FFmpeg decoder.
    ///
    /// Big Screen intentionally does not run its own playback clock. Beat Saber
    /// supplies a requested media timestamp every frame; Replay supplies the
    /// same timestamps while rendering at a fixed simulation step. The worker
    /// thread decodes toward the newest request and publishes a one-frame
    /// mailbox for the Unity thread to consume without blocking gameplay.
    class FrameDecoder final {
    public:
        FrameDecoder() = default;
        ~FrameDecoder();

        FrameDecoder(const FrameDecoder&) = delete;
        FrameDecoder& operator=(const FrameDecoder&) = delete;

        /// Opens the source and converts decoded frames to no more than the
        /// requested output height. Sources below that tier are never upscaled.
        bool Open(
            const std::filesystem::path& videoPath,
            int maximumOutputHeight,
            std::string& error);
        void Close();

        void Request(double mediaSeconds);
        bool TryTake(VideoFrame& destination);
        /// Moves the first decoder-worker failure to the main thread. The
        /// caller can stop only the video and defer UI until gameplay ends.
        std::optional<std::string> TakeError();

        bool IsOpen() const { return open_.load(); }
        int Width() const { return width_; }
        int Height() const { return height_; }
        int SourceWidth() const { return sourceWidth_; }
        int SourceHeight() const { return sourceHeight_; }
        double SourceFramesPerSecond() const {
            return nominalFrameSeconds_ > 0.0 ? 1.0 / nominalFrameSeconds_ : 0.0;
        }
        double AverageDecodeMilliseconds() const { return averageDecodeMilliseconds_.load(); }
        double DurationSeconds() const { return durationSeconds_; }

    private:
        void WorkerMain() noexcept;
        void WorkerLoop();
        void SetWorkerError(std::string message);
        bool ReadDecodedFrame();
        bool SeekNear(double mediaSeconds);
        double CurrentFrameTime() const;
        bool ConvertCurrentFrame(VideoFrame& destination);
        void Publish(VideoFrame&& frame);

        AVFormatContext* format_ = nullptr;
        AVCodecContext* codec_ = nullptr;
        AVFrame* decoded_ = nullptr;
        AVPacket* packet_ = nullptr;
        SwsContext* converter_ = nullptr;

        int videoStream_ = -1;
        int width_ = 0;
        int height_ = 0;
        int sourceWidth_ = 0;
        int sourceHeight_ = 0;
        double streamTimeBase_ = 0.0;
        double nominalFrameSeconds_ = 1.0 / 30.0;
        double durationSeconds_ = 0.0;

        std::thread worker_;
        std::atomic<bool> open_{false};
        std::atomic<bool> stopWorker_{false};
        std::atomic<double> averageDecodeMilliseconds_{0.0};

        std::mutex requestMutex_;
        std::condition_variable requestChanged_;
        double requestedSeconds_ = 0.0;
        std::uint64_t requestVersion_ = 0;

        std::mutex outputMutex_;
        VideoFrame newestFrame_;
        bool frameWaiting_ = false;

        std::mutex errorMutex_;
        std::optional<std::string> workerError_;
    };
}
