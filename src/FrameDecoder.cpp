#include "BigScreen/FrameDecoder.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/pixfmt.h"
#include "libavutil/version.h"
#include "libswscale/swscale.h"
}

namespace BigScreen {
    namespace {
        std::string FfmpegError(int code)
        {
            char buffer[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(code, buffer, sizeof(buffer));
            return buffer;
        }
    }

    FrameDecoder::~FrameDecoder()
    {
        Close();
    }

    bool FrameDecoder::Open(
        const std::filesystem::path& videoPath,
        int maximumOutputHeight,
        std::string& error)
    {
        Close();
        error.clear();
        {
            std::scoped_lock lock(errorMutex_);
            workerError_.reset();
        }

        int result = avformat_open_input(&format_, videoPath.string().c_str(), nullptr, nullptr);
        if(result < 0)
        {
            error = "FFmpeg could not open the MP4: " + FfmpegError(result);
            Close();
            return false;
        }

        result = avformat_find_stream_info(format_, nullptr);
        if(result < 0)
        {
            error = "FFmpeg could not read the MP4 stream table: " + FfmpegError(result);
            Close();
            return false;
        }

        // av_find_best_stream accounts for containers with artwork or multiple
        // video tracks and returns the codec matching the selected stream.
#if LIBAVCODEC_VERSION_MAJOR >= 59
        const AVCodec* decoder = nullptr;
#else
        AVCodec* decoder = nullptr;
#endif
        videoStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if(videoStream_ < 0 || !decoder)
        {
            error = "The MP4 does not contain a decodable video stream";
            Close();
            return false;
        }

        AVStream* stream = format_->streams[videoStream_];
        codec_ = avcodec_alloc_context3(decoder);
        if(!codec_)
        {
            error = "FFmpeg could not allocate a decoder context";
            Close();
            return false;
        }

        result = avcodec_parameters_to_context(codec_, stream->codecpar);
        if(result < 0)
        {
            error = "FFmpeg could not apply video stream parameters: " + FfmpegError(result);
            Close();
            return false;
        }

        // Two frame threads provide useful H.264 parallelism on Quest without
        // taking every core away from gameplay and Replay's encoder.
        codec_->thread_count = 2;
        codec_->thread_type = FF_THREAD_FRAME;
        result = avcodec_open2(codec_, decoder, nullptr);
        if(result < 0)
        {
            error = "FFmpeg could not start the video decoder: " + FfmpegError(result);
            Close();
            return false;
        }

        decoded_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        if(!decoded_ || !packet_)
        {
            error = "FFmpeg could not allocate decode buffers";
            Close();
            return false;
        }

        // Decoding must still read the encoded source, but scaling before the
        // RGBA mailbox substantially reduces CPU memory traffic and Unity GPU
        // upload cost. Never upscale a smaller file merely because the user
        // selected a higher tier.
        if(codec_->width <= 0 || codec_->height <= 0)
        {
            error = "The MP4 reports an invalid video size";
            Close();
            return false;
        }

        const int sourceHeight = codec_->height;
        sourceWidth_ = codec_->width;
        sourceHeight_ = codec_->height;
        height_ = maximumOutputHeight > 0
            ? std::min(sourceHeight, maximumOutputHeight)
            : sourceHeight;
        width_ = static_cast<int>(std::lround(
            codec_->width * (height_ / static_cast<double>(sourceHeight))));
        width_ = std::max(width_, 1);
        streamTimeBase_ = av_q2d(stream->time_base);

        const AVRational frameRate = av_guess_frame_rate(format_, stream, nullptr);
        if(frameRate.num > 0 && frameRate.den > 0)
            nominalFrameSeconds_ = av_q2d(av_inv_q(frameRate));

        if(stream->duration > 0)
            durationSeconds_ = stream->duration * streamTimeBase_;
        else if(format_->duration > 0)
            durationSeconds_ = format_->duration / static_cast<double>(AV_TIME_BASE);

        stopWorker_ = false;
        open_ = true;
        worker_ = std::thread(&FrameDecoder::WorkerMain, this);
        return true;
    }

    void FrameDecoder::Close()
    {
        open_ = false;
        stopWorker_ = true;
        requestChanged_.notify_all();
        if(worker_.joinable())
            worker_.join();

        if(converter_)
            sws_freeContext(converter_);
        converter_ = nullptr;
        if(packet_)
            av_packet_free(&packet_);
        if(decoded_)
            av_frame_free(&decoded_);
        if(codec_)
            avcodec_free_context(&codec_);
        if(format_)
            avformat_close_input(&format_);

        videoStream_ = -1;
        width_ = 0;
        height_ = 0;
        sourceWidth_ = 0;
        sourceHeight_ = 0;
        streamTimeBase_ = 0.0;
        nominalFrameSeconds_ = 1.0 / 30.0;
        durationSeconds_ = 0.0;
        averageDecodeMilliseconds_ = 0.0;
        bufferAllocations_ = 0;

        {
            std::scoped_lock requestLock(requestMutex_);
            requestedSeconds_ = 0.0;
            requestVersion_ = 0;
        }
        {
            std::scoped_lock outputLock(outputMutex_);
            newestFrame_ = {};
            recycledBuffers_.clear();
            frameWaiting_ = false;
        }
    }

