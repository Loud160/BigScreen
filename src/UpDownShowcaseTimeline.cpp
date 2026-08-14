#include "BigScreen/UpDownShowcaseTimeline.hpp"

#include <algorithm>
#include <cmath>

namespace BigScreen::UpDownShowcase {
    namespace {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float BaseY = 17.0f;
        constexpr float BaseZ = 58.0f;

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

    FrameState Sample(double songTimeSeconds)
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

        // 88-102: four actual surfaces each reveal one source quadrant, then
        // physically fracture the image before rebuilding it.
        if(beat < 102.0)
        {
            const float entry = EaseOutBack(Segment(beat, 88.0, 89.0));
            const float fracture = std::sin(static_cast<float>(
                Segment(beat, 90.0, 100.5) * Pi));
            for(std::size_t i = 0; i < 4; ++i)
            {
                const float side = (i & 1U) == 0 ? -1.0f : 1.0f;
                const float row = i < 2 ? 1.0f : -1.0f;
                const Geometry geometry = static_cast<Geometry>(
                    static_cast<int>(Geometry::QuadrantTopLeft) + static_cast<int>(i));
                Put(frame, i,
                    {side * Lerp(1.0f, 19.0f + 8.0f * fracture, entry),
                     BaseY + row * Lerp(1.0f, 10.5f + 4.0f * fracture, entry),
                     BaseZ + fracture * (i == 0 || i == 3 ? -12.0f : 10.0f)},
                    {-7.0f + row * fracture * 8.0f,
                     side * fracture * 28.0f,
                     side * row * fracture * 16.0f},
                    {0.92f, 0.92f, 1.0f}, geometry, 1.0f,
                    -side * row * fracture * 16.0f);
            }
            return frame;
        }

        if(beat < 121.5)
        {
            for(std::size_t i = 0; i < 2; ++i)
            {
                const float side = i == 0 ? -1.0f : 1.0f;
                const float wave = std::sin(static_cast<float>((beat - 102.0) * Pi));
                Put(frame, i,
                    {side * 23.0f, BaseY + side * wave * 9.0f,
                     55.0f + side * wave * 4.0f},
                    {-8.0f, side * 10.0f, side * wave * 9.0f},
                    {1.18f, 1.55f, 1.0f}, Geometry::Tall, 1.0f,
                    -side * wave * 9.0f);
            }
            return frame;
        }
        if(beat < 122.0)
        {
            const float t = Smooth(Segment(beat, 121.5, 122.0));
            for(std::size_t i = 0; i < 2; ++i)
            {
                const float side = i == 0 ? -1.0f : 1.0f;
                Put(frame, i, {side * Lerp(23.0f, 0.0f, t), BaseY, 55.0f},
                    {-8.0f, side * Lerp(10.0f, 0.0f, t), 0.0f},
                    {Lerp(1.18f, 0.82f, t), Lerp(1.55f, 1.45f, t), 1.0f},
                    Geometry::Tall, 1.0f - i * t);
            }
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
            return frame;
        }
        if(beat < 138.0)
        {
            PutSingle(frame, {0.0f, BaseY, 54.0f}, {-7.0f, 0.0f, 0.0f},
                {1.8f, 1.8f, 1.0f}, Geometry::CurvedIn);
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
            const float angle = static_cast<float>((beat - 168.0) / 51.0 * Pi * 1.25);
            float nod = 0.0f;
            if(beat >= 187.0 && beat < 187.125)
                nod = -15.0f;
            else if(beat >= 187.125 && beat < 187.5)
                nod = 15.0f * (1.0f - Segment(beat, 187.125, 187.5));
            PutSingle(frame,
                {std::sin(angle) * 24.0f, 15.0f + std::sin(angle * 0.5f) * 5.0f,
                 49.0f + std::cos(angle) * 11.0f},
                {-7.0f + nod, std::sin(angle) * -24.0f, std::sin(angle * 0.7f) * 5.0f},
                {0.66f, 0.66f, 1.0f}, Geometry::CurvedIn, 0.94f);
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
            PutSingle(frame,
                {0.0f, Lerp(BaseY, 19.0f, t), Lerp(61.0f, 39.0f, t)},
                {-6.0f, 0.0f, std::sin(static_cast<float>(beat * 0.12)) * 2.0f},
                {Lerp(1.5f, 2.35f, t), Lerp(1.5f, 2.1f, t), 1.0f},
                Geometry::CurvedIn, 1.0f,
                static_cast<float>((beat - 223.0) * 0.9));
            return frame;
        }

        // 247-263: seven arena-sized copies form a moving corkscrew tunnel.
        // They remain one texture and one upload; only transforms differ.
        if(beat < 263.0)
        {
            const float travel = static_cast<float>(beat - 247.0);
            for(std::size_t i = 0; i < 7; ++i)
            {
                const float depth = static_cast<float>(i) / 6.0f;
                const float angle = travel * 0.72f + i * 1.15f;
                const float radius = 5.0f + depth * 18.0f;
                Put(frame, i,
                    {std::cos(angle) * radius,
                     16.0f + std::sin(angle) * radius * 0.62f,
                     30.0f + depth * 82.0f - std::fmod(travel * 2.8f, 12.0f)},
                    {-7.0f + std::sin(angle) * 12.0f,
                     std::cos(angle) * 22.0f,
                     angle * 57.29578f + depth * 150.0f},
                    {Lerp(1.15f, 0.48f, depth), Lerp(1.15f, 0.48f, depth), 1.0f},
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
            for(std::size_t i = 0; i < 2; ++i)
            {
                const float side = i == 0 ? -1.0f : 1.0f;
                Put(frame, i,
                    {side * 23.0f, 18.0f + side * HalfBeatBounce(beat, 311.0, 6.0f), 52.0f},
                    {-7.0f, side * 14.0f, side * 5.0f},
                    {1.2f, 1.55f, 1.0f},
                    i == 0 ? Geometry::CurvedIn : Geometry::CurvedOut);
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
}
