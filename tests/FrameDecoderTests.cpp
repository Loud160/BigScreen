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
