// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncReader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libswresample/swresample.h"
}

namespace BigScreen::AudioSync
{
namespace
{
std::string Detail(const char* operation, int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return std::string(operation) + ": " + text;
}

struct Decoder final
{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* resampler = nullptr;
    int stream = -1;
    const CancelCheck* cancelled = nullptr;
    ~Decoder()
    {
        swr_free(&resampler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    static int Interrupt(void* opaque) noexcept
    {
        const auto& self = *static_cast<Decoder*>(opaque);
        try
        {
            return self.cancelled && *self.cancelled && (*self.cancelled)();
        }
        catch(...)
        {
            return 1;
        } // Never unwind through FFmpeg's C callbacks.
    }
    AudioInfo Open(const std::filesystem::path& path, const CancelCheck& cancel)
    {
        AudioInfo info;
        cancelled = &cancel;
        format = avformat_alloc_context();
        if(!format)
            throw std::bad_alloc();
        format->interrupt_callback = {Interrupt, this};
        // Bound format probing. Audio is local here: network access
        // belongs to the existing downloader and its cancellation path.
        format->probesize = 1024 * 1024;
        format->max_analyze_duration = 2 * AV_TIME_BASE;
        int result = avformat_open_input(&format, path.string().c_str(), nullptr, nullptr);
        if(result < 0)
        {
            info.error = Detail("Could not open audio", result);
            return info;
        }
        result = avformat_find_stream_info(format, nullptr);
        if(result < 0)
        {
            info.error = Detail("Could not inspect audio", result);
            return info;
        }
        stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if(stream < 0)
        {
            info.error = "This video contains no usable audio stream.";
            return info;
        }
        const auto* parameters = format->streams[stream]->codecpar;
        const auto* implementation = avcodec_find_decoder(parameters->codec_id);
        if(!implementation)
        {
            info.error = "This audio codec is not supported by Big Screen.";
            return info;
        }
        codec = avcodec_alloc_context3(implementation);
        if(!codec)
            throw std::bad_alloc();
        result = avcodec_parameters_to_context(codec, parameters);
        if(result < 0)
        {
            info.error = Detail("Invalid audio parameters", result);
            return info;
        }
        codec->thread_count = 1;
        // AAC priming and Opus pre-skip adjust frame PTS inside the
        // decoder. Without pkt_timebase FFmpeg 4 cannot express that
        // adjustment, so Opus's first trimmed frame and its successor
        // disagree about time (and seeks emit the wrong window size).
        codec->pkt_timebase = format->streams[stream]->time_base;
        result = avcodec_open2(codec, implementation, nullptr);
        if(result < 0)
        {
            info.error = Detail("Could not start audio decoder", result);
            return info;
        }
        if(codec->sample_rate < 8000 || codec->sample_rate > 192000 || codec->channels < 1 ||
           codec->channels > 8)
        {
            info.error = "Audio sample rate or channel count exceeds the supported limits.";
            return info;
        }
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if(!packet || !frame)
            throw std::bad_alloc();
        info.available = true;
        info.codec = implementation->name;
        info.sampleRate = codec->sample_rate;
        info.channels = codec->channels;
        const auto* track = format->streams[stream];
        info.streamStartSeconds = track->start_time == AV_NOPTS_VALUE
                                      ? 0.0
                                      : track->start_time * av_q2d(track->time_base);
        info.durationSeconds =
            track->duration != AV_NOPTS_VALUE
                ? track->duration * av_q2d(track->time_base)
                : (format->duration != AV_NOPTS_VALUE ? format->duration / double(AV_TIME_BASE)
                                                      : 0.0);
        return info;
    }
};
} // namespace

AudioInfo ProbeAudio(const std::filesystem::path& path, const CancelCheck& cancelled)
{
    Decoder decoder;
    return decoder.Open(path, cancelled);
}

std::string SourceFingerprint(const std::filesystem::path& path)
{
    std::error_code error;
    if(!std::filesystem::is_regular_file(path, error))
        return {};
    const auto size = std::filesystem::file_size(path, error);
    if(error)
        return {};
    const auto stamp = std::filesystem::last_write_time(path, error);
    if(error)
        return {};
    return path.lexically_normal().string() + "|" + std::to_string(size) + "|" +
           // Android libc++ may use a 128-bit file-clock representation;
           // serialize an explicit portable unit instead of overloading
           // to_string on its implementation-defined tick type.
           std::to_string(
               std::chrono::duration_cast<std::chrono::microseconds>(stamp.time_since_epoch())
                   .count());
}

std::optional<double> VideoStartTime(const std::filesystem::path& path,
                                     const CancelCheck& cancelled, std::string& error)
{
    Decoder input;
    input.cancelled = &cancelled;
    input.format = avformat_alloc_context();
    if(!input.format)
        throw std::bad_alloc();
    input.format->interrupt_callback = {Decoder::Interrupt, &input};
    input.format->probesize = 1024 * 1024;
    input.format->max_analyze_duration = 2 * AV_TIME_BASE;
    int code = avformat_open_input(&input.format, path.string().c_str(), nullptr, nullptr);
    if(code >= 0)
        code = avformat_find_stream_info(input.format, nullptr);
    if(code < 0)
    {
        error = Detail("Could not inspect the final video timeline", code);
        return std::nullopt;
    }
    const int stream = av_find_best_stream(input.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if(stream < 0)
    {
        error = "No video timeline was found.";
        return std::nullopt;
    }
    const auto* track = input.format->streams[stream];
    if(track->start_time == AV_NOPTS_VALUE)
    {
        error = "The final video's timeline origin is unknown. Advanced Sync cannot safely pair "
                "separate "
                "audio.";
        return std::nullopt;
    }
    return track->start_time * av_q2d(track->time_base);
}

ReadOutcome ReadAudio(const ReadRequest& request,
                      const std::function<bool(std::span<const float>, double)>& sink)
{
    ReadOutcome outcome;
    if(!std::isfinite(request.startSeconds) || !std::isfinite(request.endSeconds) ||
       !std::isfinite(request.sourceToFinalShiftSeconds) ||
       std::abs(request.sourceToFinalShiftSeconds) > 86400 || request.startSeconds < 0 ||
       request.endSeconds <= request.startSeconds || request.endSeconds > 86400 ||
       request.outputRate < 1000 || request.outputRate > 48000 || !sink)
    {
        outcome.error = "Invalid audio window request.";
        return outcome;
    }
    Decoder d;
    outcome.info = d.Open(request.path, request.cancelled);
    auto cancelled = [&]() { return request.cancelled && request.cancelled(); };
    if(cancelled())
    {
        outcome.cancelled = true;
        return outcome;
    }
    if(!outcome.info.available)
    {
        outcome.error = outcome.info.error;
        return outcome;
    }
    auto* track = d.format->streams[d.stream];
    if(request.startSeconds > 0.0)
    {
        const double rawTime = request.startSeconds - request.sourceToFinalShiftSeconds;
        const auto timestamp = static_cast<std::int64_t>(rawTime / av_q2d(track->time_base));
        const int result = av_seek_frame(d.format, d.stream, timestamp, AVSEEK_FLAG_BACKWARD);
        if(result < 0)
        {
            outcome.error = Detail("Could not seek audio", result);
            return outcome;
        }
        avcodec_flush_buffers(d.codec);
    }
    const auto layout =
        d.codec->channel_layout
            ? d.codec->channel_layout
            : static_cast<std::uint64_t>(av_get_default_channel_layout(d.codec->channels));
    d.resampler = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_FLT,
                                     request.outputRate, static_cast<std::int64_t>(layout),
                                     d.codec->sample_fmt, d.codec->sample_rate, 0, nullptr);
    if(!d.resampler)
        throw std::bad_alloc();
    int status = swr_init(d.resampler);
    if(status < 0)
    {
        outcome.error = Detail("Could not prepare audio resampling", status);
        return outcome;
    }
    // Mono mixing uses FFmpeg's channel-layout matrix, so stereo panning
    // cannot make one comparison ear unexpectedly lose the reference.
    // swresample supplies antialias filtering; simply dropping samples
    // would create false matches and audible artifacts above Nyquist.
    std::vector<float> output(32768);
    std::optional<double> nextOutput;
    bool stop = false;
    auto deliver = [&](int count, double time)
    {
        const auto first = static_cast<std::size_t>(std::clamp(
            std::ceil((request.startSeconds - time) * request.outputRate), 0.0, double(count)));
        const auto last = static_cast<std::size_t>(std::clamp(
            std::ceil((request.endSeconds - time) * request.outputRate), 0.0, double(count)));
        if(last > first)
        {
            outcome.outputSamples += last - first;
            if(!sink(std::span<const float>(output.data() + first, last - first),
                     time + double(first) / request.outputRate))
                stop = true;
        }
        nextOutput = time + double(count) / request.outputRate;
        if(*nextOutput >= request.endSeconds)
            stop = true;
    };
    auto drain = [&]() -> int
    {
        while(!cancelled() && !stop)
        {
            const int read = avcodec_receive_frame(d.codec, d.frame);
            if(read == AVERROR(EAGAIN) || read == AVERROR_EOF)
                return read;
            if(read < 0)
            {
                outcome.error = Detail("Audio decoding failed", read);
                return read;
            }
            if(d.frame->sample_rate != d.codec->sample_rate ||
               d.frame->format != d.codec->sample_fmt || d.frame->nb_samples < 0 ||
               d.frame->nb_samples > 32768)
            {
                outcome.error = "Audio format changed or produced an oversized frame.";
                return AVERROR_INVALIDDATA;
            }
            const auto pts = d.frame->best_effort_timestamp != AV_NOPTS_VALUE
                                 ? d.frame->best_effort_timestamp
                                 : d.frame->pts;
            if(pts == AV_NOPTS_VALUE)
            {
                outcome.error = "Audio has no reliable timestamps for synchronization.";
                return AVERROR_INVALIDDATA;
            }
            const double rawTime =
                pts * av_q2d(track->time_base) + request.sourceToFinalShiftSeconds;
            const double delayed =
                double(swr_get_delay(d.resampler, d.codec->sample_rate)) / d.codec->sample_rate;
            double time = rawTime - delayed;
            // swr retains a short filter tail. Its first output therefore
            // precedes the current input PTS by that delay. For contiguous
            // packets, use the precise output sample clock to avoid tiny
            // container time-base rounding gaps accumulating over minutes.
            // Vorbis uses lapped short/long transform blocks. Some valid map
            // encoders expose the packet granule position as the frame PTS
            // while nb_samples still describes the decoder's overlap-added
            // PCM. At a block-size transition those two clocks can differ by
            // roughly 10.2 ms even though the decoded samples are continuous.
            // FFmpeg has already performed the codec overlap-add, so the
            // resampler's output sample clock is authoritative inside this
            // narrow Vorbis-only tolerance. Other codecs retain the stricter
            // five-millisecond rule, and larger jumps still reset swresample
            // so a real edit-list/timeline discontinuity is preserved.
            const double continuityTolerance =
                d.codec->codec_id == AV_CODEC_ID_VORBIS ? .015 : .005;
            if(nextOutput && std::abs(time - *nextOutput) <= continuityTolerance)
                time = *nextOutput;
            else if(nextOutput)
            {
                swr_close(d.resampler);
                if(swr_init(d.resampler) < 0)
                {
                    outcome.error = "Could not reset audio after a timestamp discontinuity.";
                    return AVERROR_INVALIDDATA;
                }
                time = rawTime;
            }
            std::uint8_t* destination = reinterpret_cast<std::uint8_t*>(output.data());
            const int capacity = swr_get_out_samples(d.resampler, d.frame->nb_samples);
            if(capacity < 0 || capacity > static_cast<int>(output.size()))
            {
                outcome.error = "Audio resampling exceeds its working-memory limit.";
                return AVERROR_INVALIDDATA;
            }
            const int count = swr_convert(
                d.resampler, &destination, static_cast<int>(output.size()),
                const_cast<const std::uint8_t**>(d.frame->extended_data), d.frame->nb_samples);
            av_frame_unref(d.frame);
            if(count < 0)
            {
                outcome.error = Detail("Audio resampling failed", count);
                return count;
            }
            if(count > 0)
                deliver(count, time);
        }
        return 0;
    };
    while(!cancelled() && !stop && outcome.error.empty())
    {
        status = av_read_frame(d.format, d.packet);
        if(status < 0)
        {
            if(status != AVERROR_EOF)
                outcome.error = Detail("Reading audio failed", status);
            else
            {
                status = avcodec_send_packet(d.codec, nullptr);
                if(status >= 0)
                    drain();
                else if(status != AVERROR_EOF)
                    outcome.error = Detail("Flushing audio failed", status);
            }
            break;
        }
        if(d.packet->stream_index == d.stream)
        {
            status = avcodec_send_packet(d.codec, d.packet);
            if(status == AVERROR(EAGAIN))
            {
                drain();
                status = avcodec_send_packet(d.codec, d.packet);
            }
            if(status < 0)
                outcome.error = Detail("Submitting audio failed", status);
            else
                drain();
        }
        av_packet_unref(d.packet);
    }
    while(!cancelled() && !stop && outcome.error.empty() && nextOutput)
    {
        std::uint8_t* destination = reinterpret_cast<std::uint8_t*>(output.data());
        const int count =
            swr_convert(d.resampler, &destination, static_cast<int>(output.size()), nullptr, 0);
        if(count < 0)
        {
            outcome.error = Detail("Flushing resampled audio failed", count);
            break;
        }
        if(count == 0)
            break;
        deliver(count, *nextOutput);
    }
    outcome.cancelled = cancelled();
    if(outcome.cancelled)
        outcome.error.clear();
    if(!outcome.cancelled && outcome.error.empty() && outcome.outputSamples == 0)
        outcome.error = "No audio samples were available in the selected window.";
    return outcome;
}
} // namespace BigScreen::AudioSync
