// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
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
    const double previewStep = BigScreen::CoreLogic::AdvanceSmoothedPreviewClock(
        10.0, 10.0, 1.0 / 90.0);
    Expect(previewStep > 10.0 && previewStep < 10.012,
           "a quantized audio sample still advances the smoothed preview clock");
    const double correctedPreviewStep = BigScreen::CoreLogic::AdvanceSmoothedPreviewClock(
        previewStep, 10.021, 1.0 / 90.0);
    Expect(correctedPreviewStep > previewStep && correctedPreviewStep < 10.03,
           "a new audio buffer corrects the preview clock without a frame-sized jump");
    Expect(std::abs(BigScreen::CoreLogic::AdvanceSmoothedPreviewClock(
               10.0, 20.0, 0.01) - 20.0) < 0.0001,
           "a real preview seek immediately re-anchors the smoothed clock");

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

    Expect(std::abs(CpuPercentOfOneCore(500.0, 1.0) - 50.0) < 0.001,
           "half a CPU-second over one wall-second is 50 percent of one core");
    Expect(std::abs(EquivalentCpuCores(2500.0, 1.0) - 2.5) < 0.001,
           "whole-process CPU time can correctly exceed one equivalent core");
    Expect(CpuPercentOfOneCore(500.0, 0.0) == 0.0,
           "zero wall duration cannot produce an invalid CPU percentage");
    Expect(ChargeConsumedMicroampHours(3'166'000, 3'165'000) == 1'000.0,
           "a falling charge counter reports consumed microamp-hours");
    Expect(ChargeConsumedMicroampHours(3'165'000, 3'166'000) == 0.0,
           "charging does not masquerade as negative consumption");
    Expect(std::abs(ConsumptionRateMahPerHour(1'000.0, 1800.0) - 2.0) < 0.001,
           "charge consumption is normalized to mAh per hour");

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
    Expect(performanceHistory.RecordReduction(15, 720),
           "the 720p tier is retained in the five-step 1440p recovery ladder");
    Expect(performanceHistory.RecordReduction(15, 480),
           "the fifth 1440p/60 reduction fits in the fixed history");
    Expect(!performanceHistory.RecordReduction(15, 480),
           "the fixed history rejects only a sixth impossible reduction");
    auto recovery = performanceHistory.RecoveryTarget();
    Expect(recovery && recovery->first == 15 && recovery->second == 480,
           "recovery first restores the exact most recent tier");
    Expect(performanceHistory.CommitRecovery(),
           "a successful recovery removes exactly one tier");
    recovery = performanceHistory.RecoveryTarget();
    Expect(recovery && recovery->first == 15 && recovery->second == 720,
           "the next recovery restores the preceding resolution tier");
    performanceHistory.Reset();
    Expect(performanceHistory.Empty() && !performanceHistory.RecoveryTarget(),
           "a new playback baseline clears stale recovery tiers");
    Expect(performanceHistory.RecordReduction(60, 1080),
           "the ongoing controller can reduce again after a full recovery");
    Expect(performanceHistory.CommitRecovery() && performanceHistory.Empty(),
           "the repeated recovery returns to the exact new baseline");

    Expect(IsSupportedVideoCodec(VideoCodecKind::H264) &&
           IsSupportedVideoCodec(VideoCodecKind::Hevc) &&
           IsSupportedVideoCodec(VideoCodecKind::Vp8) &&
           IsSupportedVideoCodec(VideoCodecKind::Vp9),
           "all four advertised codecs are represented in shared policy");
    Expect(!IsSupportedVideoCodec(VideoCodecKind::Unknown),
           "unknown codecs are rejected by shared policy");
    Expect(VideoCodecRequiresHardware(VideoCodecKind::Hevc) &&
           !VideoCodecRequiresHardware(VideoCodecKind::H264),
           "HEVC alone is hardware-only regardless of source size");
    Expect(SoftwareFallbackBlockedReason(VideoCodecKind::H264, 1080).empty(),
           "1080p H.264 can fall back to software");
    Expect(!SoftwareFallbackBlockedReason(VideoCodecKind::Vp9, 1440).empty(),
           "1440p VP9 refuses software fallback");
    Expect(SoftwareFallbackBlockedReason(VideoCodecKind::H264, 2160)
               .find("2160p") != std::string::npos,
           "greater-than-1080p refusal reports the actual source height");
    Expect(UnsupportedVideoSignalReason(
               VideoCodecKind::H264, true, false, false, true)
               .find("HDR") != std::string::npos,
           "HDR is rejected with an explicit reason");
    Expect(UnsupportedVideoSignalReason(
               VideoCodecKind::Vp9, false, true, false, true)
               .find("profile 2") != std::string::npos,
           "10-bit VP9 is rejected with codec-specific guidance");
    Expect(UnsupportedVideoSignalReason(
               VideoCodecKind::H264, false, false, false, true).empty(),
           "8-bit SDR 4:2:0 remains compatible");

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

    Expect(ScreenScaleMaximum(false) == 8.0f,
           "flat screens allow the expanded 8x size");
    Expect(ScreenScaleMaximum(true) == 8.0f,
           "curved screens allow the expanded 8x size");
    Expect(NormalizeScreenScale(8.5f, true) == 8.0f,
           "curved screens clamp values above the shared ceiling");
    Expect(NormalizeScreenScale(7.5f, false) == 7.5f,
           "flat screens preserve values within the expanded range");

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
           "letterbox transparency removes the black background renderer");
    Expect(ScreenBackgroundVisible(false, false),
           "opaque letterboxes retain the black background renderer");
    Expect(ScreenBackgroundVisible(true, true),
           "a requested black lead-in overrides transparent letterboxing");
    Expect(!ScreenBackgroundVisible(false, false, true),
           "a full-frame picture removes an unnecessary opaque backing mesh");
    Expect(ScreenBackgroundVisible(false, false, false),
           "an uncovered opaque frame retains its black letterbox backing");
    Expect(VideoLayerOffset(4.0f, 3.0f) == -0.015f,
           "ordinary canvases retain the established minimum layer offset");
    Expect(VideoLayerOffset(100.0f, 40.0f) == -0.2f,
           "large canvases receive proportionally stable layer separation");

    Expect(SynchronizedPreviewReady(-1.214, false, false),
           "an intentional negative lead-in may start synchronized audio without a frame");
    Expect(!SynchronizedPreviewReady(0.0, false, false),
           "visible media time still waits for the decoder's first uploaded frame");
    Expect(SynchronizedPreviewReady(0.0, false, true),
           "visible media time becomes ready after its first picture reaches Unity");
    Expect(SynchronizedPreviewReady(120.0, true, false),
           "configured post-roll advances without an impossible decoded picture");

    CornerWarpSettings warp;
    warp.corners[0] = {-2.0f, -1.0f, 0.0f};
    warp.corners[1] = {-1.0f, 3.0f, 2.0f};
    warp.corners[2] = {4.0f, 5.0f, 6.0f};
    warp.corners[3] = {2.0f, -3.0f, 4.0f};
    const auto exactTopRight = BilinearCornerOffset(warp, 1.0f, 1.0f);
    Expect(exactTopRight.x == 4.0f && exactTopRight.y == 5.0f &&
               exactTopRight.z == 6.0f,
           "bilinear corner deformation preserves authored corner values");
    const auto centerWarp = BilinearCornerOffset(warp, 0.5f, 0.5f);
    Expect(std::abs(centerWarp.x - 0.75f) < 0.001f &&
               std::abs(centerWarp.y - 1.0f) < 0.001f &&
               std::abs(centerWarp.z - 3.0f) < 0.001f,
           "bilinear corner deformation continuously blends all four corners");

    FlagWaveSettings wave;
    wave.enabled = true;
    wave.depth = 8.0f;
    wave.ripple = 2.0f;
    wave.anchorRamp = 1.0f;
    const auto anchored = FlagWaveOffset(wave, 0.0f, 0.25);
    Expect(std::abs(anchored.y) < 0.001f && std::abs(anchored.z) < 0.001f,
           "the selected flag edge remains stationary");
    const auto freeEdge = FlagWaveOffset(wave, 1.0f, 0.0);
    Expect(std::abs(freeEdge.y) > 1.9f,
           "the free flag edge receives the full authored amplitude");

    SurfaceDeformationSettings coverDeformation;
    coverDeformation.enabled = true;
    coverDeformation.fillMode = DeformationFillMode::AutoZoomCover;
    coverDeformation.cornerWarp.corners[0].x = -2.0f;
    coverDeformation.cornerWarp.corners[1].x = -2.0f;
    coverDeformation.cornerWarp.corners[2].x = 2.0f;
    coverDeformation.cornerWarp.corners[3].x = 2.0f;
    Expect(DeformationAutoCoverScale(10.0f, 6.0f, coverDeformation) > 1.39f,
           "auto-cover crops inward when deformation expands the canvas");
    coverDeformation.fillMode = DeformationFillMode::StretchToFill;
    Expect(DeformationAutoCoverScale(10.0f, 6.0f, coverDeformation) == 1.0f,
           "stretch mode preserves the original UV range");

    FracturePatternSettings fractureSettings;
    fractureSettings.seed = CuratedFractureSeeds[0].second;
    fractureSettings.pieceCount = 72;
    fractureSettings.impactPoint = {0.43f, 0.58f};
    fractureSettings.spokeCount = 11;
    fractureSettings.ringCount = 3;
    fractureSettings.jitter = 0.2f;
    const auto fracture = GenerateFracturePattern(fractureSettings);
    Expect(fracture.cells.size() == 72,
           "the requested nondegenerate Voronoi shard count is preserved");
    double cellArea = 0.0;
    double triangleArea = 0.0;
    bool allVerticesInside = true;
    bool allShardTrianglesFacePlayer = true;
    for(const auto& cell : fracture.cells)
    {
        cellArea += FracturePolygonArea(cell.vertices);
        const auto triangles = TriangulateFractureCell(cell);
        for(const auto& triangle : triangles)
        {
            triangleArea += FracturePolygonArea(
                {triangle.a, triangle.b, triangle.c});
            const float signedTwiceArea =
                (triangle.b.x - triangle.a.x) *
                    (triangle.c.y - triangle.a.y) -
                (triangle.b.y - triangle.a.y) *
                    (triangle.c.x - triangle.a.x);
            allShardTrianglesFacePlayer =
                allShardTrianglesFacePlayer && signedTwiceArea < 0.0f;
        }
        for(const auto& point : cell.vertices)
            allVerticesInside = allVerticesInside &&
                point.x >= -0.0001f && point.x <= 1.0001f &&
                point.y >= -0.0001f && point.y <= 1.0001f;
    }
    Expect(allVerticesInside && std::abs(cellArea - 1.0) < 0.0005,
           "Voronoi cells tile the normalized pane without gaps or overflow");
    Expect(std::abs(cellArea - triangleArea) < 0.0005,
           "fan triangulation preserves total fracture-cell area");
    Expect(allShardTrianglesFacePlayer,
           "fracture shards use the video mesh's player-facing winding");

    const auto sameFracture = GenerateFracturePattern(fractureSettings);
    bool deterministic = fracture.cells.size() == sameFracture.cells.size() &&
                         fracture.edges.size() == sameFracture.edges.size();
    for(std::size_t index = 0;
        deterministic && index < fracture.cells.size(); ++index)
    {
        deterministic = fracture.cells[index].vertices.size() ==
            sameFracture.cells[index].vertices.size();
        for(std::size_t vertex = 0;
            deterministic && vertex < fracture.cells[index].vertices.size(); ++vertex)
        {
            deterministic = SameFracturePoint(
                fracture.cells[index].vertices[vertex],
                sameFracture.cells[index].vertices[vertex], 0.0000001f);
        }
    }
    Expect(deterministic,
           "the seeded fracture generator is byte-stable for equal inputs");
    fractureSettings.seed ^= 0x9E3779B9U;
    const auto differentFracture = GenerateFracturePattern(fractureSettings);
    Expect(!SameFracturePoint(
               fracture.cells[1].site, differentFracture.cells[1].site,
               0.000001f),
           "different fracture seeds produce different cell sites");

    fractureSettings.pieceCount = 500;
    const auto cappedFracture = GenerateFracturePattern(fractureSettings);
    Expect(cappedFracture.cells.size() <= MaximumFracturePieces,
           "fracture generation enforces the 200-piece safety ceiling");

    const std::vector<FracturePoint> impacts{
        {0.2f, 0.2f}, {0.8f, 0.25f}, {0.5f, 0.8f}};
    const auto revealGroups = PartitionFractureRevealGroups(
        fracture.edges, impacts);
    bool validPartition = revealGroups.size() == fracture.edges.size();
    for(const auto group : revealGroups)
        validPartition = validPartition && group < impacts.size();
    Expect(validPartition,
           "reveal grouping assigns every fracture edge exactly once");
    Expect(!fracture.radialEdges.empty() &&
               SameFracturePoint(
                   fracture.radialEdges.front().from,
                   {0.43f, 0.58f}, 0.0001f),
           "the radial spider-web begins in the requested impact region");

    if(failures == 0)
        std::cout << "All Big Screen core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