    void FrameDecoder::Request(double mediaSeconds)
    {
        if(!open_)
            return;

        {
            std::scoped_lock lock(requestMutex_);
            requestedSeconds_ = std::max(0.0, mediaSeconds);
            ++requestVersion_;
        }
        requestChanged_.notify_one();
    }

    bool FrameDecoder::TryTake(VideoFrame& destination)
    {
        std::scoped_lock lock(outputMutex_);
        if(!frameWaiting_)
            return false;

        destination = std::move(newestFrame_);
        newestFrame_ = {};
        frameWaiting_ = false;
        return true;
    }

    void FrameDecoder::Recycle(VideoFrame&& frame)
    {
        if(frame.rgba.empty())
            return;
        std::scoped_lock lock(outputMutex_);
        RecycleBufferLocked(std::move(frame.rgba));
    }

    std::optional<std::string> FrameDecoder::TakeError()
    {
        std::scoped_lock lock(errorMutex_);
        auto result = std::move(workerError_);
        workerError_.reset();
        return result;
    }

    void FrameDecoder::SetWorkerError(std::string message)
    {
        {
            std::scoped_lock lock(errorMutex_);
            if(!workerError_)
                workerError_ = std::move(message);
        }
        open_ = false;
        stopWorker_ = true;
        requestChanged_.notify_all();
    }

    void FrameDecoder::WorkerMain() noexcept
    {
        try
        {
            WorkerLoop();
        }
        catch(const std::bad_alloc&)
        {
            SetWorkerError("The video decoder ran out of memory.");
        }
        catch(const std::exception& exception)
        {
            SetWorkerError(std::string("The video decoder stopped: ") + exception.what());
        }
        catch(...)
        {
            SetWorkerError("The video decoder stopped because of an unexpected internal error.");
        }
    }

    void FrameDecoder::WorkerLoop()
    {
        std::uint64_t handledVersion = 0;
        double lastDecodedTime = -std::numeric_limits<double>::infinity();
        double lastDecodedDuration = nominalFrameSeconds_;

        while(!stopWorker_)
        {
            double target = 0.0;
            std::uint64_t targetVersion = 0;
            {
                std::unique_lock lock(requestMutex_);
                requestChanged_.wait(lock, [this, handledVersion]
                {
                    return stopWorker_ || requestVersion_ != handledVersion;
                });
                if(stopWorker_)
                    break;
                target = requestedSeconds_;
                targetVersion = requestVersion_;
            }

            // A normal 30 fps video does not need a new decode for every 90 Hz
            // Unity update. If the last decoded image still covers the target
            // interval, mark the request handled and wait for song time to move.
            if(std::isfinite(lastDecodedTime) &&
               target >= lastDecodedTime &&
               target < lastDecodedTime + lastDecodedDuration)
            {
                handledVersion = targetVersion;
                continue;
            }

            // Restarts, replay scrubbing, loop wraparound, and large practice
            // jumps should seek to the preceding keyframe. Small forward steps
            // decode sequentially, which is substantially cheaper on Quest.
            if(!std::isfinite(lastDecodedTime) ||
               target + nominalFrameSeconds_ < lastDecodedTime ||
               target - lastDecodedTime > 0.75)
            {
                if(!SeekNear(target))
                {
                    SetWorkerError("FFmpeg could not seek to the requested video position.");
                    break;
                }
                lastDecodedTime = -std::numeric_limits<double>::infinity();
                lastDecodedDuration = nominalFrameSeconds_;
            }

            // Measure all H.264 frames that must be decoded to reach this
            // request, not merely the final frame that is converted to RGBA.
            // Lower output-FPS limits still require intermediate reference
            // frames, so excluding them substantially understated CPU work.
            const auto requestWorkStarted = std::chrono::steady_clock::now();
            while(!stopWorker_)
            {
                if(!ReadDecodedFrame())
                {
                    handledVersion = targetVersion;
                    break;
                }

                lastDecodedTime = CurrentFrameTime();
                lastDecodedDuration = CurrentFrameDuration();
                if(lastDecodedTime + lastDecodedDuration < target)
                    continue;

                VideoFrame output = AcquireOutputFrame();
                if(!ConvertCurrentFrame(output))
                {
                    SetWorkerError("FFmpeg could not convert a decoded frame for the video screen.");
                    break;
                }
                const auto elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - requestWorkStarted).count();
                const auto previous = averageDecodeMilliseconds_.load();
                averageDecodeMilliseconds_ = previous <= 0.0
                    ? elapsed : previous * 0.9 + elapsed * 0.1;
                Publish(std::move(output));
                handledVersion = targetVersion;
                break;
            }
        }
    }

