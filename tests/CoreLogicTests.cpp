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

    Expect(IsRepeatedWithin("same", "same", std::chrono::seconds(179)),
           "same error inside three minutes trips");
    Expect(!IsRepeatedWithin("same", "same", std::chrono::seconds(181)),
           "same error outside three minutes does not trip");
    Expect(!IsRepeatedWithin("one", "two", std::chrono::seconds(1)),
           "different errors do not trip");

    if(failures == 0)
        std::cout << "All Big Screen core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
