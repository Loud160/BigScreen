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
    // The formerly single-screen calm section now introduces a staggered,
    // four-lane carousel without requiring earlier timeline samples.
    assert(VisibleCount(Sample(SecondsAtBeat(180.0))) == 2);
    assert(VisibleCount(Sample(SecondsAtBeat(195.0))) == 4);
    const auto corkscrewTunnel = Sample(SecondsAtBeat(250.0));
    assert(VisibleCount(corkscrewTunnel) == 7);
    // The nearest tunnel panel must orbit overhead at a reduced size so the
    // deeper six-screen corkscrew remains visible through the entrance.
    assert(corkscrewTunnel.panels[0].position.y > 30.0f);
    assert(corkscrewTunnel.panels[0].scale.x < 1.0f);
    const auto openTunnel = Sample(SecondsAtBeat(317.0));
    assert(VisibleCount(openTunnel) == 8);
    // The nearest center screen is now overhead and substantially smaller
    // than the wall panels, preserving an open sightline to panel seven at the
    // far end of the formation.
    assert(openTunnel.panels[0].position.y > 30.0f);
    assert(openTunnel.panels[0].scale.x < 0.75f);
    assert(openTunnel.panels[7].position.z > 80.0f);

    // The four quadrant springs must not collapse back into mirrored pairs.
    const auto fractured = Sample(SecondsAtBeat(94.0));
    assert(std::abs(std::abs(fractured.panels[0].position.x) -
                    std::abs(fractured.panels[1].position.x)) > 0.05f);
    assert(std::abs(fractured.panels[0].position.y -
                    fractured.panels[2].position.y) > 0.05f);

    // Center-ring visibility is deterministic and always restored outside the
    // exact authored windows, including invalid time inputs. The carousel uses
    // a continuous hide; the later singularity section uses a rhythmic strobe.
    assert(CenterRingVisible(SecondsAtBeat(167.999)));
    assert(!CenterRingVisible(SecondsAtBeat(168.0)));
    assert(!CenterRingVisible(SecondsAtBeat(200.0)));
    assert(CenterRingVisible(SecondsAtBeat(219.0)));
    assert(CenterRingVisible(SecondsAtBeat(278.49)));
    assert(!CenterRingVisible(SecondsAtBeat(278.5)));
    assert(CenterRingVisible(SecondsAtBeat(279.0)));
    assert(CenterRingVisible(SecondsAtBeat(342.7)));
    assert(CenterRingVisible(-1.0));

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
