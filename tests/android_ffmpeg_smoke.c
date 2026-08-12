/*
 * Standalone on-device smoke test for Big Screen's private FFmpeg runtime.
 *
 * This intentionally exercises the same demux/decode/scale path as the mod
 * without starting Beat Saber. It lets maintainers validate Android's dynamic
 * linker, the private SONAMEs and symbol versions, and one real H.264 frame
 * even when the Quest controller launch gate prevents automated game startup.
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void print_ffmpeg_error(const char *operation, int error_code) {
    char message[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(error_code, message, sizeof(message));
    fprintf(stderr, "%s failed: %s (%d)\n", operation, message, error_code);
}

int main(int argc, char **argv) {
    AVFormatContext *format = NULL;
    AVCodecContext *codec = NULL;
    AVPacket *packet = NULL;
    AVFrame *decoded = NULL;
    struct SwsContext *scaler = NULL;
    uint8_t *rgba = NULL;
    int exit_code = 1;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <h264-mp4-path>\n", argv[0]);
        return 2;
    }

    int result = avformat_open_input(&format, argv[1], NULL, NULL);
    if (result < 0) {
        print_ffmpeg_error("avformat_open_input", result);
        goto cleanup;
    }

    result = avformat_find_stream_info(format, NULL);
    if (result < 0) {
        print_ffmpeg_error("avformat_find_stream_info", result);
        goto cleanup;
    }

    AVCodec *decoder = NULL;
    const int video_stream = av_find_best_stream(
        format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (video_stream < 0 || decoder == NULL) {
        print_ffmpeg_error("av_find_best_stream", video_stream);
        goto cleanup;
    }

    codec = avcodec_alloc_context3(decoder);
    packet = av_packet_alloc();
    decoded = av_frame_alloc();
    if (codec == NULL || packet == NULL || decoded == NULL) {
        fprintf(stderr, "FFmpeg allocation failed\n");
        goto cleanup;
    }

    result = avcodec_parameters_to_context(
        codec, format->streams[video_stream]->codecpar);
    if (result < 0) {
        print_ffmpeg_error("avcodec_parameters_to_context", result);
        goto cleanup;
    }

    result = avcodec_open2(codec, decoder, NULL);
    if (result < 0) {
        print_ffmpeg_error("avcodec_open2", result);
        goto cleanup;
    }

    while ((result = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == video_stream) {
            result = avcodec_send_packet(codec, packet);
            if (result >= 0) {
                result = avcodec_receive_frame(codec, decoded);
                if (result == 0) {
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }

    if (result != 0 || decoded->width <= 0 || decoded->height <= 0) {
        print_ffmpeg_error("decode first frame", result);
        goto cleanup;
    }

    const int rgba_size = av_image_get_buffer_size(
        AV_PIX_FMT_RGBA, decoded->width, decoded->height, 1);
    if (rgba_size <= 0) {
        print_ffmpeg_error("av_image_get_buffer_size", rgba_size);
        goto cleanup;
    }

    rgba = (uint8_t *)malloc((size_t)rgba_size);
    if (rgba == NULL) {
        fprintf(stderr, "RGBA allocation failed\n");
        goto cleanup;
    }

    uint8_t *destination_data[4] = {rgba, NULL, NULL, NULL};
    int destination_linesize[4] = {decoded->width * 4, 0, 0, 0};
    scaler = sws_getContext(
        decoded->width,
        decoded->height,
        (enum AVPixelFormat)decoded->format,
        decoded->width,
        decoded->height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL);
    if (scaler == NULL) {
        fprintf(stderr, "sws_getContext failed\n");
        goto cleanup;
    }

    result = sws_scale(
        scaler,
        (const uint8_t *const *)decoded->data,
        decoded->linesize,
        0,
        decoded->height,
        destination_data,
        destination_linesize);
    if (result != decoded->height) {
        fprintf(stderr, "sws_scale converted %d of %d rows\n", result, decoded->height);
        goto cleanup;
    }

    printf(
        "PASS: decoded and scaled %dx%d H.264 frame with %s / %s\n",
        decoded->width,
        decoded->height,
        av_version_info(),
        avcodec_license());
    exit_code = 0;

cleanup:
    free(rgba);
    sws_freeContext(scaler);
    av_frame_free(&decoded);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    return exit_code;
}
