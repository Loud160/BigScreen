// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <iostream>
#include <string_view>

#include "BigScreen/VideoEditorNoticeModel.hpp"

namespace {
    int failures = 0;

    void Expect(bool condition, std::string_view description)
    {
        if(condition)
            return;
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

int main()
{
    using BigScreen::VideoEditorNoticeModel;

    VideoEditorNoticeModel model;

    const auto firstMapVisit = model.Enter("map-a");
    Expect(static_cast<bool>(firstMapVisit), "an editor visit receives a token");
    Expect(model.Current(firstMapVisit, "map-a").empty(),
           "a newly opened map starts with a blank notice");

    const auto firstTransfer = model.BeginTransfer(firstMapVisit, "map-a");
    Expect(firstTransfer.has_value(),
           "the active map visit can begin one tracked transfer");
    Expect(!model.BeginTransfer(firstMapVisit, "map-a").has_value(),
           "a visit cannot track two transfers at once");
    Expect(model.PublishTransfer(
               *firstTransfer, "Downloading 4 MB of 8 MB (50%)").has_value(),
           "the exact transfer can publish live progress");
    Expect(model.Current(firstMapVisit, "map-a") ==
               "Downloading 4 MB of 8 MB (50%)",
           "live progress becomes the current notice");

    const auto userDuringTransfer = model.Publish(
        firstMapVisit, "map-a", "Playback speed saved: 1.10x.");
    Expect(userDuringTransfer.has_value(),
           "a user action can supersede live transfer text");
    Expect(!model.FinishTransfer(
                *firstTransfer, "Video download complete.").has_value(),
           "a delayed terminal event cannot overwrite a newer user action");
    Expect(model.Current(firstMapVisit, "map-a") ==
               "Playback speed saved: 1.10x.",
           "the user action remains after the delayed terminal event");

    const auto replacementTransfer =
        model.BeginTransfer(firstMapVisit, "map-a");
    Expect(replacementTransfer.has_value(),
           "the visit can track a new transfer after supersession");
    Expect(model.PublishTransfer(
               *replacementTransfer, "Downloading 8 MB of 8 MB (100%)")
               .has_value(),
           "the replacement transfer can publish progress");

    const auto terminalRevision = model.FinishTransfer(
        *replacementTransfer, "Video download complete.");
    Expect(terminalRevision.has_value(),
           "the exact transfer can publish one terminal result");
    Expect(model.Current(firstMapVisit, "map-a") ==
               "Video download complete.",
           "the terminal result remains visible for the current visit");

    const auto userRevision = model.Publish(
        firstMapVisit, "map-a", "Playback speed saved: 1.25x.");
    Expect(userRevision.has_value(),
           "a later user action can replace the terminal result");
    Expect(!model.FinishTransfer(
                *replacementTransfer, "Video download complete.").has_value(),
           "a retained terminal event cannot publish after retirement");
    Expect(model.Current(firstMapVisit, "map-a") ==
               "Playback speed saved: 1.25x.",
           "a retained terminal event cannot overwrite a later user action");

    const auto secondMapVisit = model.Enter("map-b");
    Expect(secondMapVisit != firstMapVisit,
           "each editor opening receives a unique visit token");
    Expect(model.Current(secondMapVisit, "map-b").empty(),
           "switching maps starts with a blank notice");
    Expect(model.Current(firstMapVisit, "map-a").empty(),
           "the preceding visit can no longer read a notice");
    Expect(!model.Publish(
                firstMapVisit, "map-a", "Delayed map-a action").has_value(),
           "a delayed event from the preceding visit is ignored");
    Expect(!model.FinishTransfer(
                *firstTransfer, "Delayed map-a terminal").has_value(),
           "a terminal event from the preceding map is ignored");
    Expect(model.Current(secondMapVisit, "map-b").empty(),
           "foreign events leave the current map blank");

    const auto previewRevision = model.Publish(
        secondMapVisit, "map-b", "Preparing synchronized preview…");
    Expect(previewRevision.has_value(),
           "preview preparation receives a concrete revision");
    const auto laterAction = model.Publish(
        secondMapVisit, "map-b", "Video offset saved: -2.00 seconds.");
    Expect(laterAction.has_value(),
           "a user action can follow preview preparation");
    Expect(!model.ClearIfCurrent(*previewRevision),
           "preview completion cannot clear a newer user action");
    Expect(model.Current(secondMapVisit, "map-b") ==
               "Video offset saved: -2.00 seconds.",
           "the newer user action survives the stale preview clear");

    const auto nextPreviewRevision = model.Publish(
        secondMapVisit, "map-b", "Loading song audio…");
    Expect(nextPreviewRevision.has_value(),
           "a later preview operation receives a new revision");
    Expect(model.ClearIfCurrent(*nextPreviewRevision),
           "preview completion clears its own unchanged notice");
    Expect(model.Current(secondMapVisit, "map-b").empty(),
           "clearing the current preview revision leaves a blank notice");

    const auto secondTransfer = model.BeginTransfer(secondMapVisit, "map-b");
    Expect(secondTransfer.has_value(),
           "the second map can begin its own transfer");
    auto forgedTransfer = *secondTransfer;
    ++forgedTransfer.value;
    Expect(!model.PublishTransfer(
                forgedTransfer, "Foreign transfer progress").has_value(),
           "a foreign transfer token cannot publish progress");
    Expect(!model.FinishTransfer(
                forgedTransfer, "Foreign transfer terminal").has_value(),
           "a foreign transfer token cannot publish a terminal result");
    Expect(model.Current(secondMapVisit, "map-b").empty(),
           "foreign transfer events do not alter the current notice");
    Expect(model.ForgetTransfer(*secondTransfer),
           "an exact transfer can be forgotten without publishing text");
    Expect(!model.PublishTransfer(
                *secondTransfer, "Late progress").has_value(),
           "a forgotten transfer cannot publish later progress");

    const auto abandonedTransfer =
        model.BeginTransfer(secondMapVisit, "map-b");
    Expect(abandonedTransfer.has_value(),
           "the map can begin a transfer that later loses its mailbox");
    Expect(model.PublishTransfer(
               *abandonedTransfer, "Checking available resolutions...")
               .has_value(),
           "the soon-abandoned transfer can publish text");
    Expect(model.AbandonTransfer(*abandonedTransfer),
           "abandoning a transfer clears text that transfer still owns");
    Expect(model.Current(secondMapVisit, "map-b").empty(),
           "an abandoned transfer cannot leave stale progress text");

    const auto transferAtExit =
        model.BeginTransfer(secondMapVisit, "map-b");
    Expect(transferAtExit.has_value(),
           "the active map can start a transfer before it is closed");
    Expect(model.PublishTransfer(
               *transferAtExit, "Downloading immediately before exit...")
               .has_value(),
           "the transfer can own visible text immediately before exit");
    Expect(model.Leave(secondMapVisit),
           "leaving a map succeeds while a transfer owns the notice");
    Expect(model.Current(secondMapVisit, "map-b").empty(),
           "leaving a map clears transfer-owned progress text");
    Expect(!model.PublishTransfer(
                *transferAtExit, "Delayed progress after exit").has_value(),
           "leaving a map rejects delayed transfer progress");
    Expect(!model.FinishTransfer(
                *transferAtExit, "Delayed completion after exit").has_value(),
           "leaving a map rejects delayed transfer completion");

    const auto reopenedMapVisit = model.Enter("map-b");
    Expect(reopenedMapVisit != secondMapVisit,
           "reopening the same map creates a fresh visit");
    Expect(model.Current(reopenedMapVisit, "map-b").empty(),
           "reopening the same map does not revive old operation text");
    Expect(!model.Clear(secondMapVisit, "map-b"),
           "a stale visit cannot clear the reopened map's notice");

    const auto newerMapVisit = model.Enter("map-c");
    Expect(model.Publish(
               newerMapVisit, "map-c", "Current map-c operation.").has_value(),
           "the newest map can publish its own operation");
    Expect(!model.Leave(reopenedMapVisit),
           "a delayed close from an older visit is rejected");
    Expect(model.Current(newerMapVisit, "map-c") ==
               "Current map-c operation.",
           "a delayed close cannot erase the current map's notice");
    model.Reset();
    Expect(model.Current(newerMapVisit, "map-c").empty(),
           "an unconditional map-exit reset clears the retained string");
    Expect(!model.Publish(
                newerMapVisit, "map-c", "Delayed action after reset")
                .has_value(),
           "an unconditional map-exit reset invalidates the visit token");

    if(failures != 0)
    {
        std::cerr << failures << " VideoEditorNoticeModel test(s) failed\n";
        return 1;
    }

    std::cout << "VideoEditorNoticeModel tests passed\n";
    return 0;
}
