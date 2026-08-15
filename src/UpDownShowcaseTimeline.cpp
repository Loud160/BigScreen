// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/UpDownShowcaseTimeline.hpp"

#include <algorithm>
#include <cmath>

namespace BigScreen::UpDownShowcase {
    namespace {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float BaseY = 17.0f;
        constexpr float BaseZ = 58.0f;
        constexpr double ShapeDeformStartBeat = 52.0 * BeatsPerMinute / 60.0;
        constexpr double ShapeDeformEndBeat = 58.0 * BeatsPerMinute / 60.0;
        // The quadrant flyby is intentionally specified in seconds even though
        // the timeline samples beats. Four seconds carry the panes from the
        // back wall to the player plane; one more second keeps them visible as
        // they pass behind the platform. This replaces the former 0.8-second
        // rush that looked like an abrupt disappearance, even in slow motion.
        constexpr double QuadrantFlyStartBeat = 101.15;
        constexpr double QuadrantFlyPlayerBeat =
            QuadrantFlyStartBeat + 4.0 * BeatsPerMinute / 60.0;
        constexpr double QuadrantFlyEndBeat =
            QuadrantFlyPlayerBeat + 1.0 * BeatsPerMinute / 60.0;
        // Move the complete break one additional second earlier. At 138 BPM,
        // one second is exactly 2.3 beats, so the successful 12.5-beat fall is
        // preserved while the shards now separate well before the replacement
        // formation appears at beat 311.
        constexpr double FinaleShatterStartBeat = 304.2;
        constexpr double FinaleShatterEndBeat = 316.7;
        // A shattered surface uses a frozen texture so every shard retains one
        // coherent picture. Starting at zero separation froze that picture for
        // several visually static frames before any breakup could be seen.
        // Enter with a small visible separation instead: freezing and breaking
        // now happen together, so the video never appears to pause first.
        constexpr float FinaleInitialSeparation = 0.14f;

        float Clamp01(double value)
        {
            return static_cast<float>(std::clamp(value, 0.0, 1.0));
        }

        float Lerp(float from, float to, float amount)
        {
            return from + (to - from) * amount;
        }

        float Smooth(float amount)
        {
            amount = std::clamp(amount, 0.0f, 1.0f);
            return amount * amount * (3.0f - 2.0f * amount);
        }

        float EaseOutBack(float amount)
        {
            amount = std::clamp(amount, 0.0f, 1.0f) - 1.0f;
            constexpr float Overshoot = 1.70158f;
            return 1.0f + amount * amount *
                ((Overshoot + 1.0f) * amount + Overshoot);
        }

        float Segment(double beat, double start, double end)
        {
            return Clamp01((beat - start) / (end - start));
        }

        float Pulse(double beat, double cyclesPerBeat = 1.0)
        {
            return 0.5f + 0.5f * std::sin(
                static_cast<float>(beat * cyclesPerBeat * 2.0 * Pi));
        }

        void Put(
            FrameState& frame,
            std::size_t index,
            Float3 position,
            Float3 rotation,
            Float3 scale,
            Geometry geometry = Geometry::Wide,
            float opacity = 1.0f,
            float videoRoll = 0.0f)
        {
            if(index >= frame.panels.size())
                return;
            frame.active = true;
            frame.panels[index] = {
                true, position, rotation, scale,
                std::clamp(opacity, 0.0f, 1.0f),
                videoRoll, geometry};
        }

        void PutSingle(
            FrameState& frame,
            Float3 position = {0.0f, BaseY, BaseZ},
            Float3 rotation = {-8.0f, 0.0f, 0.0f},
            Float3 scale = {1.45f, 1.45f, 1.0f},
            Geometry geometry = Geometry::Wide,
            float opacity = 1.0f,
            float videoRoll = 0.0f)
        {
            Put(frame, 0, position, rotation, scale, geometry, opacity, videoRoll);
        }

        float HalfBeatBounce(double beat, double start, float amplitude)
        {
            const double step = std::floor((beat - start) * 2.0);
            const float direction = (static_cast<long long>(step) & 1LL) == 0
                ? 1.0f : -1.0f;
            const float phase = static_cast<float>(
                (beat - start) * 2.0 - step);
            return direction * amplitude * EaseOutBack(phase);
        }

        float FractureSpring(double beat, std::size_t index)
        {
            // Each quadrant is tethered by a different imaginary spring. The
            // amplitude grows with separation, while mismatched frequencies
            // prevent the four corners from collapsing into one mechanical,
            // mirrored motion. A negative value is an inward rubber-band snap.
            constexpr std::array<float, 4> Frequencies{5.35f, 6.10f, 4.65f, 7.05f};
            constexpr std::array<float, 4> PhaseOffsets{0.15f, 1.70f, 3.05f, 4.25f};
            const float phase = static_cast<float>(beat - 89.0) * Frequencies[index] +
                PhaseOffsets[index];
            return std::sin(phase) + 0.42f * std::sin(phase * 1.73f + index);
        }

        void ApplyFreeShape(PanelState& panel, double beat, float phaseOffset = 0.0f)
        {
            if(beat < ShapeDeformStartBeat || beat >= ShapeDeformEndBeat)
                return;
            const float progress = Segment(
                beat, ShapeDeformStartBeat, ShapeDeformEndBeat);
            const float envelope = std::sin(progress * Pi);
            const float phase = progress * Pi * 5.0f + phaseOffset;
            auto& deformation = panel.deformation;
            deformation.enabled = true;
            deformation.fillMode = progress < 0.5f
                ? CoreLogic::DeformationFillMode::StretchToFill
                : CoreLogic::DeformationFillMode::AutoZoomCover;
            // Four unrelated corner trajectories make the surface fold and
            // shear rather than merely scaling a rectangular mesh.
            deformation.cornerWarp.corners = {{
                {-4.8f * envelope * std::sin(phase),
                 -3.1f * envelope * std::cos(phase * 0.73f),
                  5.8f * envelope * std::sin(phase * 0.61f)},
                {-2.7f * envelope * std::cos(phase * 0.89f),
                  4.6f * envelope * std::sin(phase * 0.68f),
                 -6.5f * envelope * std::cos(phase * 0.57f)},
                { 4.2f * envelope * std::sin(phase * 0.81f + 0.7f),
                  3.7f * envelope * std::cos(phase * 0.64f + 0.4f),
                  7.2f * envelope * std::sin(phase * 0.52f + 1.0f)},
                { 5.1f * envelope * std::cos(phase * 0.76f + 0.2f),
                 -4.0f * envelope * std::sin(phase * 0.91f + 0.5f),
                 -5.4f * envelope * std::cos(phase * 0.66f + 0.8f)}}};
        }

        void ApplyDramaticFlagWave(PanelState& panel)
        {
            auto& deformation = panel.deformation;
            deformation.enabled = true;
            deformation.fillMode = CoreLogic::DeformationFillMode::AutoZoomCover;
            deformation.wave.enabled = true;
            deformation.wave.cyclesAcross = 1.15f;
            // One complete wave every four beats keeps the motion deep and
            // deliberate instead of looking like a rapidly vibrating cloth.
            deformation.wave.speedCyclesPerSecond =
                static_cast<float>(BeatsPerMinute / 240.0);
            // The earlier value overwhelmed the image, especially while the
            // canvas was also rocking. This remains unmistakably three-
            // dimensional without folding the picture into itself.
            deformation.wave.depth = 5.1f;
            deformation.wave.ripple = 0.82f;
            deformation.wave.anchorRamp = 0.72f;
            deformation.wave.anchoredEdge = CoreLogic::WaveAnchorEdge::Left;
            deformation.wave.phaseOffset = 0.25f;
            deformation.wave.clock = CoreLogic::DeformationClock::SongTime;
        }

