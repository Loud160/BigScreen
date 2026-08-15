// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/CoreLogic.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <pthread.h>
#include <string>
#include <time.h>

extern "C" {
#include "libavcodec/avcodec.h"
#if defined(__ANDROID__)
#include "libavcodec/jni.h"
#endif
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/display.h"
#include "libavutil/frame.h"
#include "libavutil/pixdesc.h"
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

        std::uint64_t CurrentThreadCpuNanoseconds()
        {
#if defined(CLOCK_THREAD_CPUTIME_ID)
            timespec value{};
            if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
            {
                return static_cast<std::uint64_t>(value.tv_sec) *
                           1'000'000'000ULL +
                       static_cast<std::uint64_t>(value.tv_nsec);
            }
#endif
            return 0;
        }

        struct CodecPolicy {
            const char* displayName;
            const char* softwareName;
            const char* hardwareName;
            CoreLogic::VideoCodecKind kind;
        };

        std::optional<CodecPolicy> PolicyForCodec(AVCodecID codec)
        {
            switch(codec)
            {
                case AV_CODEC_ID_H264:
                    return CodecPolicy{"H.264", "h264", "h264_mediacodec", CoreLogic::VideoCodecKind::H264};
                case AV_CODEC_ID_HEVC:
                    return CodecPolicy{"H.265/HEVC", nullptr, "hevc_mediacodec", CoreLogic::VideoCodecKind::Hevc};
                case AV_CODEC_ID_VP8:
                    return CodecPolicy{"VP8", "vp8", "vp8_mediacodec", CoreLogic::VideoCodecKind::Vp8};
                case AV_CODEC_ID_VP9:
                    return CodecPolicy{"VP9", "vp9", "vp9_mediacodec", CoreLogic::VideoCodecKind::Vp9};
                default:
                    return std::nullopt;
            }
        }

        bool IsHdrTransfer(AVColorTransferCharacteristic transfer)
        {
            return transfer == AVCOL_TRC_SMPTE2084 ||
                   transfer == AVCOL_TRC_ARIB_STD_B67;
        }

        std::string UnsupportedPixelReason(
            AVPixelFormat format,
            AVColorTransferCharacteristic transfer,
            AVCodecID codec,
            int profile)
        {
            const auto policy = PolicyForCodec(codec);
            const auto kind = policy
                ? policy->kind
                : CoreLogic::VideoCodecKind::Unknown;
            bool tenBit = false;
            // FFmpeg 9 renamed public profile constants from FF_PROFILE_* to
            // AV_PROFILE_*. Their numeric values are part of the codec spec,
            // so select the matching API name at compile time for each
            // separately built backend.
#if defined(AV_PROFILE_HEVC_MAIN_10)
            if(codec == AV_CODEC_ID_HEVC && profile == AV_PROFILE_HEVC_MAIN_10)
#else
            if(codec == AV_CODEC_ID_HEVC && profile == FF_PROFILE_HEVC_MAIN_10)
#endif
                tenBit = true;
#if defined(AV_PROFILE_VP9_2)
            if(codec == AV_CODEC_ID_VP9 && profile == AV_PROFILE_VP9_2)
#else
            if(codec == AV_CODEC_ID_VP9 && profile == FF_PROFILE_VP9_2)
#endif
                tenBit = true;
            if(format == AV_PIX_FMT_NONE)
                return CoreLogic::UnsupportedVideoSignalReason(
                    kind, IsHdrTransfer(transfer), tenBit, false, true);
            const auto* description = av_pix_fmt_desc_get(format);
            if(!description)
                return CoreLogic::UnsupportedVideoSignalReason(
                    kind, IsHdrTransfer(transfer), tenBit, false, true);
            for(int component = 0; component < description->nb_components; ++component)
            {
                if(description->comp[component].depth > 8)
                    tenBit = true;
            }
            // Big Screen's color conversion and MediaCodec contract are
            // intentionally limited to 4:2:0. Reject 4:2:2/4:4:4 instead of
            // silently accepting a format that a Quest decoder may reinterpret.
            return CoreLogic::UnsupportedVideoSignalReason(
                kind,
                IsHdrTransfer(transfer),
                tenBit,
                (description->flags & AV_PIX_FMT_FLAG_ALPHA) != 0,
                description->log2_chroma_w == 1 &&
                    description->log2_chroma_h == 1);
        }

        int DisplayQuarterTurns(const AVStream* stream)
        {
            if(!stream)
                return 0;
#if LIBAVFORMAT_VERSION_MAJOR >= 61
            const auto* sideData = av_packet_side_data_get(
                stream->codecpar->coded_side_data,
                stream->codecpar->nb_coded_side_data,
                AV_PKT_DATA_DISPLAYMATRIX);
            const auto* matrix = sideData
                ? reinterpret_cast<const int32_t*>(sideData->data)
                : nullptr;
            const std::size_t sideDataSize = sideData ? sideData->size : 0;
            if(!matrix || sideDataSize < sizeof(int32_t) * 9)
                return 0;
#else
            int sideDataSize = 0;
            const auto* matrix = reinterpret_cast<const int32_t*>(
                av_stream_get_side_data(
                    stream, AV_PKT_DATA_DISPLAYMATRIX, &sideDataSize));
            if(!matrix || sideDataSize < 0 ||
               static_cast<std::size_t>(sideDataSize) < sizeof(int32_t) * 9)
                return 0;
#endif
            const double degrees = av_display_rotation_get(matrix);
            if(!std::isfinite(degrees))
                return 0;
            int turns = static_cast<int>(std::lround(degrees / 90.0));
            turns %= 4;
            if(turns < 0) turns += 4;
            return turns;
        }

#if defined(__ANDROID__)
        int RegisterJavaVmForThisRuntime(void* javaVm)
        {
            // FrameDecoder.cpp is compiled once into each private backend SO,
            // so these statics are deliberately independent for FFmpeg 4.4
            // and FFmpeg 9. libavcodec documents VM registration as process-
            // lifetime setup; repeating it for every video is unnecessary and
            // can race when previews are rapidly recreated.
            static std::once_flag registration;
            static int registrationResult = AVERROR(EINVAL);
            std::call_once(registration, [javaVm]() {
                registrationResult = av_jni_set_java_vm(javaVm, nullptr);
            });
            return registrationResult;
        }
#endif
    }

    FrameDecoder::~FrameDecoder()
    {
        Close();
    }

    bool FrameDecoder::Open(
        const std::filesystem::path& videoPath,
        int maximumOutputHeight,
        bool preferHardwareDecoding,
        void* javaVm,
        std::string& error)
    {
        Close();
        // Close intentionally preserves the final worker total so its owner
        // can read it after join. A new Open begins a distinct decoder
        // lifetime and therefore resets the counter here.
        workerCpuNanoseconds_ = 0;
        usingHardwareDecoder_ = false;
        hardwareFallbackReason_.clear();
        softwareFallbackAllowed_ = true;
        softwareFallbackBlockedReason_.clear();
        codecName_ = "unknown";
        displayQuarterTurns_ = 0;
        error.clear();
        {
            std::scoped_lock lock(errorMutex_);
            workerError_.reset();
        }

        int result = avformat_open_input(&format_, videoPath.string().c_str(), nullptr, nullptr);
        if(result < 0)
        {
            error = "FFmpeg could not open the video container: " + FfmpegError(result);
            Close();
            return false;
        }

        result = avformat_find_stream_info(format_, nullptr);
        if(result < 0)
        {
            error = "FFmpeg could not read the video stream table: " + FfmpegError(result);
            Close();
            return false;
        }

        // av_find_best_stream accounts for containers with artwork or multiple
        // video tracks. Decoder selection happens through the explicit codec
        // policy below; accepting FFmpeg's first registered decoder could
        // accidentally treat a MediaCodec wrapper as a software fallback.
        videoStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if(videoStream_ < 0)
        {
            error = "The file does not contain a supported video stream.";
            Close();
            return false;
        }

        AVStream* stream = format_->streams[videoStream_];
        const auto policy = PolicyForCodec(stream->codecpar->codec_id);
        if(!policy)
        {
            error = "This video uses " +
                std::string(avcodec_get_name(stream->codecpar->codec_id)) +
                ". Big Screen supports H.264, H.265/HEVC, VP8, and VP9.";
            Close();
            return false;
        }
        codecName_ = policy->displayName;
        const int sourceShortEdge = std::min(
            stream->codecpar->width, stream->codecpar->height);
        softwareFallbackBlockedReason_ =
            CoreLogic::SoftwareFallbackBlockedReason(
                policy->kind, sourceShortEdge);
        softwareFallbackAllowed_ = softwareFallbackBlockedReason_.empty();

        const auto pixelReason = UnsupportedPixelReason(
            static_cast<AVPixelFormat>(stream->codecpar->format),
            stream->codecpar->color_trc,
            stream->codecpar->codec_id,
            stream->codecpar->profile);
        if(!pixelReason.empty())
        {
            error = pixelReason;
            Close();
            return false;
        }

        const AVCodec* softwareDecoder = policy->softwareName
            ? avcodec_find_decoder_by_name(policy->softwareName)
            : nullptr;
        const AVCodec* selectedDecoder = softwareDecoder;
        bool attemptingHardware = false;

        // FFmpeg 4.4 drives MediaCodec through JNI, while newer runtimes can
        // use Android's NDK wrapper. Register the process VM with each private
        // libavcodec instance anyway: the two isolated runtimes do not share
        // libavcodec's internal global state. No Surface is supplied, so
        // MediaCodec returns CPU-readable YUV frames that preserve Big Screen's
        // established RGBA conversion and Unity texture path.
        if(preferHardwareDecoding)
        {
            if(!javaVm)
            {
                hardwareFallbackReason_ =
                    "the Android Java VM was unavailable";
            }
            #if defined(__ANDROID__)
            else if((result = RegisterJavaVmForThisRuntime(javaVm)) < 0)
            {
                hardwareFallbackReason_ =
                    "FFmpeg rejected the Android Java VM: " + FfmpegError(result);
            }
            else if(const AVCodec* hardwareDecoder =
                        avcodec_find_decoder_by_name(policy->hardwareName))
            {
                selectedDecoder = hardwareDecoder;
                attemptingHardware = true;
            }
            else
            {
                hardwareFallbackReason_ =
                    "this private FFmpeg runtime does not expose " +
                    std::string(policy->hardwareName);
            }
            #else
            else
            {
                hardwareFallbackReason_ =
                    "MediaCodec is available only in the Android build";
            }
            #endif
        }

        if(!attemptingHardware && !softwareFallbackAllowed_)
        {
            error = softwareFallbackBlockedReason_;
            if(!preferHardwareDecoding)
                error += " Enable Hardware Video Decoding or use a lower-resolution video.";
            Close();
            return false;
        }
        if(!attemptingHardware && !softwareDecoder)
        {
            error = "The selected FFmpeg runtime does not provide the required " +
                codecName_ + " decoder.";
            Close();
            return false;
        }

        const auto openCodec = [&](const AVCodec* candidate,
                                   std::string& openError) -> bool
        {
            avcodec_free_context(&codec_);
            codec_ = avcodec_alloc_context3(candidate);
            if(!codec_)
            {
                openError = "FFmpeg could not allocate a decoder context";
                return false;
            }

            int openResult = avcodec_parameters_to_context(
                codec_, stream->codecpar);
            if(openResult < 0)
            {
                openError =
                    "FFmpeg could not apply video stream parameters: " +
                    FfmpegError(openResult);
                return false;
            }

            // Big Screen already decodes on its own worker. Keep libavcodec
            // single-threaded on Quest: an FFmpeg frame worker previously
            // survived an exceptional Unity teardown and later faulted in heap
            // memory. MediaCodec ignores these software threading fields.
            codec_->thread_count = 1;
            codec_->thread_type = 0;
            openResult = avcodec_open2(codec_, candidate, nullptr);
            if(openResult < 0)
            {
                openError = "FFmpeg could not start " +
                    std::string(candidate && candidate->name
                        ? candidate->name : "the video decoder") +
                    ": " + FfmpegError(openResult);
                return false;
            }
            return true;
        };

        std::string decoderOpenError;
        if(!openCodec(selectedDecoder, decoderOpenError))
        {
            if(!attemptingHardware)
            {
                error = std::move(decoderOpenError);
                Close();
                return false;
            }

            hardwareFallbackReason_ = decoderOpenError;
            if(!softwareFallbackAllowed_ || !softwareDecoder)
            {
                error = decoderOpenError + "; " + softwareFallbackBlockedReason_;
                Close();
                return false;
            }
            decoderOpenError.clear();
            if(!openCodec(softwareDecoder, decoderOpenError))
            {
                error = hardwareFallbackReason_ +
                    "; software fallback also failed: " + decoderOpenError;
                Close();
                return false;
            }
            attemptingHardware = false;
        }
        usingHardwareDecoder_ = attemptingHardware;

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
            error = "The video reports an invalid frame size";
            Close();
            return false;
        }

        sourceWidth_ = codec_->width;
        sourceHeight_ = codec_->height;
        const int shortEdge = std::min(sourceWidth_, sourceHeight_);
        const double scale = maximumOutputHeight > 0 && shortEdge > maximumOutputHeight
            ? maximumOutputHeight / static_cast<double>(shortEdge)
            : 1.0;
        conversionWidth_ = std::max(
            1, static_cast<int>(std::lround(sourceWidth_ * scale)));
        conversionHeight_ = std::max(
            1, static_cast<int>(std::lround(sourceHeight_ * scale)));
        displayQuarterTurns_ = DisplayQuarterTurns(stream);
        if(displayQuarterTurns_ % 2 == 0)
        {
            width_ = conversionWidth_;
            height_ = conversionHeight_;
        }
        else
        {
            width_ = conversionHeight_;
            height_ = conversionWidth_;
        }
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
        try
        {
            worker_ = std::thread(&FrameDecoder::WorkerMain, this);
        }
        catch(const std::exception& exception)
        {
            error = std::string("Could not start the video decoder worker: ") +
                exception.what();
            Close();
            return false;
        }
        return true;
    }

    void FrameDecoder::Close()
    {
        open_ = false;
        // The worker evaluates the wait predicate while holding requestMutex_.
        // Publish the stop flag under that same mutex before notifying. An
        // atomic flag alone does not close the gap between a false predicate
        // evaluation and the worker actually blocking, which could otherwise
        // lose this wakeup and leave the Unity thread stuck in join().
        {
            std::scoped_lock lock(requestMutex_);
            stopWorker_ = true;
        }
        requestChanged_.notify_all();
        if(worker_.joinable())
            worker_.join();

        if(converter_)
            sws_freeContext(converter_);
        converter_ = nullptr;
        converterSourceWidth_ = 0;
        converterSourceHeight_ = 0;
        converterSourceFormat_ = -1;
        converterColorSpace_ = -1;
        converterColorRange_ = -1;
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
        conversionWidth_ = 0;
        conversionHeight_ = 0;
        displayQuarterTurns_ = 0;
        rotationScratch_.clear();
        streamTimeBase_ = 0.0;
        nominalFrameSeconds_ = 1.0 / 30.0;
        durationSeconds_ = 0.0;
        codecName_ = "unknown";
        softwareFallbackAllowed_ = true;
        softwareFallbackBlockedReason_.clear();
        averageDecodeMilliseconds_ = 0.0;
        peakDecodeMilliseconds_ = 0.0;
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

    const char* FrameDecoder::RuntimeVersion() const
    {
        return av_version_info();
    }

    void FrameDecoder::SetWorkerError(std::string message)
    {
        if(usingHardwareDecoder_)
            message = "MediaCodec hardware decoder failed: " + message;
        {
            std::scoped_lock lock(errorMutex_);
            if(!workerError_)
                workerError_ = std::move(message);
        }
        open_ = false;
        // Keep the condition-variable state transition under its predicate
        // mutex for the same reason as Close(). SetWorkerError normally runs
        // on the worker itself, but this invariant keeps every producer safe.
        {
            std::scoped_lock lock(requestMutex_);
            stopWorker_ = true;
        }
        requestChanged_.notify_all();
    }

    void FrameDecoder::WorkerMain() noexcept
    {
        // Android tombstones otherwise report only a generic Thread-N name.
        // A stable name makes any future native failure attributable without
        // guessing whether it belonged to Big Screen or another mod.
        pthread_setname_np(pthread_self(), "BigScreenDecode");
        const std::uint64_t cpuStarted = CurrentThreadCpuNanoseconds();
        const auto updateCpuTotal = [this, cpuStarted]
        {
            const std::uint64_t now = CurrentThreadCpuNanoseconds();
            if(now >= cpuStarted)
                workerCpuNanoseconds_ = now - cpuStarted;
        };
        try
        {
            WorkerLoop(cpuStarted);
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
        updateCpuTotal();
    }

    void FrameDecoder::WorkerLoop(std::uint64_t cpuStartedNanoseconds)
    {
        std::uint64_t handledVersion = 0;
        double lastDecodedTime = -std::numeric_limits<double>::infinity();
        double lastDecodedDuration = nominalFrameSeconds_;
        const auto updateCpuTotal = [this, cpuStartedNanoseconds]
        {
            const std::uint64_t now = CurrentThreadCpuNanoseconds();
            if(now >= cpuStartedNanoseconds)
                workerCpuNanoseconds_ = now - cpuStartedNanoseconds;
        };

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
                updateCpuTotal();
                continue;
            }

            // Restarts, replay scrubbing, loop wraparound, and large practice
            // jumps should seek to the preceding keyframe. Small forward steps
            // decode sequentially, which is substantially cheaper on Quest.
            if(!std::isfinite(lastDecodedTime) ||
               target + nominalFrameSeconds_ < lastDecodedTime ||
               target - lastDecodedTime > 0.75)
            {
                std::string seekError;
                if(!SeekNear(target, seekError))
                {
                    SetWorkerError(
                        "FFmpeg could not seek to the requested video position: " +
                        seekError);
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
                const auto framePixelReason = UnsupportedPixelReason(
                    static_cast<AVPixelFormat>(decoded_->format),
                    decoded_->color_trc,
                    codec_->codec_id,
                    codec_->profile);
                if(!framePixelReason.empty())
                {
                    SetWorkerError(framePixelReason);
                    break;
                }
                std::string conversionError;
                if(!ConvertCurrentFrame(output, conversionError))
                {
                    SetWorkerError(
                        "FFmpeg could not convert a decoded frame for the video screen: " +
                        conversionError);
                    break;
                }
                const auto elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - requestWorkStarted).count();
                const auto previous = averageDecodeMilliseconds_.load();
                averageDecodeMilliseconds_ = previous <= 0.0
                    ? elapsed : previous * 0.9 + elapsed * 0.1;
                auto previousPeak = peakDecodeMilliseconds_.load();
                while(elapsed > previousPeak &&
                      !peakDecodeMilliseconds_.compare_exchange_weak(
                          previousPeak, elapsed))
                {
                    // compare_exchange refreshes previousPeak after another
                    // write. Retry only while this sample is still larger.
                }
                Publish(std::move(output));
                handledVersion = targetVersion;
                break;
            }
            updateCpuTotal();
        }
    }

    bool FrameDecoder::ReadDecodedFrame()
    {
        const auto applyDecodedCrop = [this]() {
            // Apply codec crop metadata before swscale sees the picture. This
            // prevents padded MediaCodec stride/slice dimensions (or ordinary
            // coded-edge padding) from appearing as green or dark bars. The
            // same rule is required for software frames and the delayed final
            // frame returned while flushing the decoder.
            if((decoded_->crop_top || decoded_->crop_bottom ||
                decoded_->crop_left || decoded_->crop_right) &&
               av_frame_apply_cropping(
                   decoded_, AV_FRAME_CROP_UNALIGNED) < 0)
            {
                SetWorkerError(
                    "FFmpeg could not apply the decoded frame crop metadata.");
                return false;
            }
            return true;
        };
        while(!stopWorker_)
        {
            int result = avcodec_receive_frame(codec_, decoded_);
            if(result == 0)
                return applyDecodedCrop();
            if(result == AVERROR_EOF)
                return false;
            if(result != AVERROR(EAGAIN))
            {
                SetWorkerError(
                    "FFmpeg could not receive a decoded video frame: " +
                    FfmpegError(result));
                return false;
            }

            // Feed packets until this codec has enough compressed data to emit
            // another frame. Non-video packets are discarded immediately.
            result = av_read_frame(format_, packet_);
            if(result < 0)
            {
                // EOF is normal. Flush the decoder once so a delayed final
                // picture can still be emitted. Every other demux failure is a
                // real media/I/O error and must not masquerade as end-of-video.
                if(result != AVERROR_EOF)
                {
                    SetWorkerError(
                        "FFmpeg could not read the next video packet: " +
                        FfmpegError(result));
                    return false;
                }

                const int flushResult = avcodec_send_packet(codec_, nullptr);
                if(flushResult < 0 && flushResult != AVERROR_EOF)
                {
                    SetWorkerError(
                        "FFmpeg could not flush the final video packet: " +
                        FfmpegError(flushResult));
                    return false;
                }
                const int finalResult = avcodec_receive_frame(codec_, decoded_);
                if(finalResult == 0)
                    return applyDecodedCrop();
                if(finalResult != AVERROR_EOF && finalResult != AVERROR(EAGAIN))
                {
                    SetWorkerError(
                        "FFmpeg could not receive the final video frame: " +
                        FfmpegError(finalResult));
                }
                return false;
            }

            if(packet_->stream_index == videoStream_)
                result = avcodec_send_packet(codec_, packet_);
            else
                result = 0;
            av_packet_unref(packet_);

            if(result < 0 && result != AVERROR(EAGAIN))
            {
                SetWorkerError(
                    "FFmpeg could not submit a compressed video packet: " +
                    FfmpegError(result));
                return false;
            }
        }
        return false;
    }

    bool FrameDecoder::SeekNear(double mediaSeconds, std::string& error)
    {
        if(!format_ || streamTimeBase_ <= 0.0)
        {
            error = "the stream timing data is unavailable";
            return false;
        }

        const auto timestamp = static_cast<std::int64_t>(mediaSeconds / streamTimeBase_);
        const int result = av_seek_frame(format_, videoStream_, timestamp, AVSEEK_FLAG_BACKWARD);
        if(result < 0)
        {
            error = FfmpegError(result);
            return false;
        }

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

    bool FrameDecoder::ConvertCurrentFrame(
        VideoFrame& destination,
        std::string& error)
    {
        const bool converterInputChanged =
            converterSourceWidth_ != decoded_->width ||
            converterSourceHeight_ != decoded_->height ||
            converterSourceFormat_ != decoded_->format;
        converter_ = sws_getCachedContext(
            converter_,
            decoded_->width,
            decoded_->height,
            static_cast<AVPixelFormat>(decoded_->format),
            conversionWidth_,
            conversionHeight_,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if(!converter_)
        {
            error = "libswscale could not create a conversion context";
            return false;
        }

        converterSourceWidth_ = decoded_->width;
        converterSourceHeight_ = decoded_->height;
        converterSourceFormat_ = decoded_->format;

        // Hardware commonly reports NV12 and software commonly reports
        // YUV420P, but both paths need the declared matrix and range. Applying
        // this uniformly prevents visible color changes after fallback.
        if(converterInputChanged ||
            converterColorSpace_ != decoded_->colorspace ||
            converterColorRange_ != decoded_->color_range)
        {
            int swsColorSpace = SWS_CS_DEFAULT;
            switch(decoded_->colorspace)
            {
                case AVCOL_SPC_BT709: swsColorSpace = SWS_CS_ITU709; break;
                case AVCOL_SPC_FCC: swsColorSpace = SWS_CS_FCC; break;
                case AVCOL_SPC_BT470BG:
                case AVCOL_SPC_SMPTE170M: swsColorSpace = SWS_CS_ITU601; break;
                case AVCOL_SPC_SMPTE240M: swsColorSpace = SWS_CS_SMPTE240M; break;
                default: break;
            }
            const int* coefficients = sws_getCoefficients(swsColorSpace);
            const int colorspaceResult = coefficients
                ? sws_setColorspaceDetails(
                   converter_,
                   coefficients,
                   decoded_->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                   coefficients,
                   1,
                   0,
                   1 << 16,
                   1 << 16)
                : 0;
            if(colorspaceResult < 0)
            {
                error = "libswscale rejected the video's color matrix or range (" +
                    std::to_string(colorspaceResult) + ")";
                return false;
            }

            converterColorSpace_ = decoded_->colorspace;
            converterColorRange_ = decoded_->color_range;
        }

        destination.width = width_;
        destination.height = height_;
        destination.presentationSeconds = CurrentFrameTime();
        destination.durationSeconds = CurrentFrameDuration();
        const auto requiredBytes = static_cast<std::size_t>(width_) * height_ * 4;
        if(destination.rgba.capacity() < requiredBytes)
            ++bufferAllocations_;
        destination.rgba.resize(requiredBytes);

        std::uint8_t* convertedPixels = destination.rgba.data();
        if(displayQuarterTurns_ != 0)
        {
            rotationScratch_.resize(
                static_cast<std::size_t>(conversionWidth_) *
                conversionHeight_ * 4);
            convertedPixels = rotationScratch_.data();
        }
        std::uint8_t* outputPlanes[4] = { convertedPixels, nullptr, nullptr, nullptr };
        int outputStrides[4] = { conversionWidth_ * 4, 0, 0, 0 };
        const int convertedRows = sws_scale(
            converter_,
            decoded_->data,
            decoded_->linesize,
            0,
            decoded_->height,
            outputPlanes,
            outputStrides);
        if(convertedRows != conversionHeight_)
        {
            error = "libswscale returned " + std::to_string(convertedRows) +
                " rows; " + std::to_string(conversionHeight_) + " were expected";
            return false;
        }
        if(displayQuarterTurns_ == 0)
            return true;

        // Apply container display-matrix orientation once to the decoded
        // picture. User Video Rotation remains a separate UV transform on top
        // of this corrected base, and mapper/Chroma screen geometry remains
        // free to override the physical canvas as before.
        for(int y = 0; y < conversionHeight_; ++y)
        {
            for(int x = 0; x < conversionWidth_; ++x)
            {
                int targetX = x;
                int targetY = y;
                if(displayQuarterTurns_ == 1)
                {
                    targetX = y;
                    targetY = conversionWidth_ - 1 - x;
                }
                else if(displayQuarterTurns_ == 2)
                {
                    targetX = conversionWidth_ - 1 - x;
                    targetY = conversionHeight_ - 1 - y;
                }
                else
                {
                    targetX = conversionHeight_ - 1 - y;
                    targetY = x;
                }
                const auto sourceOffset =
                    (static_cast<std::size_t>(y) * conversionWidth_ + x) * 4;
                const auto targetOffset =
                    (static_cast<std::size_t>(targetY) * width_ + targetX) * 4;
                std::copy_n(
                    rotationScratch_.data() + sourceOffset,
                    4,
                    destination.rgba.data() + targetOffset);
            }
        }
        return true;
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

#ifdef BIGSCREEN_FFMPEG_BACKEND_FACTORY
    std::unique_ptr<FrameDecoderBackend> BIGSCREEN_FFMPEG_BACKEND_FACTORY()
    {
        return std::make_unique<FrameDecoder>();
    }
#endif
}
