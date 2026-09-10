// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncReader.hpp"
#include <cmath>
#include <iostream>
#include <limits>

int main(int argc, char** argv)
{
    if(argc != 2)
        return 2;
    int failures = 0;
    const std::filesystem::path root(argv[1]);
    auto expect = [&](bool condition, const char* message)
    {
        if(!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    };
    using namespace BigScreen::AudioSync;
    for(const auto* name :
        {"tone.wav", "tone.m4a", "tone.ogg", "transient.ogg", "tone.webm"})
    {
        ReadRequest request;
        request.path = root / name;
        request.endSeconds = 3;
        double first = -1, next = 0;
        const auto result =
            ReadAudio(request,
                      [&](std::span<const float> data, double time)
                      {
                          if(first < 0)
                              first = time;
                          else
                              expect(std::abs(next - time) < .005, "continuous resampled clock");
                          next = time + data.size() / 16000.0;
                          return true;
                      });
        if(!result.error.empty())
            std::cerr << name << ": " << result.error << '\n';
        expect(result.error.empty() && result.outputSamples > 47000,
               "bounded AAC/Opus/Vorbis/PCM decode");
        expect(first >= 0 && first < .002,
               "codec priming/pre-skip does not create false timeline offset");
        request.startSeconds = 1;
        request.endSeconds = 1.5;
        first = -1;
        const auto window = ReadAudio(request,
                                      [&](std::span<const float>, double time)
                                      {
                                          if(first < 0)
                                              first = time;
                                          return true;
                                      });
        expect(window.error.empty() && window.outputSamples >= 7900 && window.outputSamples <= 8001,
               "seek emits only requested half-second window");
        expect(first >= 1 && first < 1.002, "seek keeps authoritative origin");
    }
    ReadRequest shifted;
    shifted.path = root / "shifted.mka";
    shifted.endSeconds = 5;
    double first = -1;
    auto result = ReadAudio(shifted,
                            [&](std::span<const float>, double time)
                            {
                                if(first < 0)
                                    first = time;
                                return true;
                            });
    expect(result.error.empty() && std::abs(first - 1.25) < .002, "nonzero container PTS retained");
    shifted.sourceToFinalShiftSeconds = -1.25;
    first = -1;
    result = ReadAudio(shifted,
                       [&](std::span<const float>, double time)
                       {
                           if(first < 0)
                               first = time;
                           return true;
                       });
    expect(result.error.empty() && std::abs(first) < .002, "video repair rebase applied once");
    for(const auto* videoName : {"offset-video.mp4","rebased-video.mp4"})
    {
        std::string error;
        const auto origin=VideoStartTime(root/videoName,{},error);
        const double expected=std::string(videoName)=="offset-video.mp4"?1.25:0;
        expect(origin && std::abs(*origin-expected)<.002,"final video start preserved or rebased");
        ReadRequest companion;
        companion.path=root/"tone.m4a";
        companion.sourceToFinalShiftSeconds=origin.value_or(0);
        first=-1;
        const auto aligned=ReadAudio(companion,[&](auto,double time){if(first<0)first=time;return true;});
        expect(aligned.error.empty() && std::abs(first-expected)<.002,
               "companion AAC follows final video origin exactly once");
    }
    shifted.cancelled = [] { return true; };
    result = ReadAudio(shifted, [](std::span<const float>, double) { return true; });
    expect(result.cancelled && result.error.empty() && result.outputSamples == 0,
           "cancel is not decode failure");
    expect(!ProbeAudio(root / "missing.mp4", {}).available, "missing source recoverable");
    return failures ? 1 : 0;
}