    bool FrameDecoder::ReadDecodedFrame()
    {
        while(!stopWorker_)
        {
            int result = avcodec_receive_frame(codec_, decoded_);
            if(result == 0)
                return true;
            if(result == AVERROR_EOF)
                return false;
            if(result != AVERROR(EAGAIN))
                return false;

            // Feed packets until this codec has enough compressed data to emit
            // another frame. Non-video packets are discarded immediately.
            result = av_read_frame(format_, packet_);
            if(result < 0)
            {
                avcodec_send_packet(codec_, nullptr);
                return avcodec_receive_frame(codec_, decoded_) == 0;
            }

            if(packet_->stream_index == videoStream_)
                result = avcodec_send_packet(codec_, packet_);
            else
                result = 0;
            av_packet_unref(packet_);

            if(result < 0 && result != AVERROR(EAGAIN))
                return false;
        }
        return false;
    }

    bool FrameDecoder::SeekNear(double mediaSeconds)
    {
        if(!format_ || streamTimeBase_ <= 0.0)
            return false;

        const auto timestamp = static_cast<std::int64_t>(mediaSeconds / streamTimeBase_);
        const int result = av_seek_frame(format_, videoStream_, timestamp, AVSEEK_FLAG_BACKWARD);
        if(result < 0)
            return false;

        avcodec_flush_buffers(codec_);
        return true;
    }

    double FrameDecoder::CurrentFrameTime() const
    {
        std::int64_t timestamp = decoded_->best_effort_timestamp;
        if(timestamp == AV_NOPTS_VALUE)
            timestamp = decoded_->pts;
        return timestamp == AV_NOPTS_VALUE ? 0.0 : timestamp * streamTimeBase_;
    }

    double FrameDecoder::CurrentFrameDuration() const
    {
        // FFmpeg renamed AVFrame::pkt_duration to AVFrame::duration after the
        // 4.4 series. Support both tested runtimes while retaining the nominal
        // rate only as a fallback for incomplete container timing data.
#if LIBAVUTIL_VERSION_MAJOR >= 58
        if(decoded_->duration > 0)
            return decoded_->duration * streamTimeBase_;
#else
        if(decoded_->pkt_duration > 0)
            return decoded_->pkt_duration * streamTimeBase_;
#endif
        return nominalFrameSeconds_;
    }

    bool FrameDecoder::ConvertCurrentFrame(VideoFrame& destination)
    {
        converter_ = sws_getCachedContext(
            converter_,
            decoded_->width,
            decoded_->height,
            static_cast<AVPixelFormat>(decoded_->format),
            width_,
            height_,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if(!converter_)
            return false;

        destination.width = width_;
        destination.height = height_;
        destination.presentationSeconds = CurrentFrameTime();
        destination.durationSeconds = CurrentFrameDuration();
        const auto requiredBytes = static_cast<std::size_t>(width_) * height_ * 4;
        if(destination.rgba.capacity() < requiredBytes)
            ++bufferAllocations_;
        destination.rgba.resize(requiredBytes);

        std::uint8_t* outputPlanes[4] = { destination.rgba.data(), nullptr, nullptr, nullptr };
        int outputStrides[4] = { width_ * 4, 0, 0, 0 };
        const int convertedRows = sws_scale(
            converter_,
            decoded_->data,
            decoded_->linesize,
            0,
            decoded_->height,
            outputPlanes,
            outputStrides);
        return convertedRows == height_;
    }

    void FrameDecoder::Publish(VideoFrame&& frame)
    {
        // A mailbox deliberately drops superseded frames. Blocking the decoder
        // until Unity consumes every image would add latency and make Replay
        // rendering slower without improving the final visible frame sequence.
        std::scoped_lock lock(outputMutex_);
        if(frameWaiting_ && !newestFrame_.rgba.empty())
            RecycleBufferLocked(std::move(newestFrame_.rgba));
        newestFrame_ = std::move(frame);
        frameWaiting_ = true;
    }

    VideoFrame FrameDecoder::AcquireOutputFrame()
    {
        std::scoped_lock lock(outputMutex_);
        VideoFrame frame;
        if(!recycledBuffers_.empty())
        {
            frame.rgba = std::move(recycledBuffers_.back());
            recycledBuffers_.pop_back();
        }
        return frame;
    }

    void FrameDecoder::RecycleBufferLocked(std::vector<std::uint8_t>&& buffer)
    {
        // One worker, one mailbox, and one main-thread upload need at most
        // three reusable allocations. A strict cap prevents an old resolution
        // from retaining arbitrary memory after rapid seeks or scene changes.
        constexpr std::size_t MaximumReusableBuffers = 3;
        if(recycledBuffers_.size() < MaximumReusableBuffers)
            recycledBuffers_.push_back(std::move(buffer));
    }
}