        // One deterministic impact is assigned to every low point in the two
        // bounce phrases from about 2:03 until the 2:15 break. The positions
        // deliberately alternate edges, corners, and center regions so the
        // accumulating web reads as repeated structural damage rather than a
        // single crack graphic becoming brighter.
        constexpr std::array<CoreLogic::FracturePoint, 18> ImpactSequence{{
            {0.16f, 0.77f}, {0.83f, 0.70f}, {0.36f, 0.21f},
            {0.67f, 0.37f}, {0.48f, 0.84f}, {0.91f, 0.28f},
            {0.10f, 0.42f}, {0.57f, 0.58f}, {0.27f, 0.62f},
            {0.76f, 0.88f}, {0.43f, 0.39f}, {0.70f, 0.16f},
            {0.20f, 0.91f}, {0.88f, 0.51f}, {0.33f, 0.73f},
             {0.61f, 0.29f}, {0.51f, 0.68f}, {0.79f, 0.43f}}};

        template<std::size_t ImpactCount>
        void ConfigureGlass(
            PanelState& panel,
            std::size_t preset,
            std::size_t pieces,
            const std::array<CoreLogic::FracturePoint, ImpactCount>& impacts,
            CoreLogic::FracturePhase phase,
            std::size_t revealedGroups,
            bool freeze,
            float separation)
        {
            auto& glass = panel.fracture;
            glass.enabled = true;
            glass.phase = phase;
            glass.pattern.seed = CoreLogic::CuratedFractureSeeds[
                preset % CoreLogic::CuratedFractureSeeds.size()].second;
            glass.pattern.pieceCount = pieces;
            glass.pattern.impactPoint = impacts.back();
            glass.pattern.spokeCount = ImpactCount == 4 ? 13 : 10;
            glass.pattern.ringCount = 3;
            glass.pattern.jitter = 0.22f;
            glass.impactCount = ImpactCount;
            for(std::size_t index = 0; index < ImpactCount; ++index)
                glass.impacts[index] = impacts[index];
            glass.revealedGroupCount = std::min(revealedGroups, ImpactCount);
            glass.freezeOnShatter = freeze;
            glass.separation = std::clamp(separation, 0.0f, 1.0f);
        }

        std::size_t RevealedImpactCount(double beat)
        {
            // HalfBeatBounce reaches its lower endpoint at each whole beat.
            // The first phrase lands at beats 284..290 and the second at
            // 296..306. The static bridge between them keeps all existing
            // cracks but does not invent impacts while the pane is stationary.
            std::size_t revealed = 0;
            if(beat >= 284.0)
                revealed = std::min<std::size_t>(
                    7, static_cast<std::size_t>(std::floor(beat - 284.0)) + 1);
            if(beat >= 296.0)
                revealed += std::min<std::size_t>(
                    11, static_cast<std::size_t>(std::floor(beat - 296.0)) + 1);
            return std::min(revealed, ImpactSequence.size());
        }

        FrameState ApplyGlassCues(FrameState frame, double beat)
        {
            if(!frame.active)
                return frame;

            // This is the showcase's only glass cue. The same foreground panel
            // persists throughout both bounce phrases, so every downbeat adds
            // damage to one physical pane. The complete 200-piece partition is
            // prepared once; changing revealedGroupCount exposes more of its
            // seams without reallocating or regenerating geometry mid-song.
            if(beat >= 282.75 && beat < FinaleShatterStartBeat &&
               frame.panels[0].visible)
            {
                const auto reveals = RevealedImpactCount(beat);
                ConfigureGlass(
                    frame.panels[0], 11, 200, ImpactSequence,
                    reveals == 0
                        ? CoreLogic::FracturePhase::Prepared
                        : CoreLogic::FracturePhase::CrackOnly,
                    reveals, true, 0.0f);
                frame.panels[0].fracture.crackOpacity = 0.86f;
            }

            // Shortly before the following formation, freeze the currently
            // displayed frame on every shard and let the pane fall forward
            // through a deep volume. The initial non-zero separation makes the
            // breakup visible on the same frame that captures the snapshot;
            // there is no static frozen-image interval before the shatter.
            // Keep the fragments alive into the following formation: a longer
            // 12.5-beat curve takes roughly 2-3 seconds to reach lane height,
            // then continues well below and behind the player. Once the next
            // formation starts at beat 311, shift its surfaces up one slot;
            // panel zero remains the falling glass without another decoder.
            if(beat >= FinaleShatterStartBeat && beat < FinaleShatterEndBeat)
            {
                if(beat >= 311.0)
                {
                    for(std::size_t index = frame.panels.size() - 1;
                        index > 0; --index)
                        frame.panels[index] = frame.panels[index - 1];
                }

                PanelState broken{};
                broken.visible = true;
                broken.position = {0.0f, 19.0f, 51.0f};
                broken.rotation = {-6.0f, 0.0f, 0.0f};
                broken.scale = {1.85f, 1.85f, 1.0f};
                broken.opacity = 1.0f -
                    0.60f * Segment(
                        beat, FinaleShatterEndBeat - 2.5,
                        FinaleShatterEndBeat);
                broken.geometry = Geometry::Wide;
                ConfigureGlass(
                    broken, 11, 200, ImpactSequence,
                    CoreLogic::FracturePhase::Shattered,
                    ImpactSequence.size(), true,
                    Lerp(
                        FinaleInitialSeparation,
                        1.0f,
                        Smooth(Segment(
                            beat, FinaleShatterStartBeat,
                            FinaleShatterEndBeat))));
                broken.fracture.outwardDistance = 15.0f;
                broken.fracture.forwardScatterDistance = 42.0f;
                broken.fracture.gravityDistance = 110.0f;
                broken.fracture.tumbleDegrees = 560.0f;
                broken.fracture.stagger = 0.16f;
                broken.fracture.crackOpacity = 0.9f;
                frame.panels[0] = broken;
            }
            return frame;
        }
    }

    bool MatchesTarget(
        std::string_view songName,
        std::string_view songArtist,
        std::string_view levelDirectoryName,
        std::string_view characteristic,
        int difficulty)
    {
#if defined(BIGSCREEN_UP_DOWN_SHOWCASE)
        return songName == "Up & Down" &&
               songArtist == "Marnik" &&
               levelDirectoryName.starts_with("11cf8 (") &&
               characteristic == "Lawless" &&
               difficulty == 4;
#else
        (void)songName;
        (void)songArtist;
        (void)levelDirectoryName;
        (void)characteristic;
        (void)difficulty;
        return false;
#endif
    }

