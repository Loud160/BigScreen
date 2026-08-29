// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/VideoTranscoderBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <system_error>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/jni.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "libswscale/swscale.h"
}

namespace BigScreen {
    namespace {
        std::string FfmpegError(int status)
        {
            char buffer[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(status, buffer, sizeof(buffer));
            return buffer;
        }

        class NativeVideoTranscoder final : public VideoTranscoderBackend {
        public:
            VideoTranscodeResult TranscodeToH264Mp4(
                const std::filesystem::path& inputPath,
                const std::filesystem::path& outputPath,
                void* javaVm,
                const ProgressCallback& progress,
                const CancellationCheck& cancelled) override
            {
                VideoTranscodeResult result;
                std::error_code cleanupError;
                std::filesystem::remove(outputPath, cleanupError);

                std::string hardwareError;
                if(TranscodeWithEncoder(
                       inputPath, outputPath, "h264_mediacodec", javaVm,
                       progress, cancelled, hardwareError))
                {
                    result.completed = true;
                    result.encoder = VideoTranscodeEncoder::AndroidMediaCodec;
                }
                else if(cancelled && cancelled())
                {
                    result.cancelled = true;
                    result.detail = "Video transcoding was cancelled.";
                }
                else
                {
                    result.hardwareFailure = hardwareError;
                    std::filesystem::remove(outputPath, cleanupError);
                    std::string softwareError;
                    if(TranscodeWithEncoder(
                           inputPath, outputPath, "libx264", nullptr,
                           progress, cancelled, softwareError))
                    {
                        result.completed = true;
                        result.encoder = VideoTranscodeEncoder::X264Software;
                    }
                    else if(cancelled && cancelled())
                    {
                        result.cancelled = true;
                        result.detail = "Video transcoding was cancelled.";
                    }
                    else
                    {
                        result.detail =
                            "Hardware H.264 transcoding failed: " +
                            hardwareError + " Software H.264 transcoding "
                            "also failed: " + softwareError;
                    }
                }

                if(result.completed)
                {
                    result.outputBytes = std::filesystem::file_size(
                        outputPath, cleanupError);
                    if(cleanupError || result.outputBytes == 0)
                    {
                        result.completed = false;
                        result.detail =
                            "The H.264 transcoder produced no usable output.";
                    }
                }
                if(!result.completed)
                    std::filesystem::remove(outputPath, cleanupError);
                return result;
            }

        private:
            static bool TranscodeWithEncoder(
                const std::filesystem::path& inputPath,
                const std::filesystem::path& outputPath,
                const char* encoderName,
                void* javaVm,
                const ProgressCallback& progress,
                const CancellationCheck& cancelled,
                std::string& error)
            {
                AVFormatContext* input = nullptr;
                AVFormatContext* output = nullptr;
                AVCodecContext* decoder = nullptr;
                AVCodecContext* encoder = nullptr;
                AVPacket* inputPacket = nullptr;
                AVPacket* outputPacket = nullptr;
                AVFrame* decoded = nullptr;
                AVFrame* converted = nullptr;
                SwsContext* converter = nullptr;
                bool headerWritten = false;
                bool trailerWritten = false;
                int status = 0;
                const bool hardwarePath =
                    std::strcmp(encoderName, "h264_mediacodec") == 0;

                auto cleanup = [&]()
                {
                    if(converter)
                        sws_freeContext(converter);
                    if(converted)
                        av_frame_free(&converted);
                    if(decoded)
                        av_frame_free(&decoded);
                    if(outputPacket)
                        av_packet_free(&outputPacket);
                    if(inputPacket)
                        av_packet_free(&inputPacket);
                    if(encoder)
                        avcodec_free_context(&encoder);
                    if(decoder)
                        avcodec_free_context(&decoder);
                    if(output)
                    {
                        if(headerWritten && !trailerWritten)
                            av_write_trailer(output);
                        if(output->pb)
                            avio_closep(&output->pb);
                        avformat_free_context(output);
                    }
                    if(input)
                        avformat_close_input(&input);
                };

                status = avformat_open_input(
                    &input, inputPath.string().c_str(), nullptr, nullptr);
                if(status < 0)
                {
                    error = "Could not open the fallback video: " +
                        FfmpegError(status);
                    cleanup();
                    return false;
                }
                status = avformat_find_stream_info(input, nullptr);
                const int videoStream = status >= 0
                    ? av_find_best_stream(
                        input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0)
                    : status;
                if(status < 0 || videoStream < 0)
                {
                    error = "Could not inspect the fallback video: " +
                        FfmpegError(status < 0 ? status : videoStream);
                    cleanup();
                    return false;
                }

                AVStream* inputStream = input->streams[videoStream];
                if(hardwarePath)
                {
                    if(!javaVm)
                    {
                        error = "Android did not provide the Java VM required "
                            "by the hardware video transcoder.";
                        cleanup();
                        return false;
                    }
                    status = av_jni_set_java_vm(javaVm, nullptr);
                    if(status < 0)
                    {
                        error = "Could not register Android with the hardware "
                            "video transcoder: " + FfmpegError(status);
                        cleanup();
                        return false;
                    }
                }

                const char* hardwareDecoderName = nullptr;
                if(hardwarePath)
                {
                    switch(inputStream->codecpar->codec_id)
                    {
                        case AV_CODEC_ID_H264:
                            hardwareDecoderName = "h264_mediacodec";
                            break;
                        case AV_CODEC_ID_HEVC:
                            hardwareDecoderName = "hevc_mediacodec";
                            break;
                        case AV_CODEC_ID_VP8:
                            hardwareDecoderName = "vp8_mediacodec";
                            break;
                        case AV_CODEC_ID_VP9:
                            hardwareDecoderName = "vp9_mediacodec";
                            break;
                        default:
                            break;
                    }
                }
                const AVCodec* decoderCodec = hardwareDecoderName
                    ? avcodec_find_decoder_by_name(hardwareDecoderName)
                    : (hardwarePath
                        ? nullptr
                        : avcodec_find_decoder(
                            inputStream->codecpar->codec_id));
                decoder = decoderCodec
                    ? avcodec_alloc_context3(decoderCodec)
                    : nullptr;
                if(!decoderCodec || !decoder)
                {
                    error = hardwarePath
                        ? "No Android MediaCodec decoder is available for the "
                            "fallback video's codec."
                        : "No software decoder is available for the fallback "
                            "video.";
                    cleanup();
                    return false;
                }
                status = avcodec_parameters_to_context(
                    decoder, inputStream->codecpar);
                if(status >= 0)
                {
                    decoder->thread_count = 2;
                    status = avcodec_open2(decoder, decoderCodec, nullptr);
                }
                if(status < 0)
                {
                    error = "Could not open the fallback decoder: " +
                        FfmpegError(status);
                    cleanup();
                    return false;
                }

                const AVCodec* encoderCodec =
                    avcodec_find_encoder_by_name(encoderName);
                encoder = encoderCodec
                    ? avcodec_alloc_context3(encoderCodec)
                    : nullptr;
                if(!encoderCodec || !encoder)
                {
                    error = std::string("The ") + encoderName +
                        " H.264 encoder is not available in this runtime.";
                    cleanup();
                    return false;
                }

                const int width = decoder->width & ~1;
                const int height = decoder->height & ~1;
                AVRational frameRate = av_guess_frame_rate(
                    input, inputStream, nullptr);
                if(frameRate.num <= 0 || frameRate.den <= 0 ||
                   av_q2d(frameRate) < 1.0 || av_q2d(frameRate) > 240.0)
                    frameRate = AVRational{30, 1};
                encoder->width = width;
                encoder->height = height;
                encoder->pix_fmt = AV_PIX_FMT_YUV420P;
                encoder->time_base = av_inv_q(frameRate);
                encoder->framerate = frameRate;
                encoder->gop_size = std::max(
                    1, static_cast<int>(std::lround(av_q2d(frameRate) * 2.0)));
                encoder->max_b_frames = 0;
                encoder->profile = AV_PROFILE_H264_HIGH;
                const std::int64_t estimatedBitrate = static_cast<std::int64_t>(
                    static_cast<double>(width) * height * av_q2d(frameRate) *
                    0.12);
                encoder->bit_rate = std::clamp<std::int64_t>(
                    inputStream->codecpar->bit_rate > 0
                        ? inputStream->codecpar->bit_rate
                        : estimatedBitrate,
                    2'000'000, 24'000'000);
                encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                if(std::strcmp(encoderName, "libx264") == 0)
                {
                    av_opt_set(encoder->priv_data, "preset", "veryfast", 0);
                    av_opt_set(encoder->priv_data, "crf", "20", 0);
                }
                else
                {
                    av_opt_set(encoder->priv_data, "bitrate_mode", "vbr", 0);
                }
                status = avcodec_open2(encoder, encoderCodec, nullptr);
                if(status < 0)
                {
                    error = std::string("Could not start the ") + encoderName +
                        " H.264 encoder: " + FfmpegError(status);
                    cleanup();
                    return false;
                }

                status = avformat_alloc_output_context2(
                    &output, nullptr, "mp4", outputPath.string().c_str());
                AVStream* outputStream = output
                    ? avformat_new_stream(output, nullptr)
                    : nullptr;
                if(status < 0 || !output || !outputStream)
                {
                    error = "Could not create the transcoded MP4 container.";
                    cleanup();
                    return false;
                }
                outputStream->time_base = encoder->time_base;
                status = avcodec_parameters_from_context(
                    outputStream->codecpar, encoder);
                if(status >= 0)
                    status = avio_open(
                        &output->pb, outputPath.string().c_str(), AVIO_FLAG_WRITE);
                if(status >= 0)
                    status = avformat_write_header(output, nullptr);
                if(status < 0)
                {
                    error = "Could not start the transcoded MP4: " +
                        FfmpegError(status);
                    cleanup();
                    return false;
                }
                headerWritten = true;

                inputPacket = av_packet_alloc();
                outputPacket = av_packet_alloc();
                decoded = av_frame_alloc();
                converted = av_frame_alloc();
                if(!inputPacket || !outputPacket || !decoded || !converted)
                {
                    error = "Could not allocate transcoder frame buffers.";
                    cleanup();
                    return false;
                }
                converted->format = encoder->pix_fmt;
                converted->width = width;
                converted->height = height;
                status = av_frame_get_buffer(converted, 32);
                if(status < 0)
                {
                    error = "Could not allocate the H.264 input frame: " +
                        FfmpegError(status);
                    cleanup();
                    return false;
                }

                std::error_code sizeError;
                const auto inputBytes = std::filesystem::file_size(
                    inputPath, sizeError);
                std::int64_t nextFramePts = 0;
                bool wrotePacket = false;

                auto drainEncoder = [&]()
                {
                    while(true)
                    {
                        const int receive = avcodec_receive_packet(
                            encoder, outputPacket);
                        if(receive == AVERROR(EAGAIN) || receive == AVERROR_EOF)
                            return 0;
                        if(receive < 0)
                            return receive;
                        av_packet_rescale_ts(
                            outputPacket, encoder->time_base,
                            outputStream->time_base);
                        outputPacket->stream_index = outputStream->index;
                        const int write = av_interleaved_write_frame(
                            output, outputPacket);
                        av_packet_unref(outputPacket);
                        if(write < 0)
                            return write;
                        wrotePacket = true;
                    }
                };

                auto encodeFrame = [&](AVFrame* frame)
                {
                    int send = avcodec_send_frame(encoder, frame);
                    if(send == AVERROR(EAGAIN))
                    {
                        const int drained = drainEncoder();
                        if(drained < 0)
                            return drained;
                        send = avcodec_send_frame(encoder, frame);
                    }
                    if(send < 0)
                        return send;
                    return drainEncoder();
                };

                auto convertAndEncode = [&]()
                {
                    int writable = av_frame_make_writable(converted);
                    if(writable < 0)
                        return writable;
                    converter = sws_getCachedContext(
                        converter,
                        decoded->width,
                        decoded->height,
                        static_cast<AVPixelFormat>(decoded->format),
                        width,
                        height,
                        AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR,
                        nullptr,
                        nullptr,
                        nullptr);
                    if(!converter)
                        return AVERROR(ENOMEM);
                    sws_scale(
                        converter,
                        decoded->data,
                        decoded->linesize,
                        0,
                        decoded->height,
                        converted->data,
                        converted->linesize);
                    const std::int64_t timestamp =
                        decoded->best_effort_timestamp != AV_NOPTS_VALUE
                            ? av_rescale_q(
                                decoded->best_effort_timestamp -
                                    (inputStream->start_time == AV_NOPTS_VALUE
                                        ? 0
                                        : inputStream->start_time),
                                inputStream->time_base,
                                encoder->time_base)
                            : nextFramePts;
                    converted->pts = std::max(nextFramePts, timestamp);
                    nextFramePts = converted->pts + 1;
                    converted->color_range = decoded->color_range;
                    converted->colorspace = decoded->colorspace;
                    converted->color_primaries = decoded->color_primaries;
                    converted->color_trc = decoded->color_trc;
                    return encodeFrame(converted);
                };

                auto drainDecoder = [&]()
                {
                    while(true)
                    {
                        const int receive = avcodec_receive_frame(
                            decoder, decoded);
                        if(receive == AVERROR(EAGAIN) || receive == AVERROR_EOF)
                            return 0;
                        if(receive < 0)
                            return receive;
                        const int encode = convertAndEncode();
                        av_frame_unref(decoded);
                        if(encode < 0)
                            return encode;
                    }
                };

                while((status = av_read_frame(input, inputPacket)) >= 0)
                {
                    if(cancelled && cancelled())
                    {
                        error = "Video transcoding was cancelled.";
                        cleanup();
                        return false;
                    }
                    if(inputPacket->stream_index == videoStream)
                    {
                        status = avcodec_send_packet(decoder, inputPacket);
                        if(status == AVERROR(EAGAIN))
                        {
                            status = drainDecoder();
                            if(status >= 0)
                                status = avcodec_send_packet(
                                    decoder, inputPacket);
                        }
                        if(status >= 0)
                            status = drainDecoder();
                    }
                    av_packet_unref(inputPacket);
                    if(status < 0)
                        break;
                    if(progress && !sizeError && input->pb)
                    {
                        const auto position = std::max<std::int64_t>(
                            0, avio_tell(input->pb));
                        progress(
                            std::min<std::uint64_t>(position, inputBytes),
                            inputBytes);
                    }
                }
                if(status == AVERROR_EOF)
                {
                    status = avcodec_send_packet(decoder, nullptr);
                    if(status == AVERROR(EAGAIN))
                    {
                        status = drainDecoder();
                        if(status >= 0)
                            status = avcodec_send_packet(decoder, nullptr);
                    }
                }
                if(status >= 0)
                    status = drainDecoder();
                if(status >= 0)
                    status = encodeFrame(nullptr);
                if(status >= 0)
                    status = av_write_trailer(output);
                if(status >= 0)
                    trailerWritten = true;

                if(status < 0 || !wrotePacket)
                {
                    error = std::string("The ") + encoderName +
                        " transcode failed while processing frames: " +
                        (status < 0 ? FfmpegError(status)
                                    : "no video packets were produced");
                    cleanup();
                    return false;
                }
                if(progress && !sizeError)
                    progress(inputBytes, inputBytes);
                cleanup();
                return true;
            }
        };
    }

    BIGSCREEN_TRANSCODER_BACKEND_EXPORT
    std::unique_ptr<VideoTranscoderBackend> CreateVideoTranscoder9Backend()
    {
        return std::make_unique<NativeVideoTranscoder>();
    }
}
