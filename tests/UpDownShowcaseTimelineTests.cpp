// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
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
            assert(panel.fracture.pattern.pieceCount <=
                BigScreen::CoreLogic::MaximumFracturePieces);
            assert(panel.fracture.impactCount <= panel.fracture.impacts.size());
            assert(panel.fracture.shardTransformCount <=
                panel.fracture.shardTransforms.size());
            assert(std::abs(panel.position.x) <= 140.0f);
            assert(panel.position.y >= -30.0f && panel.position.y <= 100.0f);
            assert(panel.position.z >= -65.0f && panel.position.z <= 140.0f);
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
    // eight-lane carousel without requiring earlier timeline samples.
    assert(VisibleCount(Sample(SecondsAtBeat(180.0))) == 4);
    assert(VisibleCount(Sample(SecondsAtBeat(195.0))) == 8);
    // The four quadrant panes now remain readable for a complete five-second
    // flyby. They reach the player plane after four seconds and spend the final
    // second traveling behind the platform before disappearing.
    constexpr double FlyStartBeat = 101.15;
    constexpr double FlyPlayerBeat =
        FlyStartBeat + 4.0 * BeatsPerMinute / 60.0;
    constexpr double FlyEndBeat =
        FlyPlayerBeat + 1.0 * BeatsPerMinute / 60.0;
    const auto earlyFlyBy = Sample(SecondsAtBeat(102.7));
    assert(VisibleCount(earlyFlyBy) == 6);
    assert(std::abs(earlyFlyBy.panels[0].position.x) < 30.0f);
    assert(earlyFlyBy.panels[0].position.z > 20.0f);
    const auto atPlayer = Sample(SecondsAtBeat(FlyPlayerBeat));
    assert(VisibleCount(atPlayer) == 6);
    for(std::size_t panel = 0; panel < 4; ++panel)
        assert(std::abs(atPlayer.panels[panel].position.z) < 0.01f);
    const auto behindPlayer = Sample(SecondsAtBeat(
        FlyPlayerBeat + 0.5 * BeatsPerMinute / 60.0));
    for(std::size_t panel = 0; panel < 4; ++panel)
        assert(behindPlayer.panels[panel].position.z < -10.0f);
    assert(VisibleCount(Sample(SecondsAtBeat(FlyEndBeat))) == 2);
    const auto massivePunchline = Sample(SecondsAtBeat(118.0));
    assert(VisibleCount(massivePunchline) == 1);
    assert(massivePunchline.panels[0].geometry == Geometry::Wide);
    assert(massivePunchline.panels[0].scale.x > 2.7f);
    assert(VisibleCount(Sample(SecondsAtBeat(134.0))) == 5);
    assert(VisibleCount(Sample(SecondsAtBeat(140.0))) == 6);
    const auto corkscrewTunnel = Sample(SecondsAtBeat(250.0));
    assert(VisibleCount(corkscrewTunnel) == 7);
    // The nearest tunnel panel must orbit overhead at a reduced size so the
    // deeper six-screen corkscrew remains visible through the entrance.
    assert(corkscrewTunnel.panels[0].position.y > 30.0f);
    assert(corkscrewTunnel.panels[0].scale.x < 1.0f);
    // Sample immediately before the authored 316.7-beat shatter window ends.
    // Sampling at beat 317 contradicted the timeline boundary below and only
    // appeared to pass in MSVC Release builds because assert() is compiled out.
    const auto openTunnel = Sample(SecondsAtBeat(316.69));
    assert(VisibleCount(openTunnel) == 12);
    // The falling fracture occupies slot zero while the new formation shifts
    // up one slot. Its nearest center screen remains overhead and substantially
    // smaller, preserving an open sightline to the far terminal panel.
    assert(openTunnel.panels[0].fracture.phase ==
        BigScreen::CoreLogic::FracturePhase::Shattered);
    assert(openTunnel.panels[1].position.y > 30.0f);
    assert(openTunnel.panels[1].scale.x < 0.75f);
    assert(openTunnel.panels[8].position.z > 80.0f);

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
    assert(CenterRingVisible(SecondsAtBeat(222.999)));
    assert(!CenterRingVisible(SecondsAtBeat(223.0)));
    assert(!CenterRingVisible(SecondsAtBeat(246.999)));
    assert(!CenterRingVisible(SecondsAtBeat(247.0)));
    assert(!CenterRingVisible(SecondsAtBeat(255.0)));
    assert(CenterRingVisible(SecondsAtBeat(263.0)));
    assert(CenterRingVisible(SecondsAtBeat(278.49)));
    assert(!CenterRingVisible(SecondsAtBeat(278.5)));
    assert(CenterRingVisible(SecondsAtBeat(279.0)));
    assert(CenterRingVisible(SecondsAtBeat(342.7)));
    assert(CenterRingVisible(-1.0));
    assert(SidePillarsVisible(SecondsAtBeat(167.999)));
    assert(!SidePillarsVisible(SecondsAtBeat(168.0)));
    assert(!SidePillarsVisible(SecondsAtBeat(200.0)));
    assert(SidePillarsVisible(SecondsAtBeat(219.0)));
    assert(SidePillarsVisible(-1.0));
    assert(BackgroundEnvironmentVisible(SecondsAtBeat(167.999)));
    assert(!BackgroundEnvironmentVisible(SecondsAtBeat(168.0)));
    assert(!BackgroundEnvironmentVisible(SecondsAtBeat(200.0)));
    assert(BackgroundEnvironmentVisible(SecondsAtBeat(219.0)));
    assert(BackgroundEnvironmentVisible(SecondsAtBeat(246.999)));
    assert(!BackgroundEnvironmentVisible(SecondsAtBeat(247.0)));
    assert(!BackgroundEnvironmentVisible(SecondsAtBeat(255.0)));
    assert(BackgroundEnvironmentVisible(SecondsAtBeat(263.0)));

    const auto freeShape = Sample(55.0);
    assert(freeShape.panels[0].deformation.enabled);
    assert(std::abs(freeShape.panels[0]
        .deformation.cornerWarp.corners[2].z) > 0.1f);
    assert(!Sample(58.01).panels[0].deformation.enabled);
    const auto dramaticFlag = Sample(SecondsAtBeat(230.0));
    assert(dramaticFlag.panels[0].deformation.wave.enabled);
    assert(dramaticFlag.panels[0].deformation.wave.depth > 4.0f);
    assert(dramaticFlag.panels[0].deformation.wave.depth < 6.0f);
    assert(dramaticFlag.panels[0].deformation.wave.clock ==
        BigScreen::CoreLogic::DeformationClock::SongTime);
    assert(std::abs(dramaticFlag.panels[0].rotation.y) < 0.01f);
    assert(std::abs(dramaticFlag.panels[0].rotation.z) < 0.01f);

    // Unrelated showcase sections no longer receive generic/random glass
    // demonstrations. Damage is reserved for the authored 2:03 impact run.
    assert(!Sample(SecondsAtBeat(145.0)).panels[0].fracture.enabled);
    const auto crackedWave = Sample(SecondsAtBeat(226.0));
    assert(!crackedWave.panels[0].fracture.enabled);
    assert(crackedWave.panels[0].deformation.enabled);
    assert(crackedWave.panels[0].deformation.wave.enabled);

    const auto firstImpact = Sample(SecondsAtBeat(284.0));
    assert(firstImpact.panels[0].fracture.phase ==
        BigScreen::CoreLogic::FracturePhase::CrackOnly);
    assert(firstImpact.panels[0].fracture.revealedGroupCount == 1);
    const auto bridgeCracks = Sample(SecondsAtBeat(294.0));
    assert(bridgeCracks.panels[0].fracture.revealedGroupCount == 7);
    const auto lastLiveImpact = Sample(SecondsAtBeat(304.0));
    assert(lastLiveImpact.panels[0].fracture.revealedGroupCount == 16);
    assert(lastLiveImpact.panels[0].fracture.pattern.pieceCount == 200);
    // The break now begins about three seconds before the replacement screens.
    // It enters with visible separation, preventing a frozen but apparently
    // intact picture from reading as a paused video before the glass moves.
    const auto shatterStart = Sample(SecondsAtBeat(304.2));
    assert(shatterStart.panels[0].fracture.phase ==
        BigScreen::CoreLogic::FracturePhase::Shattered);
    assert(shatterStart.panels[0].fracture.freezeOnShatter);
    assert(shatterStart.panels[0].fracture.separation >= 0.139f);
    assert(shatterStart.panels[0].fracture.separation <= 0.141f);
    const auto isolatedShatter = Sample(SecondsAtBeat(306.0));
    assert(isolatedShatter.panels[0].fracture.phase ==
        BigScreen::CoreLogic::FracturePhase::Shattered);
    assert(!isolatedShatter.panels[1].visible);
    const auto frozenFinale = Sample(SecondsAtBeat(312.0));
    assert(frozenFinale.panels[0].fracture.phase ==
        BigScreen::CoreLogic::FracturePhase::Shattered);
    assert(frozenFinale.panels[0].fracture.freezeOnShatter);
    assert(frozenFinale.panels[0].fracture.gravityDistance > 100.0f);
    assert(frozenFinale.panels[0].fracture.forwardScatterDistance > 40.0f);
    assert(frozenFinale.panels[1].visible);
    // Two and a half seconds into the fall, separation is deliberately only
    // partway complete: shards are around lane height and still have a long
    // forward/downward journey remaining.
    const double laneHeightBeat =
        304.2 + 2.5 * BeatsPerMinute / 60.0;
    const auto atLaneHeight = Sample(SecondsAtBeat(laneHeightBeat));
    assert(atLaneHeight.panels[0].fracture.phase ==
        BigScreen::CoreLogic::FracturePhase::Shattered);
    assert(atLaneHeight.panels[0].fracture.separation > 0.40f);
    assert(atLaneHeight.panels[0].fracture.separation < 0.60f);
    assert(Sample(SecondsAtBeat(316.69)).panels[0].fracture.phase ==
        BigScreen::CoreLogic::FracturePhase::Shattered);

    // Lock the known-good pre-circle corkscrew presentation. These values are
    // intentionally specific: a future polish pass must not quietly turn all
    // panels inward, add a tangent quarter-turn, or remove their viewing tilt.
    const auto corkscrewEntry = Sample(SecondsAtBeat(247.0));
    assert(VisibleCount(corkscrewEntry) == 7);
    assert(corkscrewEntry.panels[0].geometry ==
        BigScreen::UpDownShowcase::Geometry::CurvedIn);
    assert(corkscrewEntry.panels[1].geometry ==
        BigScreen::UpDownShowcase::Geometry::CurvedOut);
    assert(std::abs(corkscrewEntry.panels[0].rotation.x + 7.0f) < 0.01f);
    assert(std::abs(corkscrewEntry.panels[0].rotation.y - 22.0f) < 0.01f);
    assert(std::abs(corkscrewEntry.panels[0].rotation.z) < 0.01f);

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