    FrameState SampleBase(double songTimeSeconds)
    {
        FrameState frame;
        if(!std::isfinite(songTimeSeconds) || songTimeSeconds < 0.0)
            return frame;

        const double beat = songTimeSeconds * BeatsPerMinute / 60.0;
        if(beat >= 371.0)
            return frame;

        // 0-41: mirror the Lawless chart's inverted-player opening. The small
        // screen is intentionally dwarfed by what follows so the first arena-
        // sized expansion has real visual impact.
        if(beat < 31.0)
        {
            const float drift = std::sin(static_cast<float>(beat * 0.22)) * 2.0f;
            PutSingle(frame,
                {drift, 34.0f, 47.0f},
                {-12.0f, drift * 0.7f, 180.0f},
                {0.58f, 0.58f, 1.0f},
                Geometry::CurvedIn,
                0.72f);
            return frame;
        }
        if(beat < 41.0)
        {
            const float t = Smooth(Segment(beat, 31.0, 41.0));
            float roll = beat < 36.0
                ? Lerp(180.0f, -90.0f, Smooth(Segment(beat, 31.0, 36.0)))
                : Lerp(-90.0f, 0.0f, Smooth(Segment(beat, 36.0, 41.0)));
            PutSingle(frame,
                {0.0f, Lerp(34.0f, BaseY, t), Lerp(47.0f, BaseZ, t)},
                {Lerp(-12.0f, -8.0f, t), 0.0f, roll},
                {Lerp(0.58f, 1.55f, t), Lerp(0.58f, 1.55f, t), 1.0f},
                t < 0.72f ? Geometry::CurvedIn : Geometry::Wide);
            return frame;
        }
        if(beat < 45.0)
        {
            PutSingle(frame, {0.0f, BaseY, BaseZ}, {-8.0f, 0.0f, 0.0f},
                {1.65f, 1.65f, 1.0f});
            return frame;
        }

        // 45-70: four enormous cropped memories orbit at mismatched depths.
        if(beat < 70.0)
        {
            const float orbit = static_cast<float>((beat - 45.0) * 0.105);
            for(std::size_t i = 0; i < 4; ++i)
            {
                const float side = (i & 1U) == 0 ? -1.0f : 1.0f;
                const float row = i < 2 ? -1.0f : 1.0f;
                const float phase = orbit + static_cast<float>(i) * Pi * 0.5f;
                const Geometry geometry = static_cast<Geometry>(
                    static_cast<int>(Geometry::QuadrantTopLeft) + static_cast<int>(i));
                const float stagger = i >= 2
                    ? Segment(beat, 45.25, 46.25)
                    : Segment(beat, 45.0, 46.0);
                Put(frame, i,
                    {side * (23.0f + std::sin(phase) * 7.0f),
                     BaseY + row * 13.0f + std::cos(phase * 1.3f) * 4.0f,
                     BaseZ + std::cos(phase) * 14.0f + row * 3.0f},
                    {-8.0f + row * 5.0f,
                     side * (12.0f + std::sin(phase) * 9.0f),
                     -static_cast<float>(beat - 45.0) * side * 7.0f + i * 11.0f},
                    {Lerp(0.05f, 0.88f, EaseOutBack(stagger)),
                     Lerp(0.05f, 0.88f, EaseOutBack(stagger)), 1.0f},
                    geometry,
                    0.96f,
                    static_cast<float>(beat - 45.0) * side * 7.0f - i * 11.0f);
            }
            return frame;
        }

        // 70-78: the constellation implodes and reappears as an arena-width
        // gateway. A short scale shear supplies the requested glitch without
        // a full-field opacity flash.
        if(beat < 78.0)
        {
            const float t = Smooth(Segment(beat, 70.0, 72.0));
            const float glitch = beat < 71.0
                ? ((static_cast<int>((beat - 70.0) * 12.0) & 1) ? 0.80f : 1.18f)
                : 1.0f;
            PutSingle(frame,
                {0.0f, BaseY + std::sin(static_cast<float>(beat * Pi)) * 2.0f,
                 Lerp(48.0f, 53.0f, t)},
                {-6.0f, 0.0f, (1.0f - t) * 24.0f},
                {Lerp(0.35f, 2.05f, t) * glitch,
                 Lerp(2.0f, 1.55f, t) / glitch, 1.0f},
                Geometry::UltraWide);
            return frame;
        }
        if(beat < 79.0)
        {
            const float t = Smooth(Segment(beat, 78.0, 78.8));
            PutSingle(frame, {0.0f, BaseY, 53.0f}, {-6.0f, 0.0f, 360.0f * t},
                {2.05f, 1.55f, 1.0f}, Geometry::UltraWide);
            return frame;
        }
        if(beat < 86.5)
        {
            PutSingle(frame,
                {0.0f, BaseY + HalfBeatBounce(beat, 79.0, 4.4f), 55.0f},
                {-7.0f, 0.0f, 0.0f}, {1.78f, 1.78f, 1.0f},
                Geometry::Wide);
            return frame;
        }
        if(beat < 88.0)
        {
            const float t = Smooth(Segment(beat, 86.5, 88.0));
            PutSingle(frame,
                {Lerp(0.0f, -12.0f, t), Lerp(BaseY, 9.0f, t), 56.0f},
                {-7.0f, 0.0f, Lerp(0.0f, 180.0f, t)},
                {1.65f, 1.65f, 1.0f});
            return frame;
        }

        // 88-approximately 49 seconds: four actual surfaces each reveal one
        // source quadrant and
        // physically fracture the image. Rather than softly rebuilding the
        // tiled picture before the next cue, the last half of the phrase sends
        // all four spinning past the player on wide, lane-safe trajectories.
        if(beat < QuadrantFlyEndBeat)
        {
            // Keep the independent rubber-band movement alive until the final
            // hand-off. Only then turn each pane edge-on toward the lane and
            // drive it past the player along Z; a large lateral target made
            // the previous version look as though the panes simply slid off
            // the left and right edges of the back wall.
            const double fractureBeat = std::min(beat, QuadrantFlyStartBeat);
            const float entry = EaseOutBack(Segment(fractureBeat, 88.0, 89.0));
            const float fracture = Smooth(Segment(fractureBeat, 89.0, 91.0));
            const float toPlayer = Smooth(Segment(
                beat, QuadrantFlyStartBeat, QuadrantFlyPlayerBeat));
            const float pastPlayer = Smooth(Segment(
                beat, QuadrantFlyPlayerBeat, QuadrantFlyEndBeat));
            const bool behindPlayer = beat >= QuadrantFlyPlayerBeat;
            const float travel = behindPlayer
                ? Lerp(0.8f, 1.0f, pastPlayer)
                : 0.8f * toPlayer;
            const float turn = Smooth(Segment(
                beat, QuadrantFlyStartBeat, QuadrantFlyStartBeat + 1.6));
            for(std::size_t i = 0; i < 4; ++i)
            {
                const float side = (i & 1U) == 0 ? -1.0f : 1.0f;
                const float row = i < 2 ? 1.0f : -1.0f;
                const Geometry geometry = static_cast<Geometry>(
                    static_cast<int>(Geometry::QuadrantTopLeft) + static_cast<int>(i));
                const float spring = FractureSpring(fractureBeat, i) * fracture;
                const float springReach = (2.8f + static_cast<float>(i) * 0.55f) * spring;
                const float verticalSpring = FractureSpring(
                    fractureBeat + 0.37, (i + 1) % 4) *
                    fracture * (1.4f + static_cast<float>(i) * 0.25f);
                const float startX = side * Lerp(
                    1.0f, 19.0f + 9.0f * fracture + springReach, entry);
                const float startY = BaseY + row * Lerp(
                    1.0f, 10.5f + 4.5f * fracture + verticalSpring, entry);
                const float startZ = BaseZ +
                    fracture * (i == 0 || i == 3 ? -12.0f : 10.0f) +
                    spring * (i < 2 ? 2.4f : -2.0f);
                const float startPitch =
                    -7.0f + row * fracture * (8.0f + spring * 4.0f);
                const float startYaw =
                    side * fracture * (28.0f + spring * 9.0f);
                const float startRoll =
                    side * row * fracture * (16.0f + spring * 11.0f);

                // Two panes travel down each side of the block lanes. Their
                // fronts face inward at one another while distinct X-axis
                // spins preserve the chaotic departure. They disappear only
                // after passing well behind the player platform.
                constexpr std::array<float, 4> LaneX{11.5f, 14.0f, 13.0f, 15.5f};
                constexpr std::array<float, 4> PassY{29.0f, 23.0f, 9.0f, 2.0f};
                constexpr std::array<float, 4> PassZ{-38.0f, -50.0f, -43.0f, -57.0f};
                constexpr std::array<float, 4> SpinDegrees{540.0f, -690.0f, -610.0f, 760.0f};
                const float laneArc = std::sin(travel * Pi) *
                    (1.5f + static_cast<float>(i) * 0.35f);
                const float inwardYaw = -side * (88.0f + i * 1.2f);
                Put(frame, i,
                    {Lerp(startX, side * LaneX[i], travel) + side * laneArc,
                     Lerp(startY, PassY[i], travel) +
                         std::sin(travel * Pi * (1.0f + i * 0.18f)) * row * 2.2f,
                     behindPlayer
                         ? Lerp(0.0f, PassZ[i], pastPlayer)
                         : Lerp(startZ, 0.0f, toPlayer)},
                    {Lerp(startPitch, SpinDegrees[i], travel),
                     Lerp(startYaw, inwardYaw, turn),
                     Lerp(startRoll, side * row * 8.0f, turn)},
                    {Lerp(0.92f, 1.10f + static_cast<float>(i) * 0.05f, travel),
                     Lerp(0.92f, 1.10f + static_cast<float>(i) * 0.05f, travel),
                     1.0f},
                    geometry,
                    1.0f - Segment(
                        beat, QuadrantFlyEndBeat - 0.75,
                        QuadrantFlyEndBeat));
            }

            // Bring the following pair up before the rubber-band panels have
            // passed the player. All six surfaces coexist through the readable
            // five-second flyby, turning the transition into a sustained hand-
            // off rather than a soft cut to an unrelated formation.
            if(beat >= 101.45)
            {
                const float sideEntry = EaseOutBack(Segment(beat, 101.45, 102.35));
                for(std::size_t i = 0; i < 2; ++i)
                {
                    const float side = i == 0 ? -1.0f : 1.0f;
                    const float wave = std::sin(static_cast<float>((beat - 102.0) * Pi));
                    Put(frame, 4 + i,
                        {side * 23.0f, BaseY + side * wave * 9.0f,
                         55.0f + side * wave * 4.0f},
                        {-8.0f, side * 10.0f, side * wave * 9.0f},
                        {Lerp(0.05f, 1.18f, sideEntry),
                         Lerp(0.05f, 1.55f, sideEntry), 1.0f},
                        Geometry::Tall, 1.0f, -side * wave * 9.0f);
                }
            }
            return frame;
        }

        if(beat < 116.0)
        {
            for(std::size_t i = 0; i < 2; ++i)
            {
                const float side = i == 0 ? -1.0f : 1.0f;
                const float wave = std::sin(static_cast<float>((beat - 102.0) * Pi));
                Put(frame, 4 + i,
                    {side * 23.0f, BaseY + side * wave * 9.0f,
                     55.0f + side * wave * 4.0f},
                    {-8.0f, side * 10.0f, side * wave * 9.0f},
                    {1.18f, 1.55f, 1.0f}, Geometry::Tall, 1.0f,
                    -side * wave * 9.0f);
            }
            return frame;
        }
        if(beat < 121.5)
        {
            // The visual punchline near 0:51 needs arena scale. Keep the
            // source at its ordinary wide aspect/zoom and enlarge only the
            // physical flat canvas, with a restrained beat pulse so the image
            // itself is never cropped differently.
            const float entrance = EaseOutBack(Segment(beat, 116.0, 116.65));
            const float pulse = Pulse(beat, 0.5);
            const float scale = std::min(
                2.95f,
                Lerp(0.12f, 2.78f + pulse * 0.12f, entrance));
            PutSingle(frame,
                {0.0f, 18.5f, 49.0f},
                {-5.0f, 0.0f, 0.0f},
                {scale, scale, 1.0f},
                Geometry::Wide);
            ApplyFreeShape(frame.panels[0], beat);
            return frame;
        }
        if(beat < 122.0)
        {
            const float t = Smooth(Segment(beat, 121.5, 122.0));
            PutSingle(frame,
                {0.0f, Lerp(18.5f, BaseY, t), Lerp(49.0f, 55.0f, t)},
                {Lerp(-5.0f, -7.0f, t), 0.0f, 0.0f},
                {Lerp(2.84f, 1.35f, t), Lerp(2.84f, 1.95f, t), 1.0f},
                t < 0.55f ? Geometry::Wide : Geometry::Tall);
            ApplyFreeShape(frame.panels[0], beat);
            return frame;
        }
        if(beat < 136.0)
        {
            const bool tall = (static_cast<int>((beat - 122.0) / 2.0) & 1) == 0;
            float opacity = 1.0f;
            if(beat >= 134.25)
                opacity = Lerp(0.12f, 1.0f, Segment(beat, 134.25, 136.0));
            PutSingle(frame, {0.0f, BaseY, 55.0f}, {-7.0f, 0.0f, 0.0f},
                tall ? Float3{1.35f, 1.95f, 1.0f} : Float3{2.2f, 1.25f, 1.0f},
                tall ? Geometry::Tall : Geometry::UltraWide, opacity);
            ApplyFreeShape(frame.panels[0], beat);
            if(beat >= 132.0)
            {
                for(std::size_t echo = 0; echo < 4; ++echo)
                {
                    const std::size_t index = 1 + echo;
                    const float side = (echo & 1U) == 0 ? -1.0f : 1.0f;
                    const float row = echo < 2 ? 1.0f : -1.0f;
                    const float entry = EaseOutBack(Segment(
                        beat, 132.0 + echo * 0.12, 133.1 + echo * 0.12));
                    const float flap = std::sin(static_cast<float>(
                        (beat - 132.0) * 1.7 + echo * 0.82));
                    const float targetScale = tall ? 0.88f : 1.02f;
                    Put(frame, index,
                        {side * (21.0f + (echo & 1U) * 8.0f),
                         BaseY + row * (25.0f + std::abs(flap) * 4.0f),
                         61.0f + (echo & 1U) * 8.0f},
                        {-7.0f + row * flap * 12.0f,
                         side * flap * 24.0f,
                         side * (18.0f + flap * 16.0f)},
                        {Lerp(0.04f, targetScale, entry),
                         Lerp(0.04f, targetScale, entry), 1.0f},
                        tall ? Geometry::Tall : Geometry::UltraWide,
                        opacity * 0.90f,
                        -side * (18.0f + flap * 16.0f));
                    ApplyFreeShape(frame.panels[index], beat, echo * 0.8f);
                    // One echo deliberately uses wall-clock animation while
                    // the hero surface remains song-time deterministic. This
                    // proves both clocks can coexist without coupling them.
                    if(echo == 0 && frame.panels[index].deformation.enabled)
                    {
                        auto& wave = frame.panels[index].deformation.wave;
                        wave.enabled = true;
                        wave.cyclesAcross = 1.0f;
                        wave.speedCyclesPerSecond = 0.35f;
                        wave.depth = 2.2f;
                        wave.ripple = 0.35f;
                        wave.anchorRamp = 1.0f;
                        wave.clock = CoreLogic::DeformationClock::RealTime;
                    }
                }
            }
            return frame;
        }
        if(beat < 138.0)
        {
            PutSingle(frame, {0.0f, BaseY, 54.0f}, {-7.0f, 0.0f, 0.0f},
                {1.8f, 1.8f, 1.0f}, Geometry::CurvedIn);
            for(std::size_t echo = 0; echo < 4; ++echo)
            {
                const std::size_t index = 1 + echo;
                const float side = (echo & 1U) == 0 ? -1.0f : 1.0f;
                const float row = echo < 2 ? 1.0f : -1.0f;
                const float flap = std::sin(static_cast<float>(
                    (beat - 136.0) * 1.8 + echo * 0.88));
                Put(frame, index,
                    {side * (22.0f + (echo & 1U) * 8.0f),
                     BaseY + row * (25.0f + std::abs(flap) * 4.0f),
                     59.0f + (echo & 1U) * 9.0f},
                    {-8.0f + row * flap * 13.0f,
                     side * flap * 27.0f,
                     side * (22.0f + flap * 18.0f)},
                    {0.98f, 1.08f, 1.0f},
                    (echo & 1U) == 0
                        ? Geometry::CurvedIn : Geometry::CurvedOut,
                    0.90f,
                    -side * (22.0f + flap * 18.0f));
            }
            return frame;
        }
        if(beat < 142.0)
        {
            const float t = EaseOutBack(Segment(beat, 138.0, 142.0));
            for(std::size_t i = 0; i < 2; ++i)
            {
                const float side = i == 0 ? -1.0f : 1.0f;
                Put(frame, i,
                    {side * Lerp(9.0f, 20.0f, t), Lerp(-12.0f, BaseY + 3.0f, t), 48.0f},
                    {-8.0f, side * Lerp(82.0f, 20.0f, t), side * 360.0f * t},
                    {1.25f, Lerp(0.15f, 1.65f, t), 1.0f},
                    i == 0 ? Geometry::CurvedIn : Geometry::CurvedOut,
                    1.0f, -side * 360.0f * t);
            }

            // Four staggered echoes continue the same flap above and below
            // the front pair. Their greater depth and slightly smaller scale
            // leave the original two readable while eliminating the empty
            // upper/lower arena without introducing visible gaps.
            for(std::size_t echo = 0; echo < 4; ++echo)
            {
                const std::size_t index = 2 + echo;
                const float side = (echo & 1U) == 0 ? -1.0f : 1.0f;
                const float row = echo < 2 ? 1.0f : -1.0f;
                const float echoT = EaseOutBack(Segment(
                    beat, 138.10 + echo * 0.13, 142.0));
                const float phase = static_cast<float>(echo) * 0.72f;
                const float flap = std::sin(
                    static_cast<float>((beat - 138.0) * 1.35) + phase);
                const float targetScale = 1.04f + (echo & 1U) * 0.10f;
                Put(frame, index,
                    {side * Lerp(11.0f, 27.0f + (echo & 1U) * 4.0f, echoT),
                     BaseY + row * (25.0f + std::abs(flap) * 5.0f),
                     56.0f + (echo & 1U) * 8.0f + row * 2.0f},
                    {-8.0f + row * flap * 14.0f,
                     side * Lerp(72.0f, 18.0f + echo * 4.0f, echoT),
                     side * (310.0f * echoT + echo * 38.0f) + flap * 11.0f},
                    {Lerp(0.04f, targetScale, echoT),
                     Lerp(0.04f, targetScale * 1.12f, echoT), 1.0f},
                    (echo & 1U) == 0
                        ? Geometry::CurvedIn : Geometry::CurvedOut,
                    0.90f,
                    -side * (310.0f * echoT + echo * 38.0f));
            }
            return frame;
        }
        if(beat < 143.0)
        {
            PutSingle(frame, {0.0f, BaseY + 2.0f, 43.0f}, {-6.0f, 0.0f, 0.0f},
                {2.25f, 1.9f, 1.0f}, Geometry::CurvedIn);
            return frame;
        }
        if(beat < 166.0)
        {
            const float act = static_cast<float>(beat - 143.0);
            const float spread = 14.0f + 9.0f * std::sin(act * 0.42f);
            for(std::size_t i = 0; i < 2; ++i)
            {
                const float side = i == 0 ? -1.0f : 1.0f;
                const float tumble = act * (i == 0 ? 19.0f : -23.0f);
                Put(frame, i,
                    {side * spread, BaseY + side * std::sin(act * 0.7f) * 8.0f,
                     49.0f + side * std::cos(act * 0.48f) * 9.0f},
                    {-8.0f + side * std::sin(act) * 14.0f,
                     side * (26.0f + std::cos(act * 0.4f) * 18.0f), tumble},
                    {Lerp(0.85f, 1.65f, Pulse(beat, 0.5)),
                     Lerp(1.6f, 0.75f, Pulse(beat + i, 0.5)), 1.0f},
                    i == 0 ? Geometry::CurvedIn : Geometry::CurvedOut,
                    0.96f, -tumble * 0.92f);
            }

            for(std::size_t echo = 0; echo < 4; ++echo)
            {
                const std::size_t index = 2 + echo;
                const float side = (echo & 1U) == 0 ? -1.0f : 1.0f;
                const float row = echo < 2 ? 1.0f : -1.0f;
                const float phase = act * (0.46f + echo * 0.035f) + echo * 1.17f;
                const float flap = std::sin(phase);
                const float echoScale = 0.90f + Pulse(beat + echo * 0.27, 0.5) * 0.30f;
                const float tumble = act * (side < 0.0f
                    ? 15.0f + echo * 1.2f
                    : -17.0f - echo * 1.1f);
                Put(frame, index,
                    {side * (spread + 9.0f + (echo & 1U) * 5.0f),
                     BaseY + row * (25.0f + flap * 5.5f),
                     59.0f + (echo & 1U) * 10.0f + flap * 3.0f},
                    {-8.0f + row * flap * 18.0f,
                     side * (20.0f + std::cos(phase * 0.73f) * 17.0f),
                     tumble},
                    {echoScale, echoScale * (0.93f + std::abs(flap) * 0.14f), 1.0f},
                    (echo & 1U) == 0
                        ? Geometry::CurvedIn : Geometry::CurvedOut,
                    0.88f,
                    -tumble * 0.88f);
            }
            return frame;
        }
        if(beat < 168.0)
        {
            const float t = Smooth(Segment(beat, 166.0, 168.0));
            PutSingle(frame, {0.0f, Lerp(BaseY, 15.0f, t), Lerp(49.0f, 46.0f, t)},
                {-7.0f, 0.0f, 0.0f},
                {Lerp(1.5f, 0.62f, t), Lerp(1.5f, 0.62f, t), 1.0f},
                Geometry::CurvedIn);
            return frame;
        }
        if(beat < 219.0)
        {
            // Slow eight-screen carousel. The four hero screens retain their
            // original lanes; four smaller satellites occupy high and low
            // paths at distinct depths so the transparent arena is filled
            // without any surface crossing another or the playable lanes.
            // Each screen enters on its own lane,
            // crosses at a distinct height/depth, and rotates in the opposite
            // direction from the previous screen. The separated lanes keep
            // the large curved surfaces from passing through one another.
            constexpr std::array<double, 4> Starts{168.0, 175.0, 182.0, 189.0};
            constexpr std::array<float, 4> Heights{8.5f, 21.0f, 31.0f, 14.5f};
            constexpr std::array<float, 4> Depths{34.0f, 43.0f, 37.5f, 53.0f};
            for(std::size_t i = 0; i < 4; ++i)
            {
                if(beat < Starts[i])
                    continue;
                const float direction = (i & 1U) == 0 ? 1.0f : -1.0f;
                const float t = Smooth(Segment(beat, Starts[i], 219.0));
                const float entry = Smooth(Segment(beat, Starts[i], Starts[i] + 2.0));
                const float arc = std::sin(t * Pi);
                const float slowWave = std::sin(
                    static_cast<float>((beat - Starts[i]) * 0.32 + i * 1.4));
                float nod = 0.0f;
                if(i == 0 && beat >= 187.0 && beat < 187.125)
                    nod = -15.0f;
                else if(i == 0 && beat >= 187.125 && beat < 187.5)
                    nod = 15.0f * (1.0f - Segment(beat, 187.125, 187.5));

                const float tumble = direction *
                    (35.0f + t * (i < 2 ? 430.0f : 350.0f));
                Put(frame, i,
                    {direction * Lerp(38.0f, -38.0f, t),
                     Heights[i] + arc * (i == 2 ? 4.0f : -2.0f) + slowWave * 2.0f,
                     Depths[i] + arc * 4.0f},
                    {-7.0f + nod + slowWave * 4.0f,
                     tumble,
                     -direction * (10.0f + arc * 18.0f)},
                    {Lerp(0.08f, 0.82f + static_cast<float>(i) * 0.035f, entry),
                     Lerp(0.08f, 0.82f + static_cast<float>(i) * 0.035f, entry),
                     1.0f},
                    (i & 1U) == 0 ? Geometry::CurvedIn : Geometry::CurvedOut,
                    0.94f,
                    -tumble * 0.78f);
            }

            constexpr std::array<double, 4> SatelliteStarts{
                171.0, 178.0, 185.0, 192.0};
            constexpr std::array<float, 4> SatelliteHeights{
                46.0f, -9.0f, 57.0f, -18.0f};
            constexpr std::array<float, 4> SatelliteDepths{
                62.0f, 71.0f, 49.0f, 82.0f};
            for(std::size_t satellite = 0; satellite < 4; ++satellite)
            {
                if(beat < SatelliteStarts[satellite])
                    continue;
                const std::size_t index = 4 + satellite;
                const float direction = (satellite & 1U) == 0 ? -1.0f : 1.0f;
                const float t = Smooth(Segment(
                    beat, SatelliteStarts[satellite], 219.0));
                const float entry = EaseOutBack(Segment(
                    beat,
                    SatelliteStarts[satellite],
                    SatelliteStarts[satellite] + 1.6));
                const float arc = std::sin(t * Pi);
                const float weave = std::sin(static_cast<float>(
                    (beat - SatelliteStarts[satellite]) *
                    (0.27 + satellite * 0.035) + satellite * 0.9));
                const float tumble = direction *
                    (65.0f + t * (300.0f + satellite * 48.0f));
                Put(frame, index,
                    {direction * Lerp(52.0f, -52.0f, t),
                     SatelliteHeights[satellite] + weave * 4.0f +
                         arc * (satellite < 2 ? 3.0f : -3.0f),
                     SatelliteDepths[satellite] + arc *
                         (satellite & 1U ? -5.0f : 6.0f)},
                    {-8.0f + weave * 9.0f,
                     tumble,
                     direction * (18.0f + weave * 22.0f)},
                    {Lerp(0.04f, 0.58f + satellite * 0.045f, entry),
                     Lerp(0.04f, 0.58f + satellite * 0.045f, entry),
                     1.0f},
                    (satellite & 1U) == 0
                        ? Geometry::CurvedOut : Geometry::CurvedIn,
                    0.84f,
                    -tumble * 0.74f);
            }

            return frame;
        }
        if(beat < 223.0)
        {
            const float t = Smooth(Segment(beat, 221.0, 223.0));
            for(std::size_t i = 0; i < 2; ++i)
            {
                const float side = i == 0 ? -1.0f : 1.0f;
                Put(frame, i,
                    {side * Lerp(28.0f, 0.0f, t), BaseY, 52.0f},
                    {-7.0f, side * Lerp(18.0f, 0.0f, t), side * Lerp(90.0f, 0.0f, t)},
                    {Lerp(0.8f, 1.35f, t), Lerp(1.2f, 1.35f, t), 1.0f},
                    i == 0 ? Geometry::CurvedIn : Geometry::CurvedOut,
                    1.0f - i * t);
            }
            return frame;
        }
        if(beat < 247.0)
        {
            const float t = Smooth(Segment(beat, 223.0, 247.0));
            // Let the flag deformation carry this phrase by itself. Rocking
            // the physical canvas at the same time masked the slower fabric
            // wave and made the motion unnecessarily aggressive.
            PutSingle(frame,
                {0.0f, Lerp(BaseY, 19.0f, t), Lerp(61.0f, 39.0f, t)},
                {-6.0f, 0.0f, 0.0f},
                {Lerp(1.5f, 2.35f, t), Lerp(1.5f, 2.1f, t), 1.0f},
                Geometry::Wide);
            ApplyDramaticFlagWave(frame.panels[0]);
            return frame;
        }

        // 247-263: seven arena-sized copies form a moving corkscrew tunnel.
        // They remain one texture and one upload; only transforms differ.
        if(beat < 263.0)
        {
            const float travel = static_cast<float>(beat - 247.0);
            // Open as a broad cone, then blend back to the established tunnel
            // over the first phrase. The nearest layers receive the greatest
            // radial expansion, making the corkscrew immediately readable in
            // the wide arena while preserving its already-successful ending.
            const float entranceFan =
                1.0f - Smooth(Segment(beat, 247.0, 253.5));
            for(std::size_t i = 0; i < 7; ++i)
            {
                const float depth = static_cast<float>(i) / 6.0f;
                const float angle = travel * 0.72f + i * 1.15f;
                // The nearest copy originally sat directly across the tunnel
                // opening at full scale. Lift it into a wide overhead orbit,
                // then rapidly taper that clearance into the following depth
                // layers. Panel zero now frames the tunnel entrance instead
                // of covering the screens behind it.
                const float frontWeight = std::max(0.0f, 1.0f - depth * 2.5f);
                const float frontLift = frontWeight * frontWeight * 20.0f;
                const float settledRadius =
                    5.0f + depth * 18.0f + frontLift * 0.30f;
                const float entranceRadius = Lerp(34.0f, 14.0f, depth);
                const float radius =
                    Lerp(settledRadius, entranceRadius, entranceFan);
                const float verticalOrbit =
                    radius * Lerp(0.62f, 0.82f, entranceFan) +
                    frontLift * 0.22f;
                const float baseScale = Lerp(1.15f, 0.48f, depth);
                const float clearedScale = baseScale * (1.0f - frontWeight * 0.20f);
                Put(frame, i,
                    {std::cos(angle) * radius,
                     16.0f + frontLift + std::sin(angle) * verticalOrbit,
                     30.0f + depth * 82.0f - std::fmod(travel * 2.8f, 12.0f)},
                    {-7.0f + std::sin(angle) * 12.0f,
                     std::cos(angle) * 22.0f,
                     angle * 57.29578f + depth * 150.0f},
                    {clearedScale, clearedScale, 1.0f},
                    (i & 1U) == 0 ? Geometry::CurvedIn : Geometry::CurvedOut,
                    Lerp(1.0f, 0.48f, depth),
                    -angle * 57.29578f);
            }
            return frame;
        }

        // 263-279: the centerpiece. Eight extremely large panels wrap around
        // and above the player as an inside-out helical video cage. This chart
        // section has no playable notes, so the formation can occupy the full
        // arena while retaining a safe minimum radius from the head.
        if(beat < 279.0)
        {
            const float t = Segment(beat, 263.0, 279.0);
            const float handedness = t < 0.5f ? 1.0f : -1.0f;
            const float localT = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
            const float orbit = t * Pi * 4.0f;
            for(std::size_t i = 0; i < 8; ++i)
            {
                const float indexPhase = static_cast<float>(i) / 8.0f * Pi * 2.0f;
                const float angle = orbit + handedness * indexPhase;
                const float alternate = (i & 1U) == 0 ? 1.0f : -1.0f;
                const float radius = 23.0f + alternate * 5.0f +
                    std::sin(localT * Pi) * 8.0f;
                const bool tall = (static_cast<int>(beat) + static_cast<int>(i)) % 2 == 0;
                Put(frame, i,
                    {std::cos(angle) * radius,
                     18.0f + std::sin(indexPhase * 2.0f + orbit * 0.45f) * 16.0f,
                     7.0f + std::sin(angle) * radius},
                    {-10.0f + std::sin(indexPhase + orbit) * 32.0f,
                     angle * 57.29578f + 180.0f,
                     -angle * 57.29578f * 1.35f + alternate * 35.0f},
                    {tall ? 1.12f : 1.85f,
                     tall ? 1.9f : 1.05f, 1.0f},
                    (i & 1U) == 0 ? Geometry::CurvedIn : Geometry::CurvedOut,
                    0.88f,
                    angle * 57.29578f * 1.35f - alternate * 35.0f);
            }
            return frame;
        }

        if(beat < 281.0)
        {
            const float t = Smooth(Segment(beat, 279.0, 280.35));
            if(beat < 280.35)
            {
                for(std::size_t i = 0; i < 8; ++i)
                {
                    const float angle = static_cast<float>(i) / 8.0f * Pi * 2.0f +
                        static_cast<float>((beat - 279.0) * 4.0);
                    Put(frame, i,
                        {Lerp(std::cos(angle) * 24.0f, 0.0f, t),
                         Lerp(18.0f + std::sin(angle) * 14.0f, 18.0f, t),
                         Lerp(7.0f + std::sin(angle) * 24.0f, 36.0f, t)},
                        {-8.0f, angle * 57.29578f, Lerp(angle * 57.29578f, 90.0f, t)},
                        {Lerp(1.2f, 0.025f, t), Lerp(1.2f, 0.025f, t), 1.0f},
                        (i & 1U) == 0 ? Geometry::CurvedIn : Geometry::CurvedOut,
                        1.0f - t);
                }
            }
            else
            {
                const float rebuild = EaseOutBack(Segment(beat, 280.35, 281.0));
                PutSingle(frame, {0.0f, 18.0f, 40.0f}, {-6.0f, 0.0f, 0.0f},
                    {rebuild * 2.35f, rebuild * 2.05f, 1.0f},
                    Geometry::CurvedOut, std::clamp(rebuild, 0.0f, 1.0f));
            }
            return frame;
        }
        if(beat < 282.0)
        {
            PutSingle(frame, {0.0f, 18.0f, 43.0f}, {-6.0f, 0.0f, 0.0f},
                {2.35f, 2.05f, 1.0f}, Geometry::CurvedIn);
            return frame;
        }
        if(beat < 283.0)
        {
            const float t = Smooth(Segment(beat, 282.0, 283.0));
            PutSingle(frame, {0.0f, 18.0f, 45.0f}, {-6.0f, 0.0f, 360.0f * t},
                {2.15f, 1.9f, 1.0f}, Geometry::Wide);
            return frame;
        }
        if(beat < 290.5)
        {
            PutSingle(frame,
                {0.0f, 20.0f + HalfBeatBounce(beat, 283.0, 7.0f), 48.0f},
                {-6.0f, 0.0f, 0.0f}, {2.05f, 2.05f, 1.0f}, Geometry::Wide);
            return frame;
        }
        if(beat < 295.0)
        {
            PutSingle(frame, {0.0f, 20.0f, 48.0f}, {-6.0f, 0.0f, 0.0f},
                {2.05f, 2.05f, 1.0f}, Geometry::Wide);
            return frame;
        }
        if(beat < 306.5)
        {
            const double step = std::floor((beat - 295.0) * 2.0);
            const float side = (static_cast<long long>(step) & 1LL) == 0 ? -1.0f : 1.0f;
            const float pulse = Pulse(beat, 2.0);
            PutSingle(frame,
                {0.0f, 20.0f + HalfBeatBounce(beat, 295.0, 7.0f), 48.0f},
                {-6.0f, 0.0f, side * 4.0f},
                {2.0f + pulse * 0.12f, 2.0f + pulse * 0.12f, 1.0f}, Geometry::Wide);
            return frame;
        }
        if(beat < 311.0)
        {
            PutSingle(frame, {0.0f, 19.0f, 51.0f}, {-6.0f, 0.0f, 0.0f},
                {1.85f, 1.85f, 1.0f});
            return frame;
        }
        if(beat < 322.5)
        {
            // Open video corridor. The previous large front panel hid too
            // much of the depth formation, so the nearest center copy becomes
            // a small moving ceiling/keystone. Three independently twisting
            // side pairs form the walls, leaving a direct sightline to the
            // terminal screen at the vanishing point. Four additional canopy
            // and under-runway satellites fill the formerly bare quadrants at
            // different depths without touching the corridor or note lanes.
            // All twelve panels remain
            // backed by the same texture and single frame upload.
            const float section = Segment(beat, 311.0, 322.5);
            const float keystoneEntry = EaseOutBack(Segment(beat, 311.0, 312.0));
            const float keystoneWave = std::sin(static_cast<float>(
                (beat - 311.0) * 0.72));
            const float keystoneRoll = std::sin(static_cast<float>(
                (beat - 311.0) * 0.43)) * 18.0f;
            Put(frame, 0,
                {keystoneWave * 6.0f,
                 34.5f + std::cos(static_cast<float>((beat - 311.0) * 0.55)) * 2.5f,
                 35.0f + std::sin(static_cast<float>((beat - 311.0) * 0.31)) * 2.0f},
                {-17.0f + keystoneWave * 6.0f,
                 -keystoneWave * 14.0f,
                 keystoneRoll},
                {Lerp(0.04f, 0.68f, keystoneEntry),
                 Lerp(0.04f, 0.68f, keystoneEntry), 1.0f},
                Geometry::CurvedOut,
                0.96f,
                -keystoneRoll * 0.82f);

            constexpr std::array<float, 3> WallX{28.0f, 40.0f, 51.0f};
            constexpr std::array<float, 3> WallY{17.0f, 22.0f, 15.0f};
            constexpr std::array<float, 3> WallZ{43.0f, 59.0f, 76.0f};
            constexpr std::array<float, 3> WallScale{0.92f, 0.83f, 0.72f};
            for(std::size_t pair = 0; pair < 3; ++pair)
            {
                const double pairStart = 311.15 + static_cast<double>(pair) * 0.42;
                if(beat < pairStart)
                    continue;
                const float entry = EaseOutBack(Segment(
                    beat, pairStart, pairStart + 1.15));
                for(std::size_t sideIndex = 0; sideIndex < 2; ++sideIndex)
                {
                    const std::size_t index = 1 + pair * 2 + sideIndex;
                    const float side = sideIndex == 0 ? -1.0f : 1.0f;
                    const float phase = static_cast<float>(
                        (beat - 311.0) * 0.86 + pair * 1.27 + sideIndex * Pi);
                    const float corkscrew = std::sin(phase);
                    const float spring = HalfBeatBounce(
                        beat, 311.0 + pair * 0.17, 2.2f + pair * 0.8f);
                    const float targetX = WallX[pair] + spring;
                    const float targetY = WallY[pair] + corkscrew * (3.5f + pair);
                    const float targetZ = WallZ[pair] - section * (2.0f + pair);
                    const float roll = side * (8.0f + pair * 7.0f) +
                        corkscrew * (7.0f + pair * 2.0f);
                    Put(frame, index,
                        {side * Lerp(2.0f, targetX, entry),
                         Lerp(19.0f, targetY, entry),
                         Lerp(88.0f, targetZ, entry)},
                        {-7.0f + corkscrew * (5.0f + pair * 2.0f),
                         -side * (29.0f + pair * 10.0f) + corkscrew * 6.0f,
                         roll},
                        {Lerp(0.035f, WallScale[pair], entry),
                         Lerp(0.035f, WallScale[pair], entry), 1.0f},
                        (index & 1U) == 0
                            ? Geometry::CurvedIn : Geometry::CurvedOut,
                        0.94f - static_cast<float>(pair) * 0.11f,
                        -roll * 0.88f);
                }
            }

            if(beat >= 311.8)
            {
                const float terminalEntry = EaseOutBack(Segment(beat, 311.8, 313.0));
                const float terminalPulse = Pulse(beat, 0.5);
                const float terminalRoll = std::sin(static_cast<float>(
                    (beat - 311.0) * 0.66)) * 10.0f;
                Put(frame, 7,
                    {std::sin(static_cast<float>((beat - 311.0) * 0.37)) * 2.5f,
                     19.0f + std::cos(static_cast<float>((beat - 311.0) * 0.48)) * 2.0f,
                     92.0f - section * 5.0f},
                    {-7.0f, -terminalRoll * 0.35f, terminalRoll},
                    {Lerp(0.03f, 0.68f + terminalPulse * 0.10f, terminalEntry),
                     Lerp(0.03f, 0.68f + terminalPulse * 0.10f, terminalEntry), 1.0f},
                    Geometry::CurvedIn,
                    0.72f,
                    -terminalRoll);
            }

            constexpr std::array<double, 4> AccentStarts{
                311.25, 311.60, 311.95, 312.30};
            constexpr std::array<float, 4> AccentX{
                -21.0f, 27.0f, -39.0f, 42.0f};
            constexpr std::array<float, 4> AccentY{
                49.0f, -13.0f, 42.0f, -21.0f};
            constexpr std::array<float, 4> AccentZ{
                48.0f, 54.0f, 68.0f, 74.0f};
            for(std::size_t accent = 0; accent < 4; ++accent)
            {
                if(beat < AccentStarts[accent])
                    continue;
                const std::size_t index = 8 + accent;
                const float side = AccentX[accent] < 0.0f ? -1.0f : 1.0f;
                const float verticalSide = AccentY[accent] < 0.0f ? -1.0f : 1.0f;
                const float entry = EaseOutBack(Segment(
                    beat, AccentStarts[accent], AccentStarts[accent] + 1.0));
                const float phase = static_cast<float>(
                    (beat - 311.0) * (0.74 + accent * 0.09) + accent * 1.33);
                const float sway = std::sin(phase);
                const float bob = std::cos(phase * 0.71f + accent);
                const float roll = side * (24.0f + accent * 13.0f) + sway * 19.0f;
                const float targetScale = 0.66f + accent * 0.055f;
                Put(frame, index,
                    {side * Lerp(3.0f, std::abs(AccentX[accent]) + sway * 5.0f, entry),
                     Lerp(18.0f, AccentY[accent] + bob * 4.5f, entry),
                     Lerp(91.0f, AccentZ[accent] - section *
                         (1.5f + accent * 0.55f), entry)},
                    {-8.0f + verticalSide * sway * 15.0f,
                     -side * (38.0f + accent * 9.0f) + bob * 8.0f,
                     roll},
                    {Lerp(0.03f, targetScale, entry),
                     Lerp(0.03f, targetScale, entry), 1.0f},
                    (accent & 1U) == 0
                        ? Geometry::CurvedOut : Geometry::CurvedIn,
                    0.82f,
                    -roll * 0.86f);
            }
            return frame;
        }
        if(beat < 338.5)
        {
            const float t = Segment(beat, 322.5, 338.5);
            PutSingle(frame,
                {0.0f, 19.0f + HalfBeatBounce(beat, 322.5, 6.0f), Lerp(56.0f, 43.0f, t)},
                {-6.0f, 0.0f, 0.0f},
                {Lerp(1.8f, 2.45f, t), Lerp(1.55f, 2.0f, t), 1.0f},
                Geometry::UltraWide);
            return frame;
        }
        if(beat < 344.0)
        {
            const float t = Smooth(Segment(beat, 338.5, 344.0));
            PutSingle(frame,
                {0.0f, Lerp(22.0f, BaseY, t), Lerp(43.0f, BaseZ, t)},
                {-6.0f, 0.0f, 0.0f},
                {Lerp(2.45f, 1.7f, t), Lerp(2.0f, 1.7f, t), 1.0f},
                Geometry::UltraWide);
            return frame;
        }

        const float t = Smooth(Segment(beat, 344.0, 371.0));
        PutSingle(frame,
            {0.0f, BaseY, Lerp(BaseZ, 60.0f, t)},
            {Lerp(-6.0f, -8.0f, t), 0.0f, 0.0f},
            {Lerp(1.7f, 1.45f, t), Lerp(1.7f, 1.45f, t), 1.0f},
            Geometry::Wide,
            beat > 370.0 ? 1.0f - Segment(beat, 370.0, 371.0) : 1.0f);
        return frame;
    }

