#include <chrono>
#include <iostream>
#include <string>

#include "BigScreen/CoreLogic.hpp"

namespace {
    int failures = 0;
    void Expect(bool condition, const char* description)
    {
        if(condition) return;
        ++failures;
        std::cerr << "FAILED: " << description << '\n';
    }
}

int main()
{
    using BigScreen::CoreLogic::IsSupportedYouTubeUrl;
    using BigScreen::CoreLogic::IsValidYouTubeVideoId;

    Expect(IsSupportedYouTubeUrl("https://www.youtube.com/watch?v=TcHvEFxk_78"),
           "normal YouTube watch URLs should be accepted");
    Expect(IsSupportedYouTubeUrl("https://youtu.be/TcHvEFxk_78"),
           "YouTube share URLs should be accepted");
    Expect(IsSupportedYouTubeUrl("https://www.youtube-nocookie.com/embed/TcHvEFxk_78"),
           "YouTube privacy embed URLs should be accepted");
    Expect(!IsSupportedYouTubeUrl("http://youtube.com/watch?v=TcHvEFxk_78"),
           "unencrypted URLs should be rejected");
    Expect(!IsSupportedYouTubeUrl("https://youtube.com.example.invalid/video"),
           "lookalike hosts should be rejected");
    Expect(!IsSupportedYouTubeUrl("https://youtube.com@evil.invalid/video"),
           "userinfo host confusion should be rejected");
    Expect(IsValidYouTubeVideoId("TcHvEFxk_78"), "valid video IDs should be accepted");
    Expect(!IsValidYouTubeVideoId("abc"), "short video IDs should be rejected");
    using namespace BigScreen::CoreLogic;
    Expect(StableVideoKey("custom_level_123") == StableVideoKey("custom_level_123"),
           "stable keys must be deterministic");
    Expect(StableVideoKey("custom_level_123") != StableVideoKey("custom_level_124"),
           "different IDs should not share the test key");
    Expect(StableVideoKey("x").size() == 16, "stable keys are fixed-width hex");

    Expect(SafeSingleFilename("video.mp4"), "plain filename is safe");
    Expect(!SafeSingleFilename("../video.mp4"), "parent traversal is rejected");
    Expect(!SafeSingleFilename("folder/video.mp4"), "nested path is rejected");
    Expect(!SafeSingleFilename("C:\\video.mp4"), "drive path is rejected");

    auto [changed60, tier60] = NextPerformanceTier(60, 1080);
    Expect(changed60 && tier60.first == 30 && tier60.second == 1080,
           "performance fallback lowers FPS before resolution");
    auto [changed15, tier15] = NextPerformanceTier(15, 1080);
    Expect(changed15 && tier15.first == 15 && tier15.second == 720,
           "resolution falls after reaching 15 FPS");
    auto [changedMin, tierMin] = NextPerformanceTier(15, 480);
    Expect(!changedMin && tierMin.second == 480, "minimum tier is stable");

    AutomaticPerformanceHistory performanceHistory;
    Expect(performanceHistory.Empty(), "automatic quality history starts empty");
    Expect(performanceHistory.RecordReduction(60, 1080),
           "the initial FPS quality tier is recorded");
    Expect(performanceHistory.RecordReduction(30, 1080),
           "the second FPS quality tier is recorded");
    Expect(performanceHistory.RecordReduction(15, 1080),
           "the first resolution quality tier is recorded");
    auto recovery = performanceHistory.RecoveryTarget();
    Expect(recovery && recovery->first == 15 && recovery->second == 1080,
           "recovery first restores the most recent resolution tier");
    Expect(performanceHistory.CommitRecovery(),
           "a successful recovery removes exactly one tier");
    recovery = performanceHistory.RecoveryTarget();
    Expect(recovery && recovery->first == 30 && recovery->second == 1080,
           "the next recovery reverses the last FPS reduction");
    performanceHistory.Reset();
    Expect(performanceHistory.Empty() && !performanceHistory.RecoveryTarget(),
           "a new playback baseline clears stale recovery tiers");
    Expect(performanceHistory.RecordReduction(60, 1080),
           "the ongoing controller can reduce again after a full recovery");
    Expect(performanceHistory.CommitRecovery() && performanceHistory.Empty(),
           "the repeated recovery returns to the exact new baseline");

    Expect(!ShouldSampleGameplayFrame(9.9, 120.0, false),
           "gameplay FPS excludes the first ten seconds");
    Expect(ShouldSampleGameplayFrame(10.0, 120.0, false),
           "gameplay FPS begins at ten seconds");
    Expect(ShouldSampleGameplayFrame(120.0, 120.0, false),
           "the final note frame remains part of gameplay");
    Expect(!ShouldSampleGameplayFrame(120.01, 120.0, false),
           "results-transition frames after the final note are excluded");
    Expect(!ShouldSampleGameplayFrame(60.0, 120.0, true),
           "a latched final-note boundary stays frozen after a replay seek");
    Expect(ShouldSampleGameplayFrame(30.0, std::nullopt, false),
           "maps without a discovered final note keep the safe gameplay fallback");

    // A higher output cap reuses source frames; it must not manufacture misses.
    Expect(ExpectedPresentedFrames(5.0, 30.0, 1.0, 60) == 150,
           "30 FPS media expects 150 distinct frames over five seconds");
    Expect(ExpectedPresentedFrames(5.0, 24.0, 1.0, 30) == 120,
           "24 FPS media is source-limited under a 30 FPS cap");
    Expect(ExpectedPresentedFrames(5.0, 60.0, 1.0, 30) == 150,
           "60 FPS media is output-limited under a 30 FPS cap");
    Expect(ExpectedPresentedFrames(5.0, 30.0, 0.5, 60) == 75,
           "slow playback reduces source-frame demand");
    Expect(ExpectedPresentedFrames(5.0, 30.0, 2.0, 60) == 300,
           "fast playback increases demand up to the output cap");
    Expect(MissedFramePercent(150, 150) == 0.0,
           "presenting every expected frame reports no misses");
    Expect(MissedFramePercent(150, 165) == 0.0,
           "extra startup or seek frames do not create negative misses");
    Expect(std::abs(MissedFramePercent(150, 135) - 10.0) < 0.001,
           "miss percentage uses expected source-aware cadence");
    Expect(PresentedFrameIntervals(1.0, 1.0 / 30.0, 1.0 + 1.0 / 30.0, 1.0, 30) == 1,
           "an ordinary delivered 30 FPS transition is not skipped");
    Expect(PresentedFrameIntervals(1.0, 1.0 / 30.0, 1.0 + 3.0 / 30.0, 1.0, 30) == 3,
           "a timestamp gap exposes two skipped output pictures");
    Expect(PresentedFrameIntervals(1.0, 1.0 / 60.0, 1.0 + 1.0 / 30.0, 1.0, 30) == 1,
           "a 30 FPS cap intentionally selects every other 60 FPS source picture");
    Expect(PresentedFrameIntervals(1.0, 1.0 / 24.0, 1.0 + 1.0 / 24.0, 1.0, 30) == 1,
           "a 24 FPS source under a 30 FPS ceiling does not manufacture misses");
    Expect(PresentedFrameIntervals(1.0, 1.0 / 24.0, 1.0 + 2.0 / 24.0, 2.0, 30) == 1,
           "fast fit playback samples a 24 FPS source under the output ceiling");
    Expect(PresentedFrameIntervals(1.0, 1.0 / 24.0, 1.0 + 1.0 / 24.0, 0.5, 30) == 1,
           "slow fit playback preserves each native source-frame hold");
    Expect(PresentedFrameIntervals(1.0, 0.1, 1.1, 1.0, 60) == 1,
           "a variable-frame-rate hold uses its declared duration");
    Expect(PresentedFrameIntervals(4.0, 1.0 / 30.0, 2.0, 1.0, 30) == 1,
           "a seek or loop restart does not manufacture skipped frames");

    Expect(PreviewReachedLoopBoundary(179.98, 180.0, 180.0),
           "preview reaches its loop boundary just before the final sample");
    Expect(PreviewReachedLoopBoundary(174.98, 180.0, 175.0),
           "a shorter loaded audio clip controls the preview loop boundary");
    Expect(PreviewReachedLoopBoundary(174.98, 175.0, 180.0),
           "a shorter map duration controls the preview loop boundary");
    Expect(!PreviewReachedLoopBoundary(174.0, 180.0, 175.0),
           "ordinary preview playback does not loop early");
    Expect(!PreviewReachedLoopBoundary(0.0, 0.0, 0.0),
           "unknown preview durations do not create a permanent loop");

    const auto gameplayFps = SummarizeFrameRate(
        1.0 / 120.0,
        1.0 / 60.0,
        3.0 / 120.0 + 1.0 / 60.0,
        4);
    Expect(std::abs(gameplayFps.minimumFps - 60.0) < 0.001,
           "the longest gameplay frame determines minimum FPS");
    Expect(std::abs(gameplayFps.maximumFps - 120.0) < 0.001,
           "the shortest gameplay frame determines maximum FPS");
    Expect(std::abs(gameplayFps.averageFps - 96.0) < 0.001,
           "gameplay average FPS uses frames divided by elapsed time");
    Expect(SummarizeFrameRate(0.0, 0.0, 0.0, 0).sampledFrames == 0,
           "an empty gameplay sample produces empty FPS statistics");

    Expect(IsSecondFailureWithin(std::chrono::seconds(179)),
           "a second internal failure inside three minutes trips");
    Expect(!IsSecondFailureWithin(std::chrono::seconds(181)),
           "a later internal failure starts a new three-minute window");

    Expect(DownloadProgressFraction(25, 100) == 0.25f,
           "download progress reports the transferred byte fraction");
    Expect(DownloadProgressFraction(25, 0) == 0.0f,
           "unknown download totals do not divide by zero");
    Expect(DownloadProgressFraction(125, 100) == 1.0f,
           "download progress is clamped when an estimate changes");

    Expect(ScreenScaleMaximum(false) == 4.0f,
           "flat screens allow the expanded 4x size");
    Expect(ScreenScaleMaximum(true) == 2.5f,
           "curved screens retain the safe 2.5x size cap");
    Expect(NormalizeScreenScale(4.0f, true) == 2.5f,
           "enabling curvature clamps an oversized flat screen");
    Expect(NormalizeScreenScale(2.5f, false) == 2.5f,
           "returning to flat mode preserves the current size");

    const auto wideInSquare = FitVideoContent(4.0f, 4.0f, 16.0f / 9.0f, false, 1.0f);
    Expect(wideInSquare.width == 4.0f && wideInSquare.height == 2.25f,
           "aspect-preserving video is letterboxed inside a square frame");
    const auto stretched = FitVideoContent(4.0f, 3.0f, 16.0f / 9.0f, true, 1.0f);
    Expect(stretched.width == 4.0f && stretched.height == 3.0f,
           "stretch mode fills both saved frame dimensions");
    const auto zoomed = FitVideoContent(4.0f, 4.0f, 16.0f / 9.0f, false, 2.0f);
    Expect(zoomed.width == 8.0f && zoomed.height == 4.5f,
           "video zoom scales content without changing the frame");
    const auto clampedZoom = FitVideoContent(4.0f, 4.0f, 1.0f, false, 99.0f);
    Expect(clampedZoom.width == 12.0f && clampedZoom.height == 12.0f,
           "video zoom is capped at the documented 3x limit");

    Expect(!ScreenBackgroundVisible(true, false),
           "transparent layouts remove the black letterbox renderer");
    Expect(ScreenBackgroundVisible(false, false),
           "opaque layouts retain the black letterbox renderer");
    Expect(ScreenBackgroundVisible(true, true),
           "a requested black lead-in overrides transparent letterboxing");

    if(failures == 0)
        std::cout << "All Big Screen core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
