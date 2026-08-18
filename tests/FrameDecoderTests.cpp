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
        // final timestamp, then seek backward as the menu audio loops. A true
        // EOF latch must clear and produce an opening frame every time.
        for(int loop = 0; loop < 3; ++loop)
        {
            decoder.Request(decoder.DurationSeconds() + 0.25);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            decoder.Request(0.1);
            BigScreen::VideoFrame loopedFrame;
            Expect(WaitForFrame(decoder, loopedFrame),
                   "a request after EOF should restart decoding for a preview loop");
            if(!loopedFrame.rgba.empty())
            {
                Expect(loopedFrame.presentationSeconds < 0.5,
                       "a preview loop should return to an opening video frame");
                decoder.Recycle(std::move(loopedFrame));
            }
        }
        Expect(decoder.BufferAllocations() <= 3,
               "reusable RGBA pool should stop repeated capacity growth");
        decoder.Close();
        Expect(!decoder.IsOpen(), "Close should stop the worker");
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
    if(failures == 0)
        std::cout << "FrameDecoder worker and reusable-buffer tests passed.\n";
    return failures == 0 ? 0 : 1;
}
