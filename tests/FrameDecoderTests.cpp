// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "BigScreen/FrameDecoder.hpp"

namespace {
    int failures = 0;

    void Expect(bool condition, const char* description)
    {
        if(condition)
            return;
        ++failures;
        std::cerr << "FAILED: " << description << '\n';
    }

    bool WaitForFrame(BigScreen::FrameDecoder& decoder, BigScreen::VideoFrame& frame)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while(std::chrono::steady_clock::now() < deadline)
        {
            if(decoder.TryTake(frame))
                return true;
            if(const auto error = decoder.TakeError())
            {
                std::cerr << "Decoder worker error: " << *error << '\n';
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    bool WaitForFrameAt(
        BigScreen::FrameDecoder& decoder,
        double mediaSeconds,
        BigScreen::VideoFrame& frame)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while(std::chrono::steady_clock::now() < deadline)
        {
            if(decoder.TryTake(mediaSeconds, frame))
                return true;
            if(const auto error = decoder.TakeError())
            {
                std::cerr << "Decoder worker error: " << *error << '\n';
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    void Exercise(const std::filesystem::path& path, int expectedWidth, int expectedHeight)
    {
        BigScreen::FrameDecoder decoder;
        std::string error;
        Expect(decoder.Open(path, 480, error), "fixture should open");
        if(!decoder.IsOpen())
        {
            std::cerr << error << '\n';
            return;
        }
        Expect(decoder.SourceWidth() == expectedWidth, "source width should be preserved");
        Expect(decoder.SourceHeight() == expectedHeight, "source height should be preserved");
        Expect(decoder.Width() == expectedWidth, "small videos should never be upscaled");
        Expect(decoder.Height() == expectedHeight, "small videos should never be upscaled");
        Expect(decoder.DurationSeconds() > 1.0, "container duration should be available");
        Expect(std::string(decoder.RuntimeVersion()).size() > 0,
               "the active decoder should report its FFmpeg runtime");

        for(int index = 0; index < 6; ++index)
        {
            decoder.Request(index * 0.2);
            BigScreen::VideoFrame frame;
            Expect(WaitForFrame(decoder, frame), "requested frame should arrive");
            if(frame.rgba.empty())
                continue;
            Expect(frame.width == expectedWidth, "decoded width should match output");
            Expect(frame.height == expectedHeight, "decoded height should match output");
            Expect(frame.rgba.size() == static_cast<std::size_t>(
                expectedWidth * expectedHeight * 4), "RGBA byte count should be exact");
            Expect(frame.durationSeconds > 0.0, "frame duration should use real timing or fallback");
            decoder.Recycle(std::move(frame));
        }

        // The Quest compatibility cycle changes Cinema color effects while a
        // single decoder remains open. Verify that the worker observes the
        // replacement atomically and that clearing the effect restores normal
        // picture output without reopening the media.
        BigScreen::FrameVisualEffects blackout;
        blackout.enabled = true;
        blackout.brightness = 0.0f;
        decoder.UpdateVisualEffects(blackout);
        decoder.Request(0.37);
        BigScreen::VideoFrame blackFrame;
        Expect(WaitForFrame(decoder, blackFrame),
               "a frame should arrive after changing live visual effects");
        if(!blackFrame.rgba.empty())
        {
            bool allRgbBlack = true;
            for(std::size_t byte = 0; byte + 3 < blackFrame.rgba.size(); byte += 4)
            {
                if(blackFrame.rgba[byte] != 0 ||
                   blackFrame.rgba[byte + 1] != 0 ||
                   blackFrame.rgba[byte + 2] != 0)
                {
                    allRgbBlack = false;
                    break;
                }
            }
            Expect(allRgbBlack,
                   "live zero-brightness replacement should black out RGB output");
            decoder.Recycle(std::move(blackFrame));
        }

        decoder.UpdateVisualEffects({});
        decoder.Request(0.57);
        BigScreen::VideoFrame restoredFrame;
        Expect(WaitForFrame(decoder, restoredFrame),
               "a frame should arrive after clearing live visual effects");
        if(!restoredFrame.rgba.empty())
        {
            const auto hasVisibleRgb = std::any_of(
                restoredFrame.rgba.begin(), restoredFrame.rgba.end(),
                [](std::uint8_t value) { return value != 0; });
            Expect(hasVisibleRgb,
                   "clearing live visual effects should restore visible picture data");
            decoder.Recycle(std::move(restoredFrame));
        }

        // Cinema's oval radius zero must create a real ellipse. The runtime
        // material consumes decoded alpha, so the vignette must clear alpha
        // without also forcing the hidden RGB texels to black. Keeping RGB
        // intact avoids the black rectangular fringe seen on Quest.
        BigScreen::FrameVisualEffects ovalVignette;
        ovalVignette.enabled = true;
        ovalVignette.vignetteEnabled = true;
        ovalVignette.vignetteElliptical = true;
        ovalVignette.vignetteRadius = 0.0f;
        ovalVignette.vignetteSoftness = 0.1f;
        decoder.UpdateVisualEffects(ovalVignette);
        decoder.Request(0.77);
        BigScreen::VideoFrame ovalFrame;
        Expect(WaitForFrame(decoder, ovalFrame),
               "an oval-vignette frame should arrive");
        if(!ovalFrame.rgba.empty())
        {
            const auto corner = static_cast<std::size_t>(0);
            const auto center = (static_cast<std::size_t>(ovalFrame.height / 2) *
                ovalFrame.width + ovalFrame.width / 2) * 4;
            Expect(ovalFrame.rgba[corner + 3] == 0,
                   "oval vignette should clear corner alpha");
            Expect(ovalFrame.rgba[center + 3] > 240,
                   "oval vignette should preserve the center");
            decoder.Recycle(std::move(ovalFrame));
        }

        // Rectangular mode uses radius as its inset boundary. A .72 radius
        // therefore removes the outer border while leaving the center intact.
        BigScreen::FrameVisualEffects rectangularVignette = ovalVignette;
        rectangularVignette.vignetteElliptical = false;
        rectangularVignette.vignetteRadius = 0.72f;
        rectangularVignette.vignetteSoftness = 0.16f;
        decoder.UpdateVisualEffects(rectangularVignette);
        decoder.Request(0.97);
        BigScreen::VideoFrame rectangularFrame;
        Expect(WaitForFrame(decoder, rectangularFrame),
               "a rectangular-vignette frame should arrive");
        if(!rectangularFrame.rgba.empty())
        {
            const auto corner = static_cast<std::size_t>(0);
            const auto center =
                (static_cast<std::size_t>(rectangularFrame.height / 2) *
                 rectangularFrame.width + rectangularFrame.width / 2) * 4;
            Expect(rectangularFrame.rgba[corner + 3] == 0,
                   "rectangular vignette should clear the outer-border alpha");
            Expect(rectangularFrame.rgba[center + 3] > 240,
                   "rectangular vignette should preserve the center");
            decoder.Recycle(std::move(rectangularFrame));
        }

        decoder.UpdateVisualEffects({});

        // Exercise the exact long-running preview transition: drain beyond the
        // final timestamp, then use the explicit restart contract as the menu
        // audio loops. A true EOF latch and any old mailbox picture must clear
        // before an opening frame from the new pass is published.
        std::uint64_t priorRestartGeneration = 0;
        for(int loop = 0; loop < 3; ++loop)
        {
            decoder.Request(decoder.DurationSeconds() + 0.25);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto restartGeneration = decoder.Restart(0.1);
            Expect(restartGeneration > priorRestartGeneration,
                   "each explicit restart should create a new frame generation");
            priorRestartGeneration = restartGeneration;
            BigScreen::VideoFrame loopedFrame;
            Expect(WaitForFrame(decoder, loopedFrame),
                   "an explicit restart after EOF should decode a new preview loop");
            if(!loopedFrame.rgba.empty())
            {
                Expect(loopedFrame.presentationSeconds < 0.5,
                       "a preview loop should return to an opening video frame");
                Expect(loopedFrame.generation == restartGeneration,
                       "a preview loop should publish only the restarted generation");
                decoder.Recycle(std::move(loopedFrame));
            }
        }
        Expect(decoder.BufferAllocations() <= 3,
               "reusable RGBA pool should stop repeated capacity growth");
        const auto preparation = decoder.PreparationDiagnostics();
        Expect(preparation.sampleCount > 0,
               "frame preparation diagnostics should count output pictures");
        Expect(preparation.cpuNanoseconds >= preparation.peakCpuNanoseconds,
               "session preparation CPU total should include its peak sample");
        Expect(preparation.waitNanoseconds >= preparation.peakWaitNanoseconds,
               "session worker-wait total should include its peak sample");
        decoder.ResetPreparationDiagnostics();
        Expect(decoder.PreparationDiagnostics().sampleCount == 0,
               "a new measured session should clear preparation totals once");
        decoder.Close();
        Expect(!decoder.IsOpen(), "Close should stop the worker");
    }

    void ExerciseGpuTransport(
        const std::filesystem::path& path,
        int expectedWidth,
        int expectedHeight,
        BigScreen::GpuYuvUploadLayout uploadLayout)
    {
        BigScreen::FrameDecoder decoder;
        std::string error;
        Expect(
            decoder.Open(
                path,
                BigScreen::UncappedOutputHeight,
                false,
                true,
                uploadLayout,
                64u * 1024u * 1024u,
                nullptr,
                {},
                error),
            "uncapped fixture should open with GPU plane transport requested");
        if(!decoder.IsOpen())
        {
            std::cerr << error << '\n';
            return;
        }

        // Supply the same media-time cadence PlaybackSession uses at a 60 FPS
        // ceiling. The GPU path should publish the current picture promptly,
        // then keep a bounded set of future source pictures ready without
        // making any of them visible before their selected clock slot.
        decoder.Request(0.2, 1.0 / 60.0);
        BigScreen::VideoFrame yuvFrame;
        const bool receivedYuv = WaitForFrame(decoder, yuvFrame);
        Expect(receivedYuv, "GPU transport should publish a decoded frame");
        if(receivedYuv)
        {
            const int chromaWidth = (expectedWidth + 1) / 2;
            const int chromaHeight = (expectedHeight + 1) / 2;
            Expect(yuvFrame.rgba.empty(),
                   "GPU transport should not allocate an RGBA picture");
            Expect(yuvFrame.sourceWidth == expectedWidth &&
                   yuvFrame.sourceHeight == expectedHeight,
                   "GPU plane dimensions should retain decoder orientation");
            if(uploadLayout == BigScreen::GpuYuvUploadLayout::PackedAtlas)
            {
                const int atlasWidth = std::max(
                    expectedWidth, chromaWidth * 2);
                Expect(yuvFrame.storage ==
                           BigScreen::VideoFrameStorage::Yuv420PackedAtlas,
                       "packed GPU transport should use one YUV atlas");
                Expect(yuvFrame.y.empty() && yuvFrame.u.empty() &&
                           yuvFrame.v.empty(),
                       "packed GPU transport should not retain plane buffers");
                Expect(yuvFrame.packedYuv.size() ==
                           static_cast<std::size_t>(atlasWidth) *
                               (expectedHeight + chromaHeight),
                       "packed YUV atlas byte count should be exact");
            }
            else
            {
                Expect(yuvFrame.storage ==
                           BigScreen::VideoFrameStorage::Yuv420Planar,
                       "GPU transport should use normalized planar YUV420");
                Expect(yuvFrame.packedYuv.empty(),
                       "3-plane GPU transport should not retain an atlas");
                Expect(yuvFrame.y.size() == static_cast<std::size_t>(
                           expectedWidth * expectedHeight),
                       "Y plane byte count should be exact");
                Expect(yuvFrame.u.size() == static_cast<std::size_t>(
                           chromaWidth * chromaHeight) &&
                       yuvFrame.v.size() == static_cast<std::size_t>(
                           chromaWidth * chromaHeight),
                       "U and V plane byte counts should be exact");
            }
            const double firstPresentationSeconds =
                yuvFrame.presentationSeconds;
            decoder.Recycle(std::move(yuvFrame));

            BigScreen::VideoFrame earlyFrame;
            Expect(!decoder.TryTake(0.2, earlyFrame),
                   "a prefetched GPU picture must not be returned early");

            // No new Request is posted here. Receiving a later picture proves
            // that the decoder filled future presentation slots rather than
            // merely replacing the old one-frame mailbox on demand.
            BigScreen::VideoFrame prefetchedFrame;
            const bool receivedPrefetched =
                WaitForFrameAt(decoder, 0.35, prefetchedFrame);
            Expect(receivedPrefetched,
                   "GPU transport should retain a future read-ahead picture");
            if(receivedPrefetched)
            {
                Expect(prefetchedFrame.presentationSeconds >
                           firstPresentationSeconds,
                       "read-ahead should advance to a later source picture");
                decoder.Recycle(std::move(prefetchedFrame));
            }

            const auto queueDiagnostics = decoder.ReadAheadDiagnostics();
            Expect(queueDiagnostics.byteBudget == 64u * 1024u * 1024u,
                   "GPU queue should use the supplied memory budget");
            Expect(queueDiagnostics.frameCapacity > 5,
                   "a small fixture should no longer be capped at five prefetched frames");
            Expect(queueDiagnostics.peakQueuedFrames > 0,
                   "GPU queue diagnostics should record its peak reserve");
            Expect(decoder.BufferAllocations() <=
                       queueDiagnostics.frameCapacity + 1,
                   "planar Y/U/V growth should count once per reusable frame set");

            // A 10 FPS fixture produces due slots 100 ms apart. Advancing
            // from .35 to .50 makes two queued source pictures due at once;
            // the bounded consumer should present the older one now and keep
            // the newer one for the next Unity update rather than dropping it.
            BigScreen::VideoFrame catchUpFrame;
            Expect(WaitForFrameAt(decoder, 0.50, catchUpFrame),
                   "GPU transport should present a bounded catch-up frame");
            decoder.Recycle(std::move(catchUpFrame));
            const auto catchUpDiagnostics = decoder.ReadAheadDiagnostics();
            Expect(catchUpDiagnostics.catchUpPresentations > 0,
                   "GPU queue diagnostics should count recovered late frames");

            // A much larger clock jump cannot be recovered in one 90 Hz
            // display update. Old pictures must still be discarded so the
            // bounded catch-up policy never creates sustained A/V latency.
            BigScreen::VideoFrame lateFrame;
            Expect(WaitForFrameAt(decoder, 1.20, lateFrame),
                   "GPU transport should recover from a large late backlog");
            decoder.Recycle(std::move(lateFrame));
            const auto lateDiagnostics = decoder.ReadAheadDiagnostics();
            Expect(lateDiagnostics.forcedLateDrops > 0,
                   "GPU queue diagnostics should count irrecoverably late drops");
            Expect(lateDiagnostics.peakDueFrameBacklog >= 2,
                   "GPU queue diagnostics should record due-frame backlog");

            const auto restartGeneration = decoder.Restart(0.1);
            BigScreen::VideoFrame restartedFrame;
            Expect(WaitForFrameAt(decoder, 0.1, restartedFrame),
                   "restart should refill after clearing the prior GPU queue");
            if(restartedFrame.storage != BigScreen::VideoFrameStorage::Rgba32)
            {
                Expect(restartedFrame.generation == restartGeneration,
                       "restart must never return a frame from the flushed queue");
                decoder.Recycle(std::move(restartedFrame));
            }

            if(uploadLayout == BigScreen::GpuYuvUploadLayout::PackedAtlas)
            {
                // ScreenSurface can reject the packed shader or atlas while
                // the decoder is already open. The live transition must flush
                // packed pictures and publish a genuine three-plane frame so
                // Unity never interprets an old atlas as separate Y/U/V data.
                decoder.SetGpuYuvUploadLayout(
                    BigScreen::GpuYuvUploadLayout::ThreePlane);
                decoder.Request(0.7, 1.0 / 60.0);
                BigScreen::VideoFrame planarFallbackFrame;
                const bool receivedPlanarFallback =
                    WaitForFrameAt(decoder, 0.7, planarFallbackFrame);
                Expect(receivedPlanarFallback,
                       "packed transport should switch live to three-plane output");
                if(receivedPlanarFallback)
                {
                    Expect(planarFallbackFrame.storage ==
                               BigScreen::VideoFrameStorage::Yuv420Planar,
                           "packed fallback must publish a three-plane frame");
                    Expect(planarFallbackFrame.packedYuv.empty(),
                           "packed fallback must not retain an atlas buffer");
                    decoder.Recycle(std::move(planarFallbackFrame));
                }
            }
        }

        // Unity can reject the conversion shader or RenderTexture after a
        // gameplay prewarm already queued YUV. Disabling the transport must
        // discard that mailbox and make the next output ordinary RGBA without
        // reopening FFmpeg.
        decoder.SetGpuConversionEnabled(false);
        decoder.Request(0.8);
        BigScreen::VideoFrame rgbaFrame;
        const bool receivedRgba = WaitForFrame(decoder, rgbaFrame);
        Expect(receivedRgba,
               "GPU resource fallback should publish a replacement frame");
        if(receivedRgba)
        {
            Expect(rgbaFrame.storage == BigScreen::VideoFrameStorage::Rgba32,
                   "disabled GPU transport should return to RGBA");
            Expect(rgbaFrame.rgba.size() == static_cast<std::size_t>(
                       expectedWidth * expectedHeight * 4),
                   "fallback RGBA byte count should be exact");
            decoder.Recycle(std::move(rgbaFrame));
        }
        decoder.RequestStop();
        Expect(decoder.ReadAheadDiagnostics().currentQueuedFrames == 0,
               "early exit should synchronously empty the decoded-frame queue");
        decoder.Close();
        Expect(decoder.ReadAheadDiagnostics().currentQueuedFrames == 0,
               "close should leave no decoded frames queued");
    }
}

int main(int argc, char** argv)
{
    if(argc != 3)
    {
        std::cerr << "Expected landscape and portrait fixture paths.\n";
        return 2;
    }
    Exercise(argv[1], 96, 54);
    Exercise(argv[2], 54, 96);
    ExerciseGpuTransport(
        argv[1], 96, 54, BigScreen::GpuYuvUploadLayout::ThreePlane);
    ExerciseGpuTransport(
        argv[1], 96, 54, BigScreen::GpuYuvUploadLayout::PackedAtlas);
    if(failures == 0)
        std::cout << "FrameDecoder worker and reusable-buffer tests passed.\n";
    return failures == 0 ? 0 : 1;
}
