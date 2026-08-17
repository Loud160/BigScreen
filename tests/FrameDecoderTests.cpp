// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
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