    FrameState Sample(double songTimeSeconds)
    {
        auto frame = SampleBase(songTimeSeconds);
        if(!std::isfinite(songTimeSeconds) || songTimeSeconds < 0.0)
            return frame;
        return ApplyGlassCues(
            std::move(frame), songTimeSeconds * BeatsPerMinute / 60.0);
    }

    bool CenterRingVisible(double songTimeSeconds)
    {
        if(!std::isfinite(songTimeSeconds) || songTimeSeconds < 0.0)
            return true;

        const double beat = songTimeSeconds * BeatsPerMinute / 60.0;
        // Remove the center spinner throughout the slow four-screen carousel.
        // The drifting panels need an unobstructed arena, and the ring snaps
        // back on the exact boundary where the carousel merges into the next
        // section. This is a steady hide, separate from the later strobe.
        if(beat >= 168.0 && beat < 219.0)
            return false;

        // Hide the spinner for the complete flag-wave phrase so the deforming
        // canvas remains unobstructed. The corkscrew begins on the same exact
        // boundary and also requires an open center, so keep it hidden without
        // a one-frame restore/flicker and return it when the corkscrew ends.
        if(beat >= 223.0 && beat < 263.0)
            return false;

        // Begin with the singularity at 2:01 and finish before the settled
        // outro at roughly 2:29. Half-beat cells create sharp removal/restores
        // without using frame-rate-dependent timers or high-frequency flashes.
        if(beat < 278.5 || beat >= 342.7)
            return true;

        constexpr std::array<bool, 16> Pattern{
            false, true, false, false,
            true, false, true, true,
            false, false, true, false,
            true, true, false, true};
        const auto step = static_cast<std::size_t>(
            std::floor((beat - 278.5) * 2.0));
        return Pattern[step % Pattern.size()];
    }

    bool SidePillarsVisible(double songTimeSeconds)
    {
        if(!std::isfinite(songTimeSeconds) || songTimeSeconds < 0.0)
            return true;

        const double beat = songTimeSeconds * BeatsPerMinute / 60.0;
        // Match the complete floating-screen carousel. The structures return
        // at the same deterministic beat where its last panels leave, so a
        // pause, Replay seek, or practice jump cannot strand them hidden.
        return beat < 168.0 || beat >= 219.0;
    }

    bool BackgroundEnvironmentVisible(double songTimeSeconds)
    {
        if(!std::isfinite(songTimeSeconds) || songTimeSeconds < 0.0)
            return true;
        const double beat = songTimeSeconds * BeatsPerMinute / 60.0;
        // Remove the complete rendered environment during the floating-screen
        // carousel, not only the previously enumerated side obstructions. The
        // short section between the carousel and corkscrew restores the map,
        // then the corkscrew hides it again for an unobstructed depth effect.
        const bool floatingScreens = beat >= 168.0 && beat < 219.0;
        const bool corkscrew = beat >= 247.0 && beat < 263.0;
        return !floatingScreens && !corkscrew;
    }
}
