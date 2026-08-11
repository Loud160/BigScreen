#include "BigScreen/FrameDecoder.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <limits>
#include <string>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/pixfmt.h"
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
        int maximumOutputFps,
        std::string& error)
    {
        Close();
        error.clear();

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
        AVCodec* decoder = nullptr;
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
        // Treat the setting as a ceiling. Taking the longer interval preserves
        // native cadence for a 24 FPS source under a 30/60 FPS limit, while a
        // 60 FPS source presents at most 15 or 30 images when requested.
        outputFrameSeconds_ = std::max(
            nominalFrameSeconds_,
            1.0 / std::max(1, maximumOutputFps));

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
        streamTimeBase_ = 0.0;
        nominalFrameSeconds_ = 1.0 / 30.0;
        outputFrameSeconds_ = 1.0 / 30.0;
        durationSeconds_ = 0.0;

        {
            std::scoped_lock requestLock(requestMutex_);
            requestedSeconds_ = 0.0;
            requestVersion_ = 0;
        }
        {
            std::scoped_lock outputLock(outputMutex_);
            newestFrame_ = {};
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

    void FrameDecoder::WorkerMain()
    {
        std::uint64_t handledVersion = 0;
        double lastDecodedTime = -std::numeric_limits<double>::infinity();

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

            // Quantize the external song clock to the configured presentation
            // ceiling. The decoder still consumes any intervening H.264
            // dependency frames, but costly RGBA conversion and Unity uploads
            // occur only at this cadence.
            target = std::floor(
                (std::max(0.0, target) + 0.000001) / outputFrameSeconds_) *
                outputFrameSeconds_;

            // A video does not need a new decode request for every 90 Hz
            // Unity update. If the last decoded image still covers the target
            // presentation interval, wait for song time to cross the next slot.
            if(std::isfinite(lastDecodedTime) &&
               target >= lastDecodedTime &&
               target < lastDecodedTime + outputFrameSeconds_)
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
                    handledVersion = targetVersion;
                    continue;
                }
                lastDecodedTime = -std::numeric_limits<double>::infinity();
            }

            while(!stopWorker_)
            {
                if(!ReadDecodedFrame())
                {
                    handledVersion = targetVersion;
                    break;
                }

                lastDecodedTime = CurrentFrameTime();
                if(lastDecodedTime + nominalFrameSeconds_ * 0.5 < target)
                    continue;

                VideoFrame output;
                if(ConvertCurrentFrame(output))
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
        destination.rgba.resize(static_cast<std::size_t>(width_) * height_ * 4);

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
        newestFrame_ = std::move(frame);
        frameWaiting_ = true;
    }
}
