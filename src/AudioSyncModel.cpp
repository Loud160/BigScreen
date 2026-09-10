// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncModel.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace BigScreen::AudioSync
{
namespace
{
constexpr double MinimumRate = 0.05;
constexpr double MaximumRate = 8.0;
// A corruption/resource guard, not a promise of practical 24-hour
// analysis. Worker budgets impose much smaller bounded working sets.
constexpr double MaximumTime = 86400.0;

bool ValidRate(double value)
{
    return std::isfinite(value) && value >= MinimumRate && value <= MaximumRate;
}

bool ValidTime(double value) { return std::isfinite(value) && std::abs(value) <= MaximumTime; }

bool ValidAnalysisSummary(const AnalysisSummary& summary)
{
    const bool requestedPoints = summary.requestedPoints == 5 || summary.requestedPoints == 8 ||
                                 summary.requestedPoints == 12 || summary.requestedPoints == 16;
    return static_cast<unsigned>(summary.confidence) <= 2 &&
           ValidRate(summary.timing.playbackRate) && ValidTime(summary.timing.offsetSeconds) &&
           requestedPoints && summary.acceptedPoints >= 0 && summary.acceptedPoints <= 64 &&
           summary.rejectedPoints >= 0 && summary.rejectedPoints <= 64 &&
           summary.validationPoints >= 0 && summary.validationPoints <= 64 &&
           std::isfinite(summary.fitRmsSeconds) && summary.fitRmsSeconds >= 0.0 &&
           summary.fitRmsSeconds <= MaximumTime &&
           std::isfinite(summary.validationMaxSeconds) &&
           summary.validationMaxSeconds >= 0.0 &&
           summary.validationMaxSeconds <= MaximumTime;
}

bool ValidProfile(const Profile& p)
{
    return ValidRate(p.timing.playbackRate) && ValidTime(p.timing.offsetSeconds) &&
           ValidTime(p.markers.songStart) && ValidTime(p.markers.songEnd) &&
           ValidTime(p.markers.videoStart) && ValidTime(p.markers.videoEnd) &&
           p.markers.songStart >= 0.0 && p.markers.videoStart >= 0.0 &&
           p.markers.songEnd > p.markers.songStart && p.markers.videoEnd > p.markers.videoStart &&
           static_cast<unsigned>(p.mode) <= 1 && static_cast<unsigned>(p.method) <= 1 &&
           static_cast<unsigned>(p.routing) <= 4 && static_cast<unsigned>(p.timePrecision) <= 3 &&
           static_cast<unsigned>(p.speedPrecision) <= 3 && std::isfinite(p.autoPitchSemitones) &&
           std::abs(p.autoPitchSemitones) <= 60.0 && std::isfinite(p.finePitchCents) &&
           std::abs(p.finePitchCents) <= 200.0 && p.anchorCount >= 5 && p.anchorCount <= 16 &&
           std::isfinite(p.windowSeconds) && p.windowSeconds >= 2.0 && p.windowSeconds <= 20.0 &&
           p.waveformLayout >= 0 && p.waveformLayout <= 2 &&
           (p.waveformZoom == 0 || (p.waveformZoom >= 2 && p.waveformZoom <= 20 &&
                                    p.waveformZoom % 2 == 0)) &&
           p.waveformHeightScale >= 0 && p.waveformHeightScale <= 3 &&
           (!p.lastAnalysis || ValidAnalysisSummary(*p.lastAnalysis));
}
} // namespace

double Step(Precision precision)
{
    switch(precision)
    {
    case Precision::Tenths:
        return 0.1;
    case Precision::Hundredths:
        return 0.01;
    case Precision::Thousandths:
        return 0.001;
    case Precision::TenThousandths:
        return 0.0001;
    }
    return 0.01;
}

int DecimalPlaces(Precision precision)
{
    // Keep display precision derived from the same enum that controls the
    // timing arrow step. This prevents a 0.1-second adjustment from looking
    // artificially precise while still allowing the 0.0001-second mode to
    // expose every digit the user can actually change.
    switch(precision)
    {
    case Precision::Tenths:
        return 1;
    case Precision::Hundredths:
        return 2;
    case Precision::Thousandths:
        return 3;
    case Precision::TenThousandths:
        return 4;
    }
    return 2;
}

std::optional<Timing> FitMarkers(const Markers& m)
{
    if(!ValidTime(m.songStart) || !ValidTime(m.songEnd) || !ValidTime(m.videoStart) ||
       !ValidTime(m.videoEnd) || m.songStart < 0.0 || m.videoStart < 0.0 ||
       m.songEnd <= m.songStart || m.videoEnd <= m.videoStart)
        return std::nullopt;
    // V = r*S + b. Example: song [5,105], video [0,98] gives
    // r=.98 and b=-4.9; the five-second song lead-in is NOT discarded.
    const double rate = (m.videoEnd - m.videoStart) / (m.songEnd - m.songStart);
    const double offset = m.videoStart - rate * m.songStart;
    if(!ValidRate(rate) || !ValidTime(offset))
        return std::nullopt;
    return Timing{offset, rate};
}

std::optional<double> AutoPitch(double playbackRate)
{
    if(!ValidRate(playbackRate))
        return std::nullopt;
    // Ordinary resampling shifts pitch by +12*log2(r). This compensation
    // applies to that path only, never after pitch-preserving stretching.
    return -12.0 * std::log2(playbackRate);
}

std::optional<double> VideoTime(const Timing& timing, double songSeconds)
{
    if(!ValidRate(timing.playbackRate) || !ValidTime(timing.offsetSeconds) ||
       !ValidTime(songSeconds))
        return std::nullopt;
    return std::fma(timing.playbackRate, songSeconds, timing.offsetSeconds);
}

std::optional<double> SampleTimeline::TimeAt(std::int64_t sampleIndex) const
{
    if(sampleRate <= 0 || sampleIndex < 0 || !ValidTime(firstSamplePtsSeconds) ||
       !ValidTime(sourceToFinalShiftSeconds))
        return std::nullopt;
    const double value = firstSamplePtsSeconds + sourceToFinalShiftSeconds +
                         static_cast<double>(sampleIndex) / sampleRate;
    return ValidTime(value) ? std::optional<double>{value} : std::nullopt;
}

std::string Validate(const Profile& p, const Bounds& bounds)
{
    if(!ValidProfile(p))
        return "Invalid audio synchronization settings or marker range.";
    if(!ValidTime(bounds.songDuration) || !ValidTime(bounds.videoDuration) ||
       bounds.songDuration <= 0.0 || bounds.videoDuration <= 0.0)
        return "Both source durations must be available before applying changes.";
    if(p.markers.songEnd > bounds.songDuration || p.markers.videoEnd > bounds.videoDuration)
        return "A marker is outside its source track.";
    if(p.method == TimingMethod::FitMarkers)
    {
        const auto fitted = FitMarkers(p.markers);
        if(!fitted || std::abs(fitted->playbackRate - p.timing.playbackRate) > 1e-9 ||
           std::abs(fitted->offsetSeconds - p.timing.offsetSeconds) > 1e-8)
            return "Recalculate timing from the current markers before applying.";
    }
    return {};
}

Profile InitialProfile(Timing mapperTiming, const Bounds& bounds)
{
    Profile p;
    if(ValidRate(mapperTiming.playbackRate) && ValidTime(mapperTiming.offsetSeconds))
        p.timing = mapperTiming;
    // Start in ManualSpeed so unequal track lengths never silently change
    // the mapper's baseline. Markers initially span both complete sources;
    // selecting FitMarkers is an explicit request to derive a new rate.
    p.markers = {0.0, bounds.songDuration, 0.0, bounds.videoDuration};
    return p;
}

std::optional<double> EffectiveCutoff(const Record& r, std::optional<double> mapperCutoff)
{
    return r.enabled && r.profile.stopAtEndMarker && ValidProfile(r.profile)
               ? std::optional<double>{r.profile.markers.videoEnd}
               : mapperCutoff;
}

void Draft::Open(std::string levelId, Record saved, Profile initial)
{
    levelId_ = std::move(levelId);
    saved_ = std::move(saved);
    draft_ = saved_;
    initial_ = initial;
    open_ = true;
}

bool Draft::Edit(const Profile& profile, const Bounds& bounds, std::string& error)
{
    error = open_ ? Validate(profile, bounds) : "The audio sync editor is closed.";
    if(!error.empty())
        return false;
    draft_.profile = profile;
    return true;
}

void Draft::Reset()
{
    if(open_)
        draft_.profile = initial_;
}
void Draft::Revert()
{
    if(open_)
        draft_ = saved_;
}

bool Draft::ProposeTiming(Timing timing, const Bounds& bounds, std::string& error,
                          std::optional<AnalysisSummary> summary)
{
    if(!open_ || !ValidRate(timing.playbackRate) || !ValidTime(timing.offsetSeconds) ||
       !ValidTime(bounds.songDuration) || !ValidTime(bounds.videoDuration) ||
       bounds.songDuration <= 0 || bounds.videoDuration <= 0)
    {
        error = "No valid timing proposal is available.";
        return false;
    }
    auto candidate = draft_.profile;
    candidate.timing = timing;
    candidate.method = TimingMethod::ManualSpeed;
    // Carry matching markers into Manual rather than leaving the previous
    // source-length markers behind. They bound the common playable span:
    // V=r*S+b, 0<=S<=songDuration, 0<=V<=videoDuration. Negative offsets
    // retain map lead-in and positive offsets retain video intro skipping.
    candidate.markers.songStart = std::max(0.0, -timing.offsetSeconds / timing.playbackRate);
    candidate.markers.songEnd = std::min(
        bounds.songDuration, (bounds.videoDuration - timing.offsetSeconds) / timing.playbackRate);
    // Reject disjoint ranges before evaluating/dereferencing VideoTime.
    // A large valid offset with a small rate can otherwise put songStart
    // outside the time-domain guard even though the input offset is valid.
    if(candidate.markers.songStart >= candidate.markers.songEnd)
    {
        error = "The proposed timing does not overlap the two tracks.";
        return false;
    }
    candidate.markers.videoStart = std::max(0.0, *VideoTime(timing, candidate.markers.songStart));
    candidate.markers.videoEnd =
        std::min(bounds.videoDuration, *VideoTime(timing, candidate.markers.songEnd));
    if(summary)
    {
        // Accept the proposal and its explanatory evidence as one draft edit.
        // A failed validation therefore cannot leave timing updated while the
        // result shown when this map is reopened still describes older values.
        summary->timing = timing;
        candidate.lastAnalysis = std::move(summary);
    }
    return Edit(candidate, bounds, error);
}

void Draft::SetMode(Mode mode)
{
    if(open_ && (mode == Mode::Automatic || mode == Mode::Manual))
        draft_.profile.mode = mode;
}

bool Draft::AcceptSaved(const Record& persisted)
{
    // A completion for an earlier draft must not make subsequent edits
    // appear saved. The caller also checks map/source operation generation.
    if(!open_ || persisted != draft_)
        return false;
    saved_ = persisted;
    return true;
}

void Draft::Close()
{
    open_ = false;
    levelId_.clear();
    saved_ = {};
    draft_ = {};
    initial_ = {};
}

std::string SerializeRecord(const Record& r)
{
    if(r.sourceKey.empty() || r.sourceKey.size() > 2048 ||
       r.sourceKey.find('\0') != std::string::npos ||
       !Validate(r.profile, {r.profile.markers.songEnd, r.profile.markers.videoEnd}).empty())
        throw std::invalid_argument("Invalid Advanced Sync record");
    const auto& p = r.profile;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject();
    w.Key("version");
    w.Int(2);
    w.Key("sourceKey");
    w.String(r.sourceKey.data(), static_cast<rapidjson::SizeType>(r.sourceKey.size()));
    w.Key("enabled");
    w.Bool(r.enabled);
    auto number = [&](const char* key, double value)
    {
        w.Key(key);
        w.Double(value);
    };
    auto integer = [&](const char* key, int value)
    {
        w.Key(key);
        w.Int(value);
    };
    auto boolean = [&](const char* key, bool value)
    {
        w.Key(key);
        w.Bool(value);
    };
    number("offsetSeconds", p.timing.offsetSeconds);
    number("playbackRate", p.timing.playbackRate);
    number("songStart", p.markers.songStart);
    number("songEnd", p.markers.songEnd);
    number("videoStart", p.markers.videoStart);
    number("videoEnd", p.markers.videoEnd);
    integer("mode", static_cast<int>(p.mode));
    integer("method", static_cast<int>(p.method));
    integer("routing", static_cast<int>(p.routing));
    integer("timePrecision", static_cast<int>(p.timePrecision));
    integer("speedPrecision", static_cast<int>(p.speedPrecision));
    boolean("stopAtEndMarker", p.stopAtEndMarker);
    boolean("pitchCorrection", p.pitchCorrection);
    number("autoPitchSemitones", p.autoPitchSemitones);
    number("finePitchCents", p.finePitchCents);
    boolean("showPicture", p.showPicture);
    boolean("loopSelection", p.loopSelection);
    integer("anchorCount", p.anchorCount);
    number("windowSeconds", p.windowSeconds);
    integer("waveformLayout", p.waveformLayout);
    integer("waveformZoom", p.waveformZoom);
    integer("waveformHeightScale", p.waveformHeightScale);
    w.Key("lastAnalysis");
    if(!p.lastAnalysis)
        w.Null();
    else
    {
        const auto& summary = *p.lastAnalysis;
        w.StartObject();
        integer("confidence", static_cast<int>(summary.confidence));
        number("offsetSeconds", summary.timing.offsetSeconds);
        number("playbackRate", summary.timing.playbackRate);
        integer("requestedPoints", summary.requestedPoints);
        integer("acceptedPoints", summary.acceptedPoints);
        integer("rejectedPoints", summary.rejectedPoints);
        integer("validationPoints", summary.validationPoints);
        number("fitRmsSeconds", summary.fitRmsSeconds);
        number("validationMaxSeconds", summary.validationMaxSeconds);
        boolean("validationFinite", summary.validationFinite);
        boolean("incompatible", summary.incompatible);
        w.EndObject();
    }
    w.EndObject();
    return {buffer.GetString(), buffer.GetSize()};
}

std::optional<Record> ParseRecord(const std::string& json, std::string& error)
{
    error = "Invalid or unsupported Advanced Sync settings; basic timing remains available.";
    if(json.size() > 16384)
        return std::nullopt;
    rapidjson::Document d;
    d.Parse(json.data(), json.size());
    if(d.HasParseError() || !d.IsObject())
        return std::nullopt;
    auto field = [&](const char* key) -> const rapidjson::Value*
    {
        const auto it = d.FindMember(key);
        return it == d.MemberEnd() ? nullptr : &it->value;
    };
    const auto* version = field("version");
    const auto* key = field("sourceKey");
    if(!version || !version->IsInt() || version->GetInt() < 1 || version->GetInt() > 2 || !key || !key->IsString() ||
       key->GetStringLength() == 0 || key->GetStringLength() > 2048)
        return std::nullopt;
    Record r;
    r.sourceKey.assign(key->GetString(), key->GetStringLength());
    if(r.sourceKey.find('\0') != std::string::npos)
        return std::nullopt;
    bool valid = true;
    auto number = [&](const char* name, double& target)
    {
        const auto* value = field(name);
        if(!value || !value->IsNumber() || !std::isfinite(value->GetDouble()))
            valid = false;
        else
            target = value->GetDouble();
    };
    auto boolean = [&](const char* name, bool& target)
    {
        const auto* value = field(name);
        if(!value || !value->IsBool())
            valid = false;
        else
            target = value->GetBool();
    };
    auto integer = [&](const char* name, int maximum) -> int
    {
        const auto* value = field(name);
        if(!value || !value->IsInt() || value->GetInt() < 0 || value->GetInt() > maximum)
        {
            valid = false;
            return 0;
        }
        return value->GetInt();
    };
    auto& p = r.profile;
    boolean("enabled", r.enabled);
    number("offsetSeconds", p.timing.offsetSeconds);
    number("playbackRate", p.timing.playbackRate);
    number("songStart", p.markers.songStart);
    number("songEnd", p.markers.songEnd);
    number("videoStart", p.markers.videoStart);
    number("videoEnd", p.markers.videoEnd);
    p.mode = static_cast<Mode>(integer("mode", 1));
    p.method = static_cast<TimingMethod>(integer("method", 1));
    p.routing = static_cast<Routing>(integer("routing", 4));
    p.timePrecision = static_cast<Precision>(integer("timePrecision", 3));
    p.speedPrecision = static_cast<Precision>(integer("speedPrecision", 3));
    boolean("stopAtEndMarker", p.stopAtEndMarker);
    boolean("pitchCorrection", p.pitchCorrection);
    number("autoPitchSemitones", p.autoPitchSemitones);
    number("finePitchCents", p.finePitchCents);
    boolean("showPicture", p.showPicture);
    boolean("loopSelection", p.loopSelection);
    p.anchorCount = integer("anchorCount", 16);
    number("windowSeconds", p.windowSeconds);
    if(version->GetInt() >= 2)
    {
        p.waveformLayout = integer("waveformLayout", 2);
        p.waveformZoom = integer("waveformZoom", 20);
        p.waveformHeightScale = integer("waveformHeightScale", 3);
        const auto* storedSummary = field("lastAnalysis");
        if(!storedSummary)
            valid = false;
        else if(!storedSummary->IsNull())
        {
            if(!storedSummary->IsObject())
                valid = false;
            else
            {
                AnalysisSummary summary;
                auto summaryField = [&](const char* name) -> const rapidjson::Value*
                {
                    const auto it = storedSummary->FindMember(name);
                    return it == storedSummary->MemberEnd() ? nullptr : &it->value;
                };
                auto summaryNumber = [&](const char* name, double& target)
                {
                    const auto* value = summaryField(name);
                    if(!value || !value->IsNumber() || !std::isfinite(value->GetDouble()))
                        valid = false;
                    else
                        target = value->GetDouble();
                };
                auto summaryInteger = [&](const char* name, int maximum) -> int
                {
                    const auto* value = summaryField(name);
                    if(!value || !value->IsInt() || value->GetInt() < 0 ||
                       value->GetInt() > maximum)
                    {
                        valid = false;
                        return 0;
                    }
                    return value->GetInt();
                };
                auto summaryBoolean = [&](const char* name, bool& target)
                {
                    const auto* value = summaryField(name);
                    if(!value || !value->IsBool())
                        valid = false;
                    else
                        target = value->GetBool();
                };
                summary.confidence =
                    static_cast<Confidence>(summaryInteger("confidence", 2));
                summaryNumber("offsetSeconds", summary.timing.offsetSeconds);
                summaryNumber("playbackRate", summary.timing.playbackRate);
                summary.requestedPoints = summaryInteger("requestedPoints", 64);
                summary.acceptedPoints = summaryInteger("acceptedPoints", 64);
                summary.rejectedPoints = summaryInteger("rejectedPoints", 64);
                summary.validationPoints = summaryInteger("validationPoints", 64);
                summaryNumber("fitRmsSeconds", summary.fitRmsSeconds);
                summaryNumber("validationMaxSeconds", summary.validationMaxSeconds);
                summaryBoolean("validationFinite", summary.validationFinite);
                summaryBoolean("incompatible", summary.incompatible);
                p.lastAnalysis = summary;
            }
        }
    }
    if(!valid || !Validate(p, {p.markers.songEnd, p.markers.videoEnd}).empty())
        return std::nullopt;
    error.clear();
    return r;
}
} // namespace BigScreen::AudioSync
