#include "BigScreen/UpDownShowcaseTimeline.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>

namespace {
    using namespace BigScreen::UpDownShowcase;

    double SecondsAtBeat(double beat)
    {
        return beat * 60.0 / BeatsPerMinute;
    }

    std::size_t VisibleCount(const FrameState& frame)
    {
        std::size_t count = 0;
        for(const auto& panel : frame.panels)
            count += panel.visible ? 1U : 0U;
        return count;
    }

    void AssertFiniteAndSafe(const FrameState& frame)
    {
        assert(VisibleCount(frame) <= MaximumPanels);
        for(const auto& panel : frame.panels)
        {
            if(!panel.visible)
                continue;
            assert(std::isfinite(panel.position.x));
            assert(std::isfinite(panel.position.y));
            assert(std::isfinite(panel.position.z));
            assert(std::isfinite(panel.rotation.x));
            assert(std::isfinite(panel.rotation.y));
            assert(std::isfinite(panel.rotation.z));
            assert(std::isfinite(panel.videoRoll));
            assert(panel.scale.x > 0.0f && panel.scale.x <= 3.0f);
            assert(panel.scale.y > 0.0f && panel.scale.y <= 3.0f);
            assert(panel.opacity >= 0.0f && panel.opacity <= 1.0f);
            assert(std::abs(panel.position.x) <= 140.0f);
            assert(panel.position.y >= -30.0f && panel.position.y <= 100.0f);
            assert(panel.position.z >= -50.0f && panel.position.z <= 140.0f);
        }
    }
}

int main()
{
    using namespace BigScreen::UpDownShowcase;

    assert(MatchesTarget(
        "Up & Down", "Marnik", "11cf8 (Up & Down - The Good Boi)",
        "Lawless", 4));
    assert(!MatchesTarget(
        "Up & Down", "Marnik", "11cf8 (Up & Down - The Good Boi)",
        "Standard", 4));
    assert(!MatchesTarget(
        "Up & Down", "Marnik", "2c2f4 (Up & Down - SuperMemer417)",
        "Lawless", 4));
    assert(!MatchesTarget(
        "Up & Down", "Marnik", "11cf8 (Up & Down - The Good Boi)",
        "Lawless", 3));

    assert(VisibleCount(Sample(SecondsAtBeat(0.0))) == 1);
    assert(std::abs(Sample(SecondsAtBeat(0.0)).panels[0].rotation.z - 180.0f) < 0.01f);
    assert(VisibleCount(Sample(SecondsAtBeat(45.5))) == 4);
    assert(VisibleCount(Sample(SecondsAtBeat(89.0))) == 4);
    assert(VisibleCount(Sample(SecondsAtBeat(250.0))) == 7);

    // Direct seeking into the centerpiece must produce its complete state;
    // sampling earlier cues is never required to create all eight panels.
    const auto vortex = Sample(SecondsAtBeat(263.0));
    assert(vortex.active);
    assert(VisibleCount(vortex) == 8);
    const auto vortexAfterSeek = Sample(SecondsAtBeat(271.25));
    assert(VisibleCount(vortexAfterSeek) == 8);

    // The demo relinquishes all surfaces at its explicit end boundary.
    assert(Sample(SecondsAtBeat(370.999)).active);
    assert(!Sample(SecondsAtBeat(371.0)).active);
    assert(!Sample(-0.25).active);

    for(double beat = 0.0; beat < 371.0; beat += 0.0625)
        AssertFiniteAndSafe(Sample(SecondsAtBeat(beat)));

    std::cout << "Up & Down showcase timeline tests passed\n";
    return 0;
}
