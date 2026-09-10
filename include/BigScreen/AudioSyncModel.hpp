// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace BigScreen::AudioSync
{
enum class Mode
{
    Automatic,
    Manual
};
enum class TimingMethod
{
    ManualSpeed,
    FitMarkers
};
enum class Routing
{
    MapLeft,
    VideoLeft,
    Mixed,
    MapOnly,
    VideoOnly
};
enum class Precision
{
    Tenths,
    Hundredths,
    Thousandths,
    TenThousandths
};
enum class Confidence
{
    Low,
    Medium,
    High
};

struct Timing
{
    double offsetSeconds = 0.0;
    double playbackRate = 1.0;
    bool operator==(const Timing&) const = default;
};

struct Markers
{
    // Coordinates are on the ORIGINAL song and FINAL installed video
    // timelines. Silence detection never changes either origin.
    double songStart = 0.0;
    double songEnd = 0.0;
    double videoStart = 0.0;
    double videoEnd = 0.0;
    bool operator==(const Markers&) const = default;
};

/// Durable summary of the most recently accepted automatic analysis. The
/// analyzer's full anchor list can be large and is useful only while that
/// worker result is being reviewed, so the per-map record keeps only the
/// values needed to reproduce the editor's result summary on a later visit.
/// `timing` is the automatic proposal itself; it intentionally remains
/// separate from Profile::timing because the user may subsequently fine-tune
/// the active timing in Manual Adjustment.
struct AnalysisSummary
{
    Confidence confidence = Confidence::Low;
    Timing timing;
    int requestedPoints = 0;
    int acceptedPoints = 0;
    int rejectedPoints = 0;
    int validationPoints = 0;
    double fitRmsSeconds = 0.0;
    double validationMaxSeconds = 0.0;
    bool validationFinite = true;
    bool incompatible = false;
    bool operator==(const AnalysisSummary&) const = default;
};

struct Profile
{
    Timing timing;
    Markers markers;
    Mode mode = Mode::Automatic;
    TimingMethod method = TimingMethod::ManualSpeed;
    Routing routing = Routing::MapLeft;
    Precision timePrecision = Precision::Hundredths;
    Precision speedPrecision = Precision::Hundredths;
    bool stopAtEndMarker = false;
    bool pitchCorrection = false;
    double autoPitchSemitones = 0.0;
    double finePitchCents = 0.0;
    bool showPicture = true;
    bool loopSelection = false;
    int anchorCount = 8;
    double windowSeconds = 8.0;
    // These are editor presentation choices, but they are deliberately stored
    // with the map profile so reopening Advanced Sync restores the same working
    // view instead of silently returning to a generic layout.
    int waveformLayout = 0;     // 0=side by side, 1=stacked, 2=overlay
    int waveformZoom = 0;       // 0=full track, otherwise an even 2x..20x
    int waveformHeightScale = 0; // 0=default, 1=2x, 2=4x, 3=8x
    std::optional<AnalysisSummary> lastAnalysis;
    bool operator==(const Profile&) const = default;
};

struct Record
{
    // Scope includes the assignment, not only the map: replacing a video
    // must not silently apply another video's saved marker coordinates.
    // The integration layer supplies a stable fingerprint, not a signed URL.
    std::string sourceKey;
    bool enabled = false;
    Profile profile;
    bool operator==(const Record&) const = default;
};

struct Bounds
{
    double songDuration = 0.0;
    double videoDuration = 0.0;
};

/// Carries the explicit affine conversion from decoded sample coordinates
/// to the corresponding source timeline. The decoder supplies the first
/// usable PTS AFTER its own priming/pre-skip handling. Applying codec delay
/// a second time here would introduce the very error we are measuring.
struct SampleTimeline
{
    double firstSamplePtsSeconds = 0.0;
    double sourceToFinalShiftSeconds = 0.0;
    int sampleRate = 0;
    std::optional<double> TimeAt(std::int64_t sampleIndex) const;
};

double Step(Precision precision);
int DecimalPlaces(Precision precision);
std::optional<Timing> FitMarkers(const Markers& markers);
std::optional<double> AutoPitch(double playbackRate);
std::optional<double> VideoTime(const Timing& timing, double songSeconds);
std::string Validate(const Profile& profile, const Bounds& bounds);
Profile InitialProfile(Timing mapperTiming, const Bounds& bounds);
std::optional<double> EffectiveCutoff(const Record& record, std::optional<double> mapperCutoff);

/// Small value-only edit transaction. Persistence and UI are deliberately
/// absent: the owner sends Candidate() to the existing durable library
/// writer, then calls AcceptSaved ONLY after that write succeeds. Failure
/// therefore leaves both baseline and draft available for retry/discard.
class Draft final
{
  public:
    void Open(std::string levelId, Record saved, Profile initial);
    bool IsOpen() const { return open_; }
    bool Dirty() const { return open_ && draft_ != saved_; }
    const std::string& LevelId() const { return levelId_; }
    const Record& Candidate() const { return draft_; }
    const Record& Saved() const { return saved_; }
    const Profile& Initial() const { return initial_; }
    bool Edit(const Profile& profile, const Bounds& bounds, std::string& error);
    /// Automatic proposals become the SAME draft used by Manual. Mode
    /// changes never reset timing or save. Analyze itself accepts sources
    /// and effort only, so manual edits cannot bias a later fresh search.
    bool ProposeTiming(Timing timing, const Bounds& bounds, std::string& error,
                       std::optional<AnalysisSummary> summary = std::nullopt);
    void SetMode(Mode mode);
    void Reset();
    void Revert();
    bool AcceptSaved(const Record& persisted);
    void Close();

  private:
    bool open_ = false;
    std::string levelId_;
    Record saved_;
    Record draft_;
    Profile initial_;
};

/// Strict, versioned JSON codec shared by host tests and library.json.
/// Missing/invalid/newer schemas return no record (basic mode), never an
/// implicitly enabled profile assembled from partially corrupt fields.
std::optional<Record> ParseRecord(const std::string& json, std::string& error);
std::string SerializeRecord(const Record& record);
} // namespace BigScreen::AudioSync
