// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/VideoContainerNormalizer.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
}

namespace BigScreen {
    namespace {
        std::string FfmpegError(int status)
        {
            char buffer[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(status, buffer, sizeof(buffer));
            return buffer;
        }

        bool FormatNameContains(
            const AVFormatContext* format,
            const char* expected)
        {
            if(!format || !format->iformat || !format->iformat->name)
                return false;
            const std::string names(format->iformat->name);
            std::size_t start = 0;
            while(start <= names.size())
            {
                const auto end = names.find(',', start);
                if(names.substr(start, end - start) == expected)
                    return true;
                if(end == std::string::npos)
                    break;
                start = end + 1;
            }
            return false;
        }

        int OpenVideoInput(
            const std::filesystem::path& path,
            AVFormatContext*& format,
            int& videoStream,
            std::string& error)
        {
            int status = avformat_open_input(
                &format, path.string().c_str(), nullptr, nullptr);
            if(status < 0)
            {
                error = "Could not open the downloaded stream: " +
                    FfmpegError(status);
                return status;
            }
            status = avformat_find_stream_info(format, nullptr);
            if(status < 0)
            {
                error = "Could not read the downloaded stream information: " +
                    FfmpegError(status);
                return status;
            }
            videoStream = av_find_best_stream(
                format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if(videoStream < 0)
            {
                error = "The downloaded file does not contain a video stream.";
                return videoStream;
            }
            return 0;
        }

        bool CanDecodeSoftwareFrame(
            const std::filesystem::path& path,
            std::string& error)
        {
            AVFormatContext* format = nullptr;
            int videoStream = -1;
            if(OpenVideoInput(path, format, videoStream, error) < 0)
            {
                if(format)
                    avformat_close_input(&format);
                return false;
            }

            const AVCodecParameters* parameters =
                format->streams[videoStream]->codecpar;
            const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
            AVCodecContext* context = codec ? avcodec_alloc_context3(codec) : nullptr;
            AVFrame* frame = av_frame_alloc();
            AVPacket* packet = av_packet_alloc();
            bool decoded = false;
            if(!codec || !context || !frame || !packet)
                error = "The software decoder could not be allocated.";
            else
            {
                int status = avcodec_parameters_to_context(context, parameters);
                if(status >= 0)
                {
                    // This is a viability probe, not playback. One worker is
                    // sufficient and avoids briefly consuming the CPU budget
                    // of an active Beat Saber menu while the download closes.
                    context->thread_count = 1;
                    status = avcodec_open2(context, codec, nullptr);
                }
                if(status < 0)
                    error = "The software decoder could not open this stream: " +
                        FfmpegError(status);
                else
                {
                    int inspectedPackets = 0;
                    while(inspectedPackets < 600 &&
                          av_read_frame(format, packet) >= 0)
                    {
                        if(packet->stream_index == videoStream)
                        {
                            ++inspectedPackets;
                            status = avcodec_send_packet(context, packet);
                            if(status >= 0 || status == AVERROR(EAGAIN))
                            {
                                status = avcodec_receive_frame(context, frame);
                                if(status >= 0)
                                {
                                    decoded = true;
                                    break;
                                }
                            }
                        }
                        av_packet_unref(packet);
                    }
                    if(!decoded)
                        error = "The software decoder could not produce a test frame.";
                }
            }

            if(packet)
                av_packet_free(&packet);
            if(frame)
                av_frame_free(&frame);
            if(context)
                avcodec_free_context(&context);
            avformat_close_input(&format);
            return decoded;
        }

        bool ValidateRemuxedMp4(
            const std::filesystem::path& path,
            std::string& error)
        {
            AVFormatContext* format = nullptr;
            int videoStream = -1;
            if(OpenVideoInput(path, format, videoStream, error) < 0)
            {
                if(format)
                    avformat_close_input(&format);
                return false;
            }
            const auto* parameters = format->streams[videoStream]->codecpar;
            const bool valid = FormatNameContains(format, "mov") &&
                parameters && parameters->codec_id == AV_CODEC_ID_H264 &&
                parameters->width > 0 && parameters->height > 0;
            if(!valid)
                error = "The prepared file did not validate as an H.264 MP4.";
            avformat_close_input(&format);
            return valid;
        }

        bool RemuxMpegTsToMp4(
            AVFormatContext* input,
            int inputVideoStream,
            const std::filesystem::path& outputPath,
            std::uint64_t inputBytes,
            const VideoContainerNormalizer::ProgressCallback& progress,
            const VideoContainerNormalizer::CancellationCheck& cancelled,
            std::string& error)
        {
            AVFormatContext* output = nullptr;
            int status = avformat_alloc_output_context2(
                &output, nullptr, "mp4", outputPath.string().c_str());
            if(status < 0 || !output)
            {
                error = "Could not create the MP4 container: " +
                    FfmpegError(status < 0 ? status : AVERROR_UNKNOWN);
                return false;
            }

            AVStream* inputStream = input->streams[inputVideoStream];
            AVStream* outputStream = avformat_new_stream(output, nullptr);
            if(!outputStream)
            {
                error = "Could not create the MP4 video track.";
                avformat_free_context(output);
                return false;
            }
            status = avcodec_parameters_copy(
                outputStream->codecpar, inputStream->codecpar);
            if(status >= 0)
            {
                outputStream->codecpar->codec_tag = 0;
                outputStream->time_base = inputStream->time_base;
                if(!(output->oformat->flags & AVFMT_NOFILE))
                    status = avio_open(
                        &output->pb,
                        outputPath.string().c_str(),
                        AVIO_FLAG_WRITE);
            }
            if(status >= 0)
                status = avformat_write_header(output, nullptr);
            if(status < 0)
            {
                error = "Could not start the MP4 file: " + FfmpegError(status);
                if(output->pb)
                    avio_closep(&output->pb);
                avformat_free_context(output);
                return false;
            }

            AVPacket* packet = av_packet_alloc();
            if(!packet)
            {
                error = "Could not allocate an MP4 packet.";
                av_write_trailer(output);
                avio_closep(&output->pb);
                avformat_free_context(output);
                return false;
            }

            const auto started = std::chrono::steady_clock::now();
            auto lastProgress = started - std::chrono::seconds(1);
            const std::int64_t inputStart = inputStream->start_time;
            bool wrotePacket = false;
            bool wasCancelled = false;
            while((status = av_read_frame(input, packet)) >= 0)
            {
                if(cancelled && cancelled())
                {
                    wasCancelled = true;
                    av_packet_unref(packet);
                    break;
                }
                if(packet->stream_index == inputVideoStream)
                {
                    if(inputStart != AV_NOPTS_VALUE)
                    {
                        if(packet->pts != AV_NOPTS_VALUE)
                            packet->pts -= inputStart;
                        if(packet->dts != AV_NOPTS_VALUE)
                            packet->dts -= inputStart;
                    }
                    av_packet_rescale_ts(
                        packet, inputStream->time_base, outputStream->time_base);
                    packet->stream_index = outputStream->index;
                    packet->pos = -1;
                    status = av_interleaved_write_frame(output, packet);
                    if(status < 0)
                    {
                        error = "Could not write the MP4 video track: " +
                            FfmpegError(status);
                        av_packet_unref(packet);
                        break;
                    }
                    wrotePacket = true;
                }
                av_packet_unref(packet);

                const auto now = std::chrono::steady_clock::now();
                if(progress && now - lastProgress >=
                       std::chrono::milliseconds(125))
                {
                    const auto position = input->pb
                        ? std::max<std::int64_t>(0, avio_tell(input->pb))
                        : 0;
                    progress(
                        std::min<std::uint64_t>(
                            static_cast<std::uint64_t>(position), inputBytes),
                        inputBytes);
                    lastProgress = now;
                }
            }

            if(!wasCancelled && status == AVERROR_EOF)
                status = 0;
            if(!wasCancelled && status >= 0 && wrotePacket)
                status = av_write_trailer(output);
            else
                av_write_trailer(output);
            av_packet_free(&packet);
            if(output->pb)
                avio_closep(&output->pb);
            avformat_free_context(output);

            if(wasCancelled)
            {
                error = "Video preparation was cancelled.";
                return false;
            }
            if(status < 0)
            {
                if(error.empty())
                    error = "Could not finish the MP4 file: " +
                        FfmpegError(status);
                return false;
            }
            if(!wrotePacket)
            {
                error = "The downloaded stream did not contain video packets.";
                return false;
            }
            if(progress)
                progress(inputBytes, inputBytes);
            return true;
        }
    }

    VideoNormalizationResult VideoContainerNormalizer::PrepareDownloadedVideo(
        const std::filesystem::path& downloadedPath,
        const std::filesystem::path& remuxedPath,
        int requestedHeight,
        const ProgressCallback& progress,
        const CancellationCheck& cancelled)
    {
        VideoNormalizationResult result;
        std::error_code sizeError;
        const auto inputBytes = std::filesystem::file_size(
            downloadedPath, sizeError);
        if(sizeError || inputBytes == 0)
        {
            result.detail = "The downloaded video file is empty or unavailable.";
            return result;
        }

        AVFormatContext* input = nullptr;
        int videoStream = -1;
        if(OpenVideoInput(
               downloadedPath, input, videoStream, result.detail) < 0)
        {
            if(input)
                avformat_close_input(&input);
            return result;
        }
        const auto* parameters = input->streams[videoStream]->codecpar;
        const bool isMpegTs = FormatNameContains(input, "mpegts");
        const bool isMp4 = FormatNameContains(input, "mov");
        const bool isWebm = FormatNameContains(input, "matroska");
        const bool expectedCodec = parameters &&
            (parameters->codec_id == AV_CODEC_ID_H264 ||
             parameters->codec_id == AV_CODEC_ID_VP9);

        if((isMp4 || isWebm) && expectedCodec &&
           parameters->width > 0 && parameters->height > 0)
        {
            avformat_close_input(&input);
            result.state = VideoNormalizationState::Ready;
            result.outputBytes = inputBytes;
            return result;
        }
        if(!isMpegTs || !parameters ||
           parameters->codec_id != AV_CODEC_ID_H264)
        {
            avformat_close_input(&input);
            result.detail =
                "The downloaded stream is not a supported MP4, WebM, or H.264 MPEG-TS file.";
            return result;
        }

        std::error_code cleanupError;
        std::filesystem::remove(remuxedPath, cleanupError);
        std::string remuxError;
        const bool remuxed = RemuxMpegTsToMp4(
            input,
            videoStream,
            remuxedPath,
            inputBytes,
            progress,
            cancelled,
            remuxError);
        avformat_close_input(&input);

        if(cancelled && cancelled())
        {
            std::filesystem::remove(remuxedPath, cleanupError);
            result.state = VideoNormalizationState::Cancelled;
            result.detail = "Video preparation was cancelled.";
            return result;
        }
        if(remuxed)
        {
            std::string validationError;
            if(ValidateRemuxedMp4(remuxedPath, validationError))
            {
                result.state = VideoNormalizationState::Remuxed;
                result.outputBytes = std::filesystem::file_size(
                    remuxedPath, sizeError);
                if(sizeError)
                    result.outputBytes = 0;
                return result;
            }
            remuxError = "MP4 validation failed: " + validationError;
        }
        std::filesystem::remove(remuxedPath, cleanupError);

        // A failed container conversion must never turn a usable download
        // into a hard error. Confirm that Big Screen's software H.264 decoder
        // can actually produce a picture before accepting the original TS.
        std::string softwareError;
        if(requestedHeight <= 1080 &&
           CanDecodeSoftwareFrame(downloadedPath, softwareError))
        {
            result.state = VideoNormalizationState::SoftwareDecoderRequired;
            result.outputBytes = inputBytes;
            result.detail = remuxError.empty()
                ? "The H.264 stream could not be converted to MP4."
                : remuxError;
            return result;
        }

        result.state = VideoNormalizationState::Failed;
        result.detail = (remuxError.empty()
            ? "The H.264 stream could not be converted to MP4."
            : remuxError) + " Software validation also failed: " + softwareError;
        return result;
    }
}
