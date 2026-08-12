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

    if(failures == 0)
        std::cout << "All Big Screen core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
