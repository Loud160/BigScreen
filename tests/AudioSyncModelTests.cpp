// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncModel.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
int failures = 0;
void Expect(bool condition, std::string_view description)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}
bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }
} // namespace

int main()
{
    using namespace BigScreen::AudioSync;
    const auto faster = FitMarkers({0, 100, 3, 108});
    Expect(faster && Near(faster->playbackRate, 1.05) && Near(faster->offsetSeconds, 3),
           "faster fit");
    const auto slower = FitMarkers({5, 105, 0, 98});
    Expect(slower && Near(slower->playbackRate, .98) && Near(slower->offsetSeconds, -4.9),
           "lead-in preserved");
    Expect(!FitMarkers({0, 0, 0, 10}), "reject zero span");
    Expect(!FitMarkers({5, 1, 0, 10}), "reject reversed markers");
    Expect(!FitMarkers({0, 1, 0, 100}), "rate bounds");
    Expect(!FitMarkers({0, 1, 0, std::numeric_limits<double>::infinity()}), "reject infinity");
    Expect(!AutoPitch(0), "invalid pitch rate");
    Expect(Near(*AutoPitch(2), -12), "resample auto compensation");
    Expect(Near(Step(Precision::TenThousandths), .0001), "submillisecond nudge");
    Expect(DecimalPlaces(Precision::Tenths) == 1 &&
               DecimalPlaces(Precision::Hundredths) == 2 &&
               DecimalPlaces(Precision::Thousandths) == 3 &&
               DecimalPlaces(Precision::TenThousandths) == 4,
           "time precision maps to visible decimal places");
    const SampleTimeline timeline{1.024, -1.0, 48000};
    Expect(Near(*timeline.TimeAt(48000), 1.024), "PTS plus repair rebase exactly once");
    Expect(!SampleTimeline{}.TimeAt(0), "invalid sample rate");
    Expect(!timeline.TimeAt(-1), "invalid sample index");

    const Bounds bounds{200, 210};
    auto initial = InitialProfile({-3.25, .98}, bounds);
    Expect(Near(initial.timing.offsetSeconds, -3.25) && Near(initial.timing.playbackRate, .98),
           "reset mapper timing");
    Expect(initial.method == TimingMethod::ManualSpeed && !initial.stopAtEndMarker,
           "no implicit marker fit/cutoff");
    Expect(Validate(initial, bounds).empty(), "valid initial profile");
    Record record{"map-a/video-v1", true, initial};
    record.profile.waveformLayout = 2;
    record.profile.waveformZoom = 14;
    record.profile.waveformHeightScale = 2;
    record.profile.lastAnalysis = AnalysisSummary{
        Confidence::High, {-2.25, 1.0125}, 12, 9, 1, 2, .003, .009, true, false};
    std::string error;
    const auto roundTrip = ParseRecord(SerializeRecord(record), error);
    Expect(roundTrip && *roundTrip == record && error.empty(),
           "version 2 preserves editor view and analysis summary");
    Expect(!ParseRecord("{}", error), "old records remain basic");
    auto future = SerializeRecord(record);
    future.replace(future.find("\"version\":2"), 11, "\"version\":3");
    Expect(!ParseRecord(future, error), "unsupported schema not activated");
    Record legacy{"map-a/video-v1", true, initial};
    auto versionOne = SerializeRecord(legacy);
    versionOne.replace(versionOne.find("\"version\":2"), 11, "\"version\":1");
    const auto presentation = versionOne.find(",\"waveformLayout\"");
    versionOne.erase(presentation, versionOne.size() - presentation - 1);
    const auto migrated = ParseRecord(versionOne, error);
    Expect(migrated && *migrated == legacy && error.empty(),
           "version 1 records migrate with default editor presentation");
    Expect(!ParseRecord("{\"version\":1,\"enabled\":true}", error),
           "incomplete record not activated");
    record.enabled = false;
    Expect(ParseRecord(SerializeRecord(record), error) == record,
           "disabled advanced settings retained");
    auto malformed = initial;
    malformed.finePitchCents = 201;
    Expect(!Validate(malformed, bounds).empty(), "pitch bound enforced");
    malformed = initial;
    malformed.markers.songEnd = 201;
    Expect(!Validate(malformed, bounds).empty(), "source duration enforced");
    malformed = initial;
    malformed.waveformZoom = 3;
    Expect(!Validate(malformed, bounds).empty(), "invalid waveform zoom rejected");
    malformed = initial;
    malformed.method = TimingMethod::FitMarkers;
    Expect(!Validate(malformed, bounds).empty(), "marker fit must match derived timing");
    malformed.timing = *FitMarkers(malformed.markers);
    Expect(Validate(malformed, bounds).empty(), "consistent marker fit accepted");

    Draft draft;
    draft.Open("map-a", record, initial);
    auto changed = initial;
    changed.timing.offsetSeconds = 1.2345;
    Expect(draft.Edit(changed, bounds, error) && draft.Dirty(), "edit changes draft only");
    Expect(draft.Saved() == record, "save failure leaves baseline intact");
    Expect(!draft.AcceptSaved(record), "old completion cannot acknowledge newer edit");
    const auto candidate = draft.Candidate();
    Expect(draft.AcceptSaved(candidate) && !draft.Dirty(),
           "successful durable save advances baseline");
    draft.Reset();
    Expect(draft.Dirty() && draft.Candidate().profile == initial, "reset is a draft operation");
    draft.Revert();
    Expect(!draft.Dirty() && draft.Candidate() == candidate,
           "discard reset restores saved profile");
    draft.Close();
    Expect(!draft.IsOpen() && draft.LevelId().empty() && draft.Candidate().sourceKey.empty(),
           "close clears all per-map state");
    Record second{"map-b/video-v2", false, InitialProfile({}, bounds)};
    draft.Open("map-b", second, second.profile);
    Expect(draft.Candidate() == second && !draft.Dirty(), "another map has no stale draft");
    AnalysisSummary proposalSummary{
        Confidence::Medium, {-4.9, .98}, 8, 6, 1, 1, .005, .012, true, false};
    Expect(draft.ProposeTiming({-4.9, .98}, bounds, error, proposalSummary),
           "automatic proposal enters shared draft");
    const auto proposed = draft.Candidate().profile;
    Expect(proposed.lastAnalysis == proposalSummary,
           "automatic proposal and its result summary enter one draft");
    draft.SetMode(Mode::Manual);
    Expect(draft.Candidate().profile.timing == proposed.timing &&
               draft.Candidate().profile.markers == proposed.markers,
           "auto-to-manual preserves all calculated timing");
    auto fineTuned = draft.Candidate().profile;
    fineTuned.timing.offsetSeconds += .001;
    Expect(draft.Edit(fineTuned, bounds, error), "manual can fine tune automatic result");
    draft.SetMode(Mode::Automatic);
    Expect(draft.Candidate().profile.timing == fineTuned.timing && draft.Saved() == second,
           "switching modes neither loses work nor auto-saves; next analysis is independent");
    const auto beforeInvalid = draft.Candidate();
    Expect(!draft.ProposeTiming({-86400, .05}, bounds, error) && draft.Candidate() == beforeInvalid,
           "nonoverlapping proposal cannot dereference invalid time or overwrite draft");
    Expect(!draft.ProposeTiming({}, {std::numeric_limits<double>::quiet_NaN(), 210}, error),
           "invalid source bounds rejected before marker calculation");
    Expect(EffectiveCutoff(record, 190) == 190, "basic mapper cutoff");
    record.enabled = true;
    Expect(EffectiveCutoff(record, 190) == 190, "fitting-only preserves mapper cutoff");
    record.profile.stopAtEndMarker = true;
    Expect(EffectiveCutoff(record, 190) == 210, "advanced stop overrides mapper cutoff");
    record.enabled = false;
    Expect(EffectiveCutoff(record, 190) == 190, "disabling advanced restores cutoff");
    return failures ? 1 : 0;
}
