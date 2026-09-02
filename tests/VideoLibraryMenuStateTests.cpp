// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <cmath>
#include <iostream>
#include <string_view>

#include "BigScreen/DownloadRequestPolicy.hpp"
#include "BigScreen/VideoLibraryMenuState.hpp"

namespace {
    int failures = 0;

    void Expect(bool condition, std::string_view description)
    {
        if(condition)
            return;
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }

    bool Near(double actual, double expected)
    {
        return std::abs(actual - expected) < 0.000001;
    }
}

int main()
{
    using namespace BigScreen;

    PreviewTransport transport;
    Expect(transport.Is(PreviewTransportState::Stopped),
           "preview transport starts stopped");
    transport.Set(PreviewTransportState::WaitingForAudio);
    Expect(transport.IsWaiting() && transport.WantsPlayback(),
           "waiting for audio is one active playback intent");
    transport.Set(PreviewTransportState::WaitingForVideo);
    Expect(transport.Is(PreviewTransportState::WaitingForVideo) &&
               !transport.Is(PreviewTransportState::WaitingForAudio),
           "switching wait stages cannot leave overlapping boolean states");
    transport.Set(PreviewTransportState::Playing);
    Expect(!transport.IsWaiting() && transport.WantsPlayback(),
           "playing replaces every waiting state");
    transport.Set(PreviewTransportState::Paused);
    Expect(!transport.WantsPlayback(),
           "an explicit pause does not request asynchronous resurrection");

    transport.ArmPreRoll(12.5);
    transport.ArmPreRoll(20.0);
    Expect(!transport.PreRollComplete(12.49),
           "arming an existing pre-roll does not move its deadline");
    Expect(transport.PreRollComplete(12.5),
           "pre-roll completes at its original deadline");
    transport.ResetClock(4.25, 30.0);
    Expect(transport.CurrentClock().has_value() &&
               Near(transport.CurrentClock()->songTime, 4.25),
           "preview clock belongs to the transport state");
    transport.Stop();
    Expect(transport.Is(PreviewTransportState::Stopped) &&
               transport.PreRollComplete(0.0) &&
               !transport.CurrentClock().has_value(),
           "stopping clears transport, pre-roll, and clock together");

    TerminalDownloadProgressOwner terminalProgress;
    terminalProgress.RetainFor("map-a");
    Expect(terminalProgress.IsRetainedFor("map-a") &&
               !terminalProgress.IsRetainedFor("map-b"),
           "terminal progress is owned by exactly one map");
    terminalProgress.Reset();
    Expect(terminalProgress.Empty() &&
               !terminalProgress.IsRetainedFor("map-a"),
           "reset releases terminal progress ownership");

    MapVideoConfig timing;
    timing.offsetSeconds = -1.25;
    timing.playbackRate = 1.08;
    timing.fitToSong = true;
    timing.blackDuringLeadIn = true;
    const auto request = MakeVideoDownloadRequest(
        "level-id",
        "Song",
        "Artist",
        "https://youtu.be/example",
        VideoOrigin::User,
        true,
        &timing,
        1440,
        60);
    Expect(request.levelId == "level-id" && request.songName == "Song" &&
               request.songAuthor == "Artist" &&
               request.sourceUrl == "https://youtu.be/example",
           "shared request policy retains the selected media identity");
    Expect(request.origin == VideoOrigin::User &&
               request.explicitContentAllowed &&
               request.requestedHeight == 1440 &&
               request.maximumSourceFps == 60,
           "shared request policy retains source and download policy");
    Expect(Near(request.offsetSeconds, -1.25) &&
               Near(request.playbackRate, 1.08) && request.fitToSong &&
               request.blackDuringLeadIn,
           "shared request policy copies the active timing configuration");

    const auto neutralRequest = MakeVideoDownloadRequest(
        "other-level",
        "Other Song",
        "",
        "https://youtu.be/other",
        VideoOrigin::Mapper,
        false,
        nullptr,
        720,
        30);
    Expect(Near(neutralRequest.offsetSeconds, 0.0) &&
               Near(neutralRequest.playbackRate, 1.0) &&
               !neutralRequest.fitToSong &&
               !neutralRequest.blackDuringLeadIn,
           "missing mapper timing preserves DownloadRequest defaults");

    if(failures != 0)
        return 1;
    std::cout << "Video Library state tests passed\n";
    return 0;
}
