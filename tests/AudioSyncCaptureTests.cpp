// SPDX-License-Identifier: GPL-3.0-only
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
#include "BigScreen/AudioSyncOperation.hpp"
#include "BigScreen/AudioSyncPcm.hpp"
#include <cmath>
#include <iostream>
#include <thread>
using namespace BigScreen::AudioSync;
int main(int argc, char** argv)
{
    if(argc != 2)
        return 2;
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    const auto abandoned=root/"deadbeef.wav.part";
    {std::ofstream part(abandoned,std::ios::binary);part<<"interrupted capture fixture";}
    for(const int rate : {44100, 48000})
    {
        auto input = std::make_shared<CaptureBuffer>();
        input->Configure(rate, 2.123);
        Operation<std::shared_ptr<const PcmLease>> operation;
        operation.Start(1, "capture",
                        [=](const auto& context) -> std::optional<std::shared_ptr<const PcmLease>>
                        {
                            std::string error;
                            auto lease = PcmCache::Capture(
                                input, "host-capture-" + std::to_string(rate), root,
                                [&] { return context.Cancelled(); }, {}, error);
                            if(!lease)
                            {
                                context.Fail(error);
                                return std::nullopt;
                            }
                            return lease;
                        });
        std::array<float, 1024> chunk{};
        std::uint64_t sent = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while(operation.Busy() && std::chrono::steady_clock::now() < deadline)
        {
            if(input->accepting.load() && sent < input->inputMaximum &&
               input->write.load() - input->read.load() < CaptureBuffer::Capacity - 2048)
            {
                const auto size = std::min<std::uint64_t>(chunk.size(), input->inputMaximum - sent);
                for(std::size_t i = 0; i < size; ++i)
                    chunk[i] = .5 * std::sin((sent + i) * 440 * 2 * 3.141592653589793 / rate);
                input->Push({chunk.data(), size}, 1);
                sent += size;
                if(sent == input->inputMaximum)
                    input->Finish();
            }
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(operation.Busy())
        {
            operation.Cancel();
            std::cerr << "Capture timeout";
            return 1;
        }
        auto result = operation.TakeResult(1, "capture");
        if(!result)
        {
            std::cerr << operation.Poll()->error;
            return 1;
        }
        if(std::abs((*result)->duration - 2.123) > 1.0 / 16000 ||
           PcmCache::RemoveUnused((*result)->path))
            return 1;
        const auto info = ProbeAudio((*result)->path, {});
        if(std::filesystem::exists(abandoned))return 1;
        if(!info.available || info.channels != 1 || info.sampleRate != 16000)
            return 1;
        ReadRequest request;
        request.path = (*result)->path;
        request.startSeconds = .1;
        request.endSeconds = 2;
        double squared = 0;
        std::size_t count = 0;
        const auto decoded = ReadAudio(request,
                                       [&](auto samples, double)
                                       {
                                           for(auto value : samples)
                                           {
                                               squared += value * value;
                                               ++count;
                                           }
                                           return true;
                                       });
        if(!decoded.error.empty() || count == 0 ||
           std::abs(std::sqrt(squared / count) - .35355) > .005)
            return 1;
    }
    // Overflow cannot silently produce a shortened or discontinuous timeline.
    CaptureBuffer overflow;
    overflow.Configure(48000, 10);
    overflow.accepting = true;
    std::array<float, 1024> data{};
    for(int n = 0; n < 40; ++n)
        overflow.Push(data, 1);
    if(!overflow.overflow.load() || !overflow.stop.load())
        return 1;
    std::cout
        << "44.1/48 kHz bounded capture, sample duration, RMS, cache leases and overflow passed\n";
}
