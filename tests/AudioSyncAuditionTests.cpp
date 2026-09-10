// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncAudition.hpp"
#include <cmath>
#include <iostream>
#include <thread>
using namespace BigScreen::AudioSync;
int main(int argc, char** argv)
{
    if(argc != 2)
        return 2;
    const std::filesystem::path root(argv[1]);
    const Source source{root / "matching-map.wav", "same", 0};
    Preparation prep;
    prep.Start(1, "test",
               [=](const auto& c) { return Prepare(source, source, c, root / "audition-cache"); });
    for(int i = 0; prep.Busy() && i < 2000; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto pair = prep.TakeResult(1, "test");
    if(!pair)
    {
        std::cerr << prep.Poll()->error;
        return 1;
    }
    for(int scenario = 0; scenario < 4; ++scenario)
    {
        const bool pitch = scenario != 0;
        Audition engine;
        AuditionRequest request;
        request.sources = pair;
        request.profile = InitialProfile({0, 1}, {40, 40});
        request.profile.pitchCorrection = pitch;
        if(scenario >= 2)
        {
            request.profile.timing.playbackRate = scenario == 2 ? .5 : 2;
            request.profile.autoPitchSemitones = scenario == 2 ? 12 : -12;
            request.auditionSpeed = scenario == 2 ? .5 : .75;
        }
        request.songEnd = 2;
        std::string error;
        if(!engine.Start(request, error))
        {
            std::cerr << error;
            return 1;
        }
        auto state = engine.Output();
        state->paused.store(false);
        std::array<float, 2048> audio{};
        std::size_t frames = 0;
        for(int spin = 0; spin < 5000 && (!state->complete.load() || state->Buffered()); ++spin)
        {
            if(state->Buffered() < 1024 && !state->complete.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const auto before = state->read.load();
            state->Render(audio.data(), 1024, 2);
            frames += state->read.load() - before;
            for(std::size_t i = 0; i < audio.size(); i += 2)
                if(!std::isfinite(audio[i]) || !std::isfinite(audio[i + 1]) ||
                   (scenario < 2 && std::abs(audio[i] - audio[i + 1]) > .00004f))
                {
                    std::cerr << "Channel mismatch, pitch=" << pitch << " left=" << audio[i]
                              << " right=" << audio[i + 1] << '\n';
                    return 1;
                }
        }
        if(frames != static_cast<std::size_t>(std::ceil(96000 / request.auditionSpeed)) ||
           std::abs(state->SongTime() - 2) > .00002)
        {
            std::cerr << "Unexpected frames=" << frames << " clock=" << state->SongTime() << '\n';
            return 1;
        }
        state->Render(audio.data(), 1024, 2);
        if(std::abs(state->SongTime() - 2) > .00002)
            return 1; // underrun does not move source clock
        engine.Stop();
    }
    std::cout << "Common-clock routing, unity/octave pitch duration, shared slowdown, PCM leases "
                 "and EOS passed\n";
}
