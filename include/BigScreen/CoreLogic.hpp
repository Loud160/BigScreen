// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BigScreen::CoreLogic {
    struct SemanticVersion {
        std::array<std::uint64_t, 3> core{};
        std::vector<std::string> prerelease;
    };

    /// Parses the release-tag subset Big Screen publishes (for example
    /// v0.7.0 or 0.7.0-alpha.1). Build metadata is intentionally ignored for
    /// precedence. Rejecting malformed tags is safer than presenting an
    /// untrusted GitHub label as an actionable update.
    inline std::optional<SemanticVersion> ParseSemanticVersion(
        std::string_view text)
    {
        while(!text.empty() && std::isspace(
                  static_cast<unsigned char>(text.front())))
            text.remove_prefix(1);
        while(!text.empty() && std::isspace(
                  static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        if(!text.empty() && (text.front() == 'v' || text.front() == 'V'))
            text.remove_prefix(1);
        if(const auto plus = text.find('+'); plus != std::string_view::npos)
            text = text.substr(0, plus);

        SemanticVersion version;
        const auto dash = text.find('-');
        const auto core = text.substr(0, dash);
        auto remaining = core;
        for(std::size_t index = 0; index < version.core.size(); ++index)
        {
            const auto dot = remaining.find('.');
            const auto part = remaining.substr(0, dot);
            if(part.empty() ||
               !std::all_of(part.begin(), part.end(), [](unsigned char value) {
                   return std::isdigit(value) != 0;
               }))
                return std::nullopt;
            std::uint64_t value = 0;
            const auto result = std::from_chars(
                part.data(), part.data() + part.size(), value);
            if(result.ec != std::errc{} || result.ptr != part.data() + part.size())
                return std::nullopt;
            version.core[index] = value;
            if(index + 1 < version.core.size())
            {
                if(dot == std::string_view::npos)
                    return std::nullopt;
                remaining.remove_prefix(dot + 1);
            }
            else if(dot != std::string_view::npos)
                return std::nullopt;
        }

        if(dash == std::string_view::npos)
            return version;
        auto prerelease = text.substr(dash + 1);
        if(prerelease.empty())
            return std::nullopt;
        while(true)
        {
            const auto dot = prerelease.find('.');
            const auto identifier = prerelease.substr(0, dot);
            if(identifier.empty() ||
               !std::all_of(
                   identifier.begin(), identifier.end(), [](unsigned char value) {
                       return std::isalnum(value) != 0 || value == '-';
                   }))
                return std::nullopt;
            version.prerelease.emplace_back(identifier);
            if(dot == std::string_view::npos)
                break;
            prerelease.remove_prefix(dot + 1);
        }
        return version;
    }

    inline int CompareSemanticVersions(
        const SemanticVersion& left,
        const SemanticVersion& right)
    {
        if(left.core < right.core) return -1;
        if(left.core > right.core) return 1;
        if(left.prerelease.empty() != right.prerelease.empty())
            return left.prerelease.empty() ? 1 : -1;

        const auto count = std::min(
            left.prerelease.size(), right.prerelease.size());
        for(std::size_t index = 0; index < count; ++index)
        {
            const auto& l = left.prerelease[index];
            const auto& r = right.prerelease[index];
            const bool lNumeric = std::all_of(
                l.begin(), l.end(), [](unsigned char value) {
                    return std::isdigit(value) != 0;
                });
            const bool rNumeric = std::all_of(
                r.begin(), r.end(), [](unsigned char value) {
                    return std::isdigit(value) != 0;
                });
            if(lNumeric && rNumeric)
            {
                // Numeric identifiers can exceed an integer type. Stripping
                // leading zeros and comparing length preserves exact ordering.
                const auto lStart = std::min(l.find_first_not_of('0'), l.size());
                const auto rStart = std::min(r.find_first_not_of('0'), r.size());
                const auto lDigits = l.substr(lStart);
                const auto rDigits = r.substr(rStart);
                if(lDigits.size() != rDigits.size())
                    return lDigits.size() < rDigits.size() ? -1 : 1;
                if(lDigits != rDigits)
                    return lDigits < rDigits ? -1 : 1;
            }
            else if(lNumeric != rNumeric)
                return lNumeric ? -1 : 1;
            else if(l != r)
                return l < r ? -1 : 1;
        }
        if(left.prerelease.size() == right.prerelease.size()) return 0;
        return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
    }

    inline bool IsReleaseVersionNewer(
        std::string_view current,
        std::string_view candidate)
    {
        const auto currentVersion = ParseSemanticVersion(current);
        const auto candidateVersion = ParseSemanticVersion(candidate);
        return currentVersion && candidateVersion &&
            CompareSemanticVersions(*candidateVersion, *currentVersion) > 0;
    }

    enum class VideoCodecKind { Unknown, H264, Hevc, Vp8, Vp9 };

    inline bool IsSupportedVideoCodec(VideoCodecKind codec) noexcept
    {
        return codec != VideoCodecKind::Unknown;
    }

    inline bool VideoCodecRequiresHardware(VideoCodecKind codec) noexcept
    {
        return codec == VideoCodecKind::Hevc;
    }

    /// Empty means CPU fallback is permitted. HEVC is intentionally omitted
    /// from Big Screen's software build, while any source above 1080p is kept
    /// hardware-only to protect Quest gameplay performance.
    inline std::string SoftwareFallbackBlockedReason(
        VideoCodecKind codec,
        int sourceShortEdge)
    {
        if(codec == VideoCodecKind::Hevc)
            return "H.265/HEVC requires Hardware Video Decoding; Big Screen does not ship a software HEVC decoder.";
        if(sourceShortEdge > 1080)
            return "This " + std::to_string(sourceShortEdge) +
                "p video requires hardware decoding. Video playback was stopped because software fallback is not supported above 1080p.";
        return {};
    }

    inline std::string UnsupportedVideoSignalReason(
        VideoCodecKind codec,
        bool hdrTransfer,
        bool tenBit,
        bool alphaChannel,
        bool chroma420)
    {
        if(hdrTransfer)
            return "HDR video is not supported - re-export the video as 8-bit SDR.";
        if(tenBit)
        {
            if(codec == VideoCodecKind::Hevc)
                return "H.265/HEVC Main10 video is not supported - re-export the video as 8-bit SDR Main profile.";
            if(codec == VideoCodecKind::Vp9)
                return "VP9 profile 2 (10-bit/HDR) is not supported - re-export the video as 8-bit SDR VP9 profile 0.";
            return "10-bit video is not supported - re-export the video as 8-bit SDR.";
        }
        if(alphaChannel)
            return "Video with an alpha channel is not supported - export ordinary 8-bit 4:2:0 video without transparency.";
        if(!chroma420)
            return "Only 8-bit 4:2:0 video is supported - re-export this video using yuv420p.";
        return {};
    }

    /// Advances a menu-preview clock smoothly between the coarse updates that
    /// Unity exposes through AudioSource.time. Large differences are real
    /// seeks/restarts and re-anchor immediately; ordinary buffer quantization
    /// is corrected gradually so a 60 FPS video does not skip presentation
    /// slots merely because the audio property advances in larger steps.
    inline double AdvanceSmoothedPreviewClock(
        double smoothedSongSeconds,
        double rawAudioSongSeconds,
        double realSecondsElapsed)
    {
        if(!std::isfinite(rawAudioSongSeconds) || rawAudioSongSeconds < 0.0)
            return std::max(0.0, smoothedSongSeconds);
        if(!std::isfinite(smoothedSongSeconds) || smoothedSongSeconds < 0.0 ||
           !std::isfinite(realSecondsElapsed) || realSecondsElapsed < 0.0)
            return rawAudioSongSeconds;

        const double predicted = smoothedSongSeconds +
            std::clamp(realSecondsElapsed, 0.0, 0.10);
        const double error = rawAudioSongSeconds - predicted;
        if(std::abs(error) > 0.075)
            return rawAudioSongSeconds;
        return std::max(
            0.0,
            predicted + std::clamp(error * 0.08, -0.0015, 0.0015));
    }

    /// Converts CPU milliseconds accumulated by one process or thread into the
    /// conventional percentage of one fully occupied core. Values above 100
    /// are valid for a multi-threaded process.
    inline double CpuPercentOfOneCore(
        double cpuMilliseconds,
        double wallSeconds)
    {
        if(cpuMilliseconds <= 0.0 || wallSeconds <= 0.0)
            return 0.0;
        return cpuMilliseconds / (wallSeconds * 10.0);
    }

    inline double EquivalentCpuCores(
        double cpuMilliseconds,
        double wallSeconds)
    {
        return CpuPercentOfOneCore(cpuMilliseconds, wallSeconds) / 100.0;
    }

    /// Converts a fuel-gauge charge-counter decrease to an hourly drain rate.
    /// A rising counter means the headset was charging and is intentionally
    /// reported as zero consumption rather than a negative battery drain.
    inline double ChargeConsumedMicroampHours(
        std::int64_t startMicroampHours,
        std::int64_t endMicroampHours)
    {
        return static_cast<double>(std::max<std::int64_t>(
            0,
            startMicroampHours - endMicroampHours));
    }

    inline double ConsumptionRateMahPerHour(
        double consumedMicroampHours,
        double wallSeconds)
    {
        if(consumedMicroampHours <= 0.0 || wallSeconds <= 0.0)
            return 0.0;
        return (consumedMicroampHours / 1000.0) /
               (wallSeconds / 3600.0);
    }
    inline constexpr float MinimumScreenScale = 0.5f;
    inline constexpr float CurvedScreenMaximumScale = 8.0f;
    inline constexpr float FlatScreenMaximumScale = 8.0f;

    /// Both canvas modes share the same presentation ceiling. Curvature changes
    /// vertex positions but not segment count, so increasing physical size does
    /// not create additional geometry or decoder work.
    inline constexpr float ScreenScaleMaximum(bool curved)
    {
        return curved ? CurvedScreenMaximumScale : FlatScreenMaximumScale;
    }

    inline float NormalizeScreenScale(float value, bool curved)
    {
        return std::clamp(
            value,
            MinimumScreenScale,
            ScreenScaleMaximum(curved));
    }

    struct VideoContentSize {
        float width;
        float height;
    };

    /// Small engine-independent vector used by the showcase deformation math.
    /// Keeping this type free of Unity headers makes the geometry rules usable
    /// by the host tests and prevents accidental Unity calls off the main thread.
    struct DeformationVector {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    enum class WaveAnchorEdge {
        Left,
        Right
    };

    enum class DeformationClock {
        SongTime,
        RealTime
    };

    enum class DeformationFillMode {
        StretchToFill,
        AutoZoomCover
    };

    struct CornerWarpSettings {
        // Storage order is bottom-left, top-left, top-right, bottom-right.
        std::array<DeformationVector, 4> corners{};
    };

    struct FlagWaveSettings {
        bool enabled = false;
        float cyclesAcross = 1.0f;
        float speedCyclesPerSecond = 1.0f;
        float depth = 0.0f;
        float ripple = 0.0f;
        float anchorRamp = 1.0f;
        WaveAnchorEdge anchoredEdge = WaveAnchorEdge::Left;
        float phaseOffset = 0.0f;
        DeformationClock clock = DeformationClock::SongTime;
    };

    struct SurfaceDeformationSettings {
        bool enabled = false;
        CornerWarpSettings cornerWarp{};
        FlagWaveSettings wave{};
        DeformationFillMode fillMode = DeformationFillMode::StretchToFill;
    };

    // Engine-independent fracture primitives. Coordinates use the normalized
    // screen rectangle [0,1] x [0,1], which keeps generation independent of
    // the current world-space screen size and lets the Unity layer reuse one
    // deterministic pattern across transforms.
    struct FracturePoint {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct FractureEdge {
        FracturePoint from{};
        FracturePoint to{};
    };

    struct FractureCell {
        FracturePoint site{};
        std::vector<FracturePoint> vertices{};
    };

    struct FractureTriangle {
        FracturePoint a{};
        FracturePoint b{};
        FracturePoint c{};
    };

    struct FracturePatternSettings {
        std::uint32_t seed = 0x42534731U;
        std::size_t pieceCount = 64;
        FracturePoint impactPoint{0.5f, 0.5f};
        std::size_t spokeCount = 10;
        std::size_t ringCount = 3;
        float jitter = 0.18f;
    };

    struct FracturePattern {
        std::vector<FractureCell> cells{};
        std::vector<FractureEdge> edges{};
        std::vector<FractureEdge> radialEdges{};
    };

    enum class FracturePhase {
        Inactive,
        Prepared,
        CrackOnly,
        Shattered,
        Rejoining
    };

    struct FractureShardTransform {
        std::size_t shardIndex = 0;
        DeformationVector translation{};
        DeformationVector rotationDegrees{};
        float scale = 1.0f;
    };

    struct FractureEffectSettings {
        bool enabled = false;
        FracturePhase phase = FracturePhase::Inactive;
        FracturePatternSettings pattern{};
        // The showcase impact sequence can accumulate one authored strike per
        // downward beat before the final break. Keep this fixed and bounded so
        // gameplay performs no timeline-driven heap allocation while still
        // allowing cracks to spread across the full pane.
        std::array<FracturePoint, 20> impacts{};
        std::size_t impactCount = 0;
        std::size_t revealedGroupCount = 0;
        // Most cues use the bulk explosion/gravity controls below. A bounded
        // override list still makes selected pieces individually addressable
        // without dynamic allocation or exposing internal mesh buffers.
        std::array<FractureShardTransform, 16> shardTransforms{};
        std::size_t shardTransformCount = 0;
        bool freezeOnShatter = false;
        bool retainCracksAfterRejoin = false;
        float separation = 0.0f;
        float outwardDistance = 28.0f;
        // Optional independent travel toward the viewer. A zero value retains
        // the legacy kick-derived depth; authored shatters can set this higher
        // to spread fragments through a deep volume without also exaggerating
        // their horizontal and vertical explosion.
        float forwardScatterDistance = 0.0f;
        float gravityDistance = 45.0f;
        float tumbleDegrees = 420.0f;
        float stagger = 0.18f;
        float crackOpacity = 0.78f;
    };

    inline constexpr std::size_t MaximumFracturePieces = 200;
    inline constexpr std::array<std::pair<std::string_view, std::uint32_t>, 12>
        CuratedFractureSeeds{{
            {"Prism Burst", 0x3E91A5C7U},
            {"Spider Crown", 0xA80F24D1U},
            {"Split Horizon", 0x61C4B93EU},
            {"Crystal Rain", 0xD25E718BU},
            {"Fault Line", 0x1847CF62U},
            {"Star Impact", 0xF09A36B4U},
            {"Jagged Halo", 0x72B5E80DU},
            {"Glass Storm", 0xC6134A9FU},
            {"Broken Orbit", 0x4D8F215AU},
            {"Shiver Web", 0x9B307CE1U},
            {"Diamond Fall", 0x256AE7D8U},
            {"Finale", 0xE74C1385U}}};

    /// Small fixed algorithm used instead of engine randomness. Keeping the
    /// state transition explicit makes the same seed reproduce the same pane
    /// under Replay, practice seeks, pauses, and different frame rates.
    class SeededFractureRandom final {
    public:
        explicit SeededFractureRandom(std::uint32_t seed)
            : state_(seed == 0 ? 0x6D2B79F5U : seed) {}

        std::uint32_t Next()
        {
            std::uint32_t value = state_;
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
            state_ = value;
            return value;
        }

        float Unit()
        {
            return static_cast<float>(Next() >> 8) /
                   static_cast<float>(0x01000000U);
        }

        float Signed() { return Unit() * 2.0f - 1.0f; }

    private:
        std::uint32_t state_;
    };

    inline float FracturePolygonArea(
        const std::vector<FracturePoint>& polygon)
    {
        if(polygon.size() < 3)
            return 0.0f;
        double twiceArea = 0.0;
        for(std::size_t index = 0; index < polygon.size(); ++index)
        {
            const auto& current = polygon[index];
            const auto& next = polygon[(index + 1) % polygon.size()];
            twiceArea += static_cast<double>(current.x) * next.y -
                         static_cast<double>(next.x) * current.y;
        }
        return static_cast<float>(std::abs(twiceArea) * 0.5);
    }

    inline std::vector<FractureTriangle> TriangulateFractureCell(
        const FractureCell& cell)
    {
        std::vector<FractureTriangle> triangles;
        if(cell.vertices.size() < 3)
            return triangles;
        triangles.reserve(cell.vertices.size() - 2);
        // Fracture polygons are generated counter-clockwise in normalized
        // screen coordinates. Big Screen's video mesh deliberately uses the
        // opposite winding so its front face points toward the player from the
        // back wall. Preserve that same clockwise front face for every shard;
        // otherwise Unity back-face culling makes the pane disappear at the
        // exact frame where the intact mesh is replaced by the fracture mesh.
        for(std::size_t index = 1; index + 1 < cell.vertices.size(); ++index)
            triangles.push_back(
                {cell.vertices[0], cell.vertices[index + 1], cell.vertices[index]});
        return triangles;
    }

    inline std::vector<FracturePoint> ClipFracturePolygonToBisector(
        const std::vector<FracturePoint>& polygon,
        FracturePoint site,
        FracturePoint other)
    {
        std::vector<FracturePoint> output;
        if(polygon.empty())
            return output;
        output.reserve(polygon.size() + 1);
        const float normalX = other.x - site.x;
        const float normalY = other.y - site.y;
        const float limit = (other.x * other.x + other.y * other.y -
                             site.x * site.x - site.y * site.y) * 0.5f;
        const auto distance = [&](FracturePoint point)
        {
            return point.x * normalX + point.y * normalY - limit;
        };
        FracturePoint previous = polygon.back();
        float previousDistance = distance(previous);
        bool previousInside = previousDistance <= 0.000001f;
        for(const auto current : polygon)
        {
            const float currentDistance = distance(current);
            const bool currentInside = currentDistance <= 0.000001f;
            if(currentInside != previousInside)
            {
                const float denominator = previousDistance - currentDistance;
                const float amount = std::abs(denominator) < 0.0000001f
                    ? 0.0f : previousDistance / denominator;
                output.push_back({
                    previous.x + (current.x - previous.x) * amount,
                    previous.y + (current.y - previous.y) * amount});
            }
            if(currentInside)
                output.push_back(current);
            previous = current;
            previousDistance = currentDistance;
            previousInside = currentInside;
        }
        return output;
    }

    inline bool SameFracturePoint(
        FracturePoint left,
        FracturePoint right,
        float epsilon = 0.0001f)
    {
        return std::abs(left.x - right.x) <= epsilon &&
               std::abs(left.y - right.y) <= epsilon;
    }

    inline std::vector<FractureEdge> UniqueFractureEdges(
        const std::vector<FractureCell>& cells)
    {
        std::vector<FractureEdge> edges;
        edges.reserve(cells.size() * 3);
        for(const auto& cell : cells)
        {
            for(std::size_t index = 0; index < cell.vertices.size(); ++index)
            {
                FractureEdge candidate{
                    cell.vertices[index],
                    cell.vertices[(index + 1) % cell.vertices.size()]};
                // The outside rectangle is the physical screen boundary, not
                // a glass crack. Excluding it avoids drawing a bright frame.
                const bool outside =
                    (std::abs(candidate.from.x) < 0.0001f &&
                     std::abs(candidate.to.x) < 0.0001f) ||
                    (std::abs(candidate.from.x - 1.0f) < 0.0001f &&
                     std::abs(candidate.to.x - 1.0f) < 0.0001f) ||
                    (std::abs(candidate.from.y) < 0.0001f &&
                     std::abs(candidate.to.y) < 0.0001f) ||
                    (std::abs(candidate.from.y - 1.0f) < 0.0001f &&
                     std::abs(candidate.to.y - 1.0f) < 0.0001f);
                if(outside)
                    continue;
                const bool duplicate = std::any_of(
                    edges.begin(), edges.end(), [&](const FractureEdge& edge)
                    {
                        return (SameFracturePoint(edge.from, candidate.from) &&
                                SameFracturePoint(edge.to, candidate.to)) ||
                               (SameFracturePoint(edge.from, candidate.to) &&
                                SameFracturePoint(edge.to, candidate.from));
                    });
                if(!duplicate)
                    edges.push_back(candidate);
            }
        }
        return edges;
    }

    inline std::vector<FractureEdge> GenerateRadialCracks(
        const FracturePatternSettings& settings)
    {
        std::vector<FractureEdge> edges;
        const std::size_t spokes = std::clamp<std::size_t>(
            settings.spokeCount, 3, 32);
        const std::size_t rings = std::clamp<std::size_t>(
            settings.ringCount, 1, 8);
        edges.reserve(spokes * (rings + 1));
        SeededFractureRandom random(settings.seed ^ 0xD1B54A35U);
        constexpr float TwoPi = 6.28318530717958647692f;
        std::vector<std::vector<FracturePoint>> ringPoints(
            rings, std::vector<FracturePoint>(spokes));
        for(std::size_t spoke = 0; spoke < spokes; ++spoke)
        {
            const float angle = TwoPi * static_cast<float>(spoke) /
                static_cast<float>(spokes) + random.Signed() * settings.jitter * 0.15f;
            FracturePoint previous = settings.impactPoint;
            for(std::size_t ring = 0; ring < rings; ++ring)
            {
                const float radius = 0.48f * static_cast<float>(ring + 1) /
                    static_cast<float>(rings) *
                    (1.0f + random.Signed() * settings.jitter * 0.18f);
                FracturePoint point{
                    std::clamp(settings.impactPoint.x + std::cos(angle) * radius,
                               0.0f, 1.0f),
                    std::clamp(settings.impactPoint.y + std::sin(angle) * radius,
                               0.0f, 1.0f)};
                ringPoints[ring][spoke] = point;
                edges.push_back({previous, point});
                previous = point;
            }
        }
        for(std::size_t ring = 0; ring < rings; ++ring)
            for(std::size_t spoke = 0; spoke < spokes; ++spoke)
                edges.push_back({
                    ringPoints[ring][spoke],
                    ringPoints[ring][(spoke + 1) % spokes]});
        return edges;
    }

    inline FracturePattern GenerateFracturePattern(
        FracturePatternSettings settings)
    {
        settings.pieceCount = std::clamp<std::size_t>(
            settings.pieceCount, 1, MaximumFracturePieces);
        settings.impactPoint.x = std::clamp(settings.impactPoint.x, 0.0f, 1.0f);
        settings.impactPoint.y = std::clamp(settings.impactPoint.y, 0.0f, 1.0f);
        SeededFractureRandom random(settings.seed);
        std::vector<FracturePoint> sites;
        sites.reserve(settings.pieceCount);
        sites.push_back(settings.impactPoint);

        // Place early sites on jittered rings. Their Voronoi boundaries make
        // the resulting cells visibly radiate from the impact while the
        // remaining seeded sites retain irregular glass-like breakup.
        constexpr float TwoPi = 6.28318530717958647692f;
        const std::size_t spokes = std::clamp<std::size_t>(
            settings.spokeCount, 3, 32);
        const std::size_t rings = std::clamp<std::size_t>(
            settings.ringCount, 1, 8);
        for(std::size_t ring = 1;
            ring <= rings && sites.size() < settings.pieceCount; ++ring)
        {
            for(std::size_t spoke = 0;
                spoke < spokes && sites.size() < settings.pieceCount; ++spoke)
            {
                const float angle = TwoPi * static_cast<float>(spoke) /
                    static_cast<float>(spokes) +
                    random.Signed() * settings.jitter * 0.22f;
                const float radius = 0.46f * static_cast<float>(ring) /
                    static_cast<float>(rings) *
                    (1.0f + random.Signed() * settings.jitter * 0.24f);
                sites.push_back({
                    std::clamp(settings.impactPoint.x + std::cos(angle) * radius,
                               0.002f, 0.998f),
                    std::clamp(settings.impactPoint.y + std::sin(angle) * radius,
                               0.002f, 0.998f)});
            }
        }
        while(sites.size() < settings.pieceCount)
            sites.push_back({0.002f + random.Unit() * 0.996f,
                             0.002f + random.Unit() * 0.996f});

        FracturePattern pattern;
        pattern.cells.reserve(sites.size());
        for(std::size_t siteIndex = 0; siteIndex < sites.size(); ++siteIndex)
        {
            std::vector<FracturePoint> polygon{
                {0.0f, 0.0f}, {1.0f, 0.0f},
                {1.0f, 1.0f}, {0.0f, 1.0f}};
            for(std::size_t otherIndex = 0;
                otherIndex < sites.size() && polygon.size() >= 3; ++otherIndex)
            {
                if(otherIndex == siteIndex)
                    continue;
                polygon = ClipFracturePolygonToBisector(
                    polygon, sites[siteIndex], sites[otherIndex]);
            }
            if(polygon.size() >= 3 && FracturePolygonArea(polygon) > 0.0000001f)
                pattern.cells.push_back({sites[siteIndex], std::move(polygon)});
        }
        pattern.edges = UniqueFractureEdges(pattern.cells);
        pattern.radialEdges = GenerateRadialCracks(settings);
        return pattern;
    }

    inline std::vector<std::size_t> PartitionFractureRevealGroups(
        const std::vector<FractureEdge>& edges,
        const std::vector<FracturePoint>& impacts)
    {
        std::vector<std::size_t> groups(edges.size(), 0);
        if(impacts.empty())
            return groups;
        for(std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
        {
            const FracturePoint middle{
                (edges[edgeIndex].from.x + edges[edgeIndex].to.x) * 0.5f,
                (edges[edgeIndex].from.y + edges[edgeIndex].to.y) * 0.5f};
            float nearestDistance = std::numeric_limits<float>::max();
            for(std::size_t impactIndex = 0; impactIndex < impacts.size(); ++impactIndex)
            {
                const float x = middle.x - impacts[impactIndex].x;
                const float y = middle.y - impacts[impactIndex].y;
                const float distance = x * x + y * y;
                if(distance < nearestDistance)
                {
                    nearestDistance = distance;
                    groups[edgeIndex] = impactIndex;
                }
            }
        }
        return groups;
    }

    /// Bilinear interpolation gives every interior vertex a continuous blend
    /// of the four independently authored corner offsets. This avoids seams
    /// and makes arbitrary quadrilaterals deterministic under direct seeking.
    inline DeformationVector BilinearCornerOffset(
        const CornerWarpSettings& warp,
        float u,
        float v)
    {
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);
        const auto blend = [](float a, float b, float amount)
        {
            return a + (b - a) * amount;
        };
        const auto& bottomLeft = warp.corners[0];
        const auto& topLeft = warp.corners[1];
        const auto& topRight = warp.corners[2];
        const auto& bottomRight = warp.corners[3];
        return {
            blend(blend(bottomLeft.x, bottomRight.x, u),
                  blend(topLeft.x, topRight.x, u), v),
            blend(blend(bottomLeft.y, bottomRight.y, u),
                  blend(topLeft.y, topRight.y, u), v),
            blend(blend(bottomLeft.z, bottomRight.z, u),
                  blend(topLeft.z, topRight.z, u), v)};
    }

    /// Returns the post-curvature flag displacement. The anchored edge is
    /// exactly stationary and anchorRamp controls how quickly motion reaches
    /// full amplitude across the canvas.
    inline DeformationVector FlagWaveOffset(
        const FlagWaveSettings& wave,
        float u,
        double clockSeconds)
    {
        if(!wave.enabled || !std::isfinite(clockSeconds))
            return {};
        constexpr double TwoPi = 6.28318530717958647692;
        u = std::clamp(u, 0.0f, 1.0f);
        const float distanceFromAnchor = wave.anchoredEdge == WaveAnchorEdge::Left
            ? u : 1.0f - u;
        const float rampExponent = std::max(0.01f, wave.anchorRamp);
        const float amplitude = std::pow(distanceFromAnchor, rampExponent);
        const double phase =
            static_cast<double>(wave.phaseOffset) +
            (static_cast<double>(u) * wave.cyclesAcross +
             clockSeconds * wave.speedCyclesPerSecond) * TwoPi;
        return {
            0.0f,
            static_cast<float>(std::cos(phase)) * wave.ripple * amplitude,
            static_cast<float>(std::sin(phase)) * wave.depth * amplitude};
    }

    /// Auto-cover crops inward just enough to cover the largest in-plane
    /// extent created by corner offsets or ripple. It never zooms below one,
    /// so switching modes cannot expose pixels beyond the source texture.
    inline float DeformationAutoCoverScale(
        float frameWidth,
        float frameHeight,
        const SurfaceDeformationSettings& deformation)
    {
        if(!deformation.enabled ||
           deformation.fillMode != DeformationFillMode::AutoZoomCover)
            return 1.0f;
        frameWidth = std::max(0.0001f, frameWidth);
        frameHeight = std::max(0.0001f, frameHeight);
        float minimumX = -frameWidth * 0.5f;
        float maximumX = frameWidth * 0.5f;
        float minimumY = -frameHeight * 0.5f;
        float maximumY = frameHeight * 0.5f;
        constexpr std::array<float, 4> U{0.0f, 0.0f, 1.0f, 1.0f};
        constexpr std::array<float, 4> V{0.0f, 1.0f, 1.0f, 0.0f};
        for(std::size_t index = 0; index < 4; ++index)
        {
            const auto& offset = deformation.cornerWarp.corners[index];
            const float x = (U[index] - 0.5f) * frameWidth + offset.x;
            const float y = (V[index] - 0.5f) * frameHeight + offset.y;
            minimumX = std::min(minimumX, x);
            maximumX = std::max(maximumX, x);
            minimumY = std::min(minimumY, y);
            maximumY = std::max(maximumY, y);
        }
        const float rippleExtent = deformation.wave.enabled
            ? std::abs(deformation.wave.ripple) * 2.0f : 0.0f;
        return std::max({
            1.0f,
            (maximumX - minimumX) / frameWidth,
            (maximumY - minimumY + rippleExtent) / frameHeight});
    }

    struct FrameRateStats {
        double minimumFps = 0.0;
        double averageFps = 0.0;
        double maximumFps = 0.0;
        std::uint64_t sampledFrames = 0;
    };

    /// Gameplay statistics describe only the playable body of a map. The
    /// caller latches samplingFinished after crossing the final note so replay
    /// seeks or results animations cannot reopen the measurement window.
    inline bool ShouldSampleGameplayFrame(
        double songTimeSeconds,
        std::optional<double> lastNoteTimeSeconds,
        bool samplingFinished)
    {
        return !samplingFinished &&
               songTimeSeconds >= 10.0 &&
               (!lastNoteTimeSeconds || songTimeSeconds <= *lastNoteTimeSeconds);
    }

    /// Summarizes accepted Unity frame durations. The average is derived from
    /// total frames / total elapsed time rather than averaging instantaneous
    /// rates, which would over-weight short fast frames and hide stutters.
    inline FrameRateStats SummarizeFrameRate(
        double minimumFrameSeconds,
        double maximumFrameSeconds,
        double totalFrameSeconds,
        std::uint64_t sampledFrames)
    {
        if(sampledFrames == 0 || minimumFrameSeconds <= 0.0 ||
           maximumFrameSeconds <= 0.0 || totalFrameSeconds <= 0.0)
            return {};
        return {
            1.0 / maximumFrameSeconds,
            static_cast<double>(sampledFrames) / totalFrameSeconds,
            1.0 / minimumFrameSeconds,
            sampledFrames};
    }

    /// Calculates the picture's uncropped size inside a fixed screen frame.
    /// Rotation is intentionally absent: rotating media must never swap or
    /// mutate the saved frame dimensions. Clipping happens after this result.
    inline VideoContentSize FitVideoContent(
        float frameWidth,
        float frameHeight,
        float sourceAspectRatio,
        bool stretchToFit,
        float zoom)
    {
        frameWidth = std::max(frameWidth, 0.0001f);
        frameHeight = std::max(frameHeight, 0.0001f);
        sourceAspectRatio = std::max(sourceAspectRatio, 0.0001f);
        zoom = std::clamp(zoom, 0.5f, 3.0f);
        float width = frameWidth;
        float height = frameHeight;
        if(!stretchToFit)
        {
            const float frameAspect = frameWidth / frameHeight;
            if(sourceAspectRatio > frameAspect)
                height = frameWidth / sourceAspectRatio;
            else
                width = frameHeight * sourceAspectRatio;
        }
        return {width * zoom, height * zoom};
    }

    /// Keeps the decoded-picture mesh far enough in front of its backing mesh
    /// for the headset depth buffer to distinguish them. A fixed separation
    /// worked for the original screen sizes, but large/distant canvases lost
    /// depth precision and exposed rapidly flickering pieces of the black
    /// backing surface. Scaling by physical canvas size preserves the same
    /// small proportional separation for every supported layout.
    inline float VideoLayerOffset(float frameWidth, float frameHeight)
    {
        const float largestDimension = std::max(
            std::abs(frameWidth), std::abs(frameHeight));
        return -std::max(0.015f, largestDimension * 0.002f);
    }

    /// The full-frame background supplies black letterboxing only where it is
    /// actually visible. Transparent layouts remove it altogether, and an
    /// untransformed picture that covers the complete canvas does not need a
    /// second coplanar surface behind it. A requested black lead-in is the sole
    /// deliberate exception and always covers the complete frame.
    inline constexpr bool ScreenBackgroundVisible(
        bool opaqueScreenBody,
        bool transparent,
        bool blackLeadInActive,
        bool videoCoversFrame = false,
        bool authoredAlphaCutout = false)
    {
        // A mapper-authored vignette supplies the final alpha-defined screen
        // shape. It must take precedence over the normal opaque-screen body;
        // otherwise that independent rectangular body remains visible as a
        // black box around the oval vignette. A deliberate black lead-in is
        // the only state that temporarily covers the complete frame.
        if(blackLeadInActive)
            return true;
        if(authoredAlphaCutout)
            return false;
        return opaqueScreenBody || (!transparent && !videoCoversFrame);
    }

    /// Matches PC Cinema's mapper-bloom normalization. Cinema accepts a
    /// fractional intensity and clamps authored values to the 0..2 range
    /// before deriving the camera-specific blur boost.
    inline constexpr float CinemaBloomIntensity(float authoredValue)
    {
        return std::clamp(authoredValue, 0.0f, 2.0f);
    }

    /// Pure counterpart of Cinema's camera-relative bloom boost. Keeping the
    /// calculation independent from Unity makes its safety limits and PC
    /// compatibility testable on the host before the render callback runs.
    inline float CinemaBloomBoost(
        float screenWidth,
        float screenHeight,
        float cameraDistance,
        float cameraFieldOfView,
        float cameraAngle,
        float authoredIntensity)
    {
        constexpr float BaseFactor = 0.045f;
        constexpr float CenterAttenuationFloor = 0.3f;
        const float area = std::max(
            0.0001f,
            std::abs(screenWidth * screenHeight));
        const float distance = std::max(0.0001f, cameraDistance);
        const float fieldOfView = std::max(0.0001f, cameraFieldOfView);
        float boost = BaseFactor /
            std::sqrt(std::sqrt(area) / distance);
        boost *= std::sqrt(CinemaBloomIntensity(authoredIntensity));
        const float attenuation = std::max(
            CenterAttenuationFloor,
            std::max(0.0f, cameraAngle) / (fieldOfView * 0.5f));
        boost /= attenuation + (1.0f - CenterAttenuationFloor);
        boost *= fieldOfView / 100.0f;
        return std::isfinite(boost) ? boost : 0.0f;
    }

    /// A Library preview normally holds Beat Saber's audio until FFmpeg has
    /// uploaded the picture for the requested song position. Negative media
    /// time and a configured post-roll are different: neither interval is
    /// supposed to produce a decoded frame. Treat both as presentation-ready
    /// so audio can advance instead of waiting forever for an impossible frame.
    inline constexpr bool SynchronizedPreviewReady(
        double mediaTimeSeconds,
        bool mediaPastConfiguredEnd,
        bool firstFrameUploaded)
    {
        return mediaTimeSeconds < 0.0 || mediaPastConfiguredEnd ||
            firstFrameUploaded;
    }

    /// Deterministic filesystem key. FNV-1a is not used for security; the full
    /// level ID remains the authoritative manifest key.
    inline std::string StableVideoKey(const std::string& levelId)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for(const unsigned char value : levelId)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        std::ostringstream text;
        text << std::hex << std::setw(16) << std::setfill('0') << hash;
        return text.str();
    }

    inline bool SafeSingleFilename(const std::string& value)
    {
        if(value.empty() || value == "." || value == "..") return false;
        return value.find('/') == std::string::npos &&
               value.find('\\') == std::string::npos &&
               value.find(':') == std::string::npos;
    }

    /// Accept only HTTPS YouTube addresses. Map metadata is untrusted input,
    /// so the downloader must never become a general-purpose URL fetcher or
    /// accept user-info/port confusion.
    inline bool IsSupportedYouTubeUrl(std::string_view value)
    {
        while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.remove_prefix(1);
        while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.remove_suffix(1);

        constexpr std::string_view scheme = "https://";
        if(value.size() <= scheme.size())
            return false;
        for(std::size_t i = 0; i < scheme.size(); ++i)
        {
            if(std::tolower(static_cast<unsigned char>(value[i])) != scheme[i])
                return false;
        }

        value.remove_prefix(scheme.size());
        const auto hostEnd = value.find_first_of("/?#");
        auto host = value.substr(0, hostEnd);
        if(host.empty() || host.find('@') != std::string_view::npos ||
           host.find(':') != std::string_view::npos)
            return false;

        std::string lowercaseHost(host);
        std::transform(lowercaseHost.begin(), lowercaseHost.end(), lowercaseHost.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

        const auto isHostOrSubdomain = [&lowercaseHost](std::string_view domain)
        {
            return lowercaseHost == domain ||
                (lowercaseHost.size() > domain.size() &&
                 lowercaseHost.ends_with(std::string(".") + std::string(domain)));
        };
        return isHostOrSubdomain("youtube.com") ||
               isHostOrSubdomain("youtu.be") ||
               isHostOrSubdomain("youtube-nocookie.com");
    }

    inline bool IsValidYouTubeVideoId(std::string_view value)
    {
        if(value.size() != 11)
            return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char character)
        {
            return std::isalnum(character) || character == '-' || character == '_';
        });
    }

    /// Returns the next temporary presentation limit for Automatic
    /// Performance. The saved menu value is a ceiling, so the first reduction
    /// begins below the video's effective source cadence rather than walking
    /// through limits that would not alter presentation. Mapper Fit-to-Song
    /// speed is part of that cadence. The caller-selected step is applied to
    /// that effective ceiling, so a 24 FPS source with a five-FPS step begins
    /// at 19 rather than spending several response windows on no-op caps.
    inline int EffectivePerformanceFpsLimit(
        int currentLimit,
        double sourceFramesPerSecond,
        double playbackRate)
    {
        constexpr int MinimumFps = 15;
        int effectiveLimit = std::max(MinimumFps, currentLimit);
        if(std::isfinite(sourceFramesPerSecond) &&
           std::isfinite(playbackRate) &&
           sourceFramesPerSecond > 0.0 && playbackRate > 0.0)
        {
            effectiveLimit = std::min(
                effectiveLimit,
                std::max(
                    MinimumFps,
                    static_cast<int>(std::ceil(
                        sourceFramesPerSecond * playbackRate - 0.000001))));
        }
        return effectiveLimit;
    }

    inline std::pair<bool, int> NextPerformanceFpsLimit(
        int currentLimit,
        double sourceFramesPerSecond,
        double playbackRate,
        int stepFramesPerSecond = 5)
    {
        constexpr int MinimumFps = 15;
        if(currentLimit <= MinimumFps)
            return {false, currentLimit};
        const int effectiveLimit = EffectivePerformanceFpsLimit(
            currentLimit,
            sourceFramesPerSecond,
            playbackRate);
        if(effectiveLimit <= MinimumFps)
            return {false, currentLimit};
        const int step = std::clamp(stepFramesPerSecond, 1, 5);
        const int nextLimit = std::max(
            MinimumFps,
            effectiveLimit - step);
        if(nextLimit >= currentLimit)
            return {false, currentLimit};
        return {true, nextLimit};
    }

    /// Records the exact path Automatic Performance used while lowering
    /// quality. Recovery reads this small stack backwards instead of trying to
    /// infer the user's previous setting from the current tier. A 60 FPS cap
    /// can reach the 15 FPS floor in forty-five one-FPS reductions, so a fixed
    /// array covers every configurable ladder without allocating in Beat
    /// Saber's gameplay loop.
    class AutomaticPerformanceHistory final {
    public:
        void Reset() noexcept { size_ = 0; }
        bool Empty() const noexcept { return size_ == 0; }
        std::size_t Size() const noexcept { return size_; }

        bool RecordReduction(int previousFps) noexcept
        {
            if(size_ >= tiers_.size())
                return false;
            tiers_[size_++] = previousFps;
            return true;
        }

        std::optional<int> RecoveryTarget() const noexcept
        {
            if(Empty())
                return std::nullopt;
            return tiers_[size_ - 1];
        }

        bool CommitRecovery() noexcept
        {
            if(Empty())
                return false;
            --size_;
            return true;
        }

    private:
        std::array<int, 45> tiers_{};
        std::size_t size_ = 0;
    };

    /// Estimates how many distinct source images should have been displayed
    /// during an interval. Output requests above the media's effective cadence
    /// are intentional frame reuse, not decoder misses. playbackRate converts
    /// source cadence into Beat Saber's song-clock domain.
    inline double ExpectedPresentationRate(
        double sourceFramesPerSecond,
        double playbackRate,
        int outputFramesPerSecond)
    {
        if(sourceFramesPerSecond <= 0.0 || playbackRate <= 0.0 ||
           outputFramesPerSecond <= 0)
            return 0.0;
        const double effectiveSourceRate = sourceFramesPerSecond * playbackRate;
        return std::min(
            effectiveSourceRate,
            static_cast<double>(outputFramesPerSecond));
    }

    inline std::uint64_t ExpectedPresentedFrames(
        double activeSongSeconds,
        double sourceFramesPerSecond,
        double playbackRate,
        int outputFramesPerSecond)
    {
        if(activeSongSeconds <= 0.0)
            return 0;
        const double expectedRate = ExpectedPresentationRate(
            sourceFramesPerSecond, playbackRate, outputFramesPerSecond);
        return static_cast<std::uint64_t>(std::floor(
            activeSongSeconds * expectedRate + 0.000001));
    }

    /// Advances a source-aware presentation clock without losing fractional
    /// deadlines between Unity updates. Repeatedly flooring each short frame
    /// delta would produce zero at common Quest refresh rates, so the caller
    /// owns the fractional remainder for the lifetime of its measurement
    /// window. The returned count represents pictures whose output deadlines
    /// elapsed, independent of media PTS spacing or decoder scheduling.
    inline std::uint64_t AccumulatePresentationDeadlines(
        double elapsedSongSeconds,
        double sourceFramesPerSecond,
        double playbackRate,
        int outputFramesPerSecond,
        double& fractionalDeadline)
    {
        if(!std::isfinite(elapsedSongSeconds) || elapsedSongSeconds <= 0.0)
            return 0;
        const double expectedRate = ExpectedPresentationRate(
            sourceFramesPerSecond,
            playbackRate,
            outputFramesPerSecond);
        if(expectedRate <= 0.0)
            return 0;

        const double accumulated = std::max(0.0, fractionalDeadline) +
            elapsedSongSeconds * expectedRate;
        const auto completed = static_cast<std::uint64_t>(
            std::floor(accumulated + 0.000001));
        fractionalDeadline = std::max(
            0.0,
            accumulated - static_cast<double>(completed));
        return completed;
    }

    /// Converts elapsed output deadlines into the comparison shown to users.
    /// One deadline may legitimately be in flight between the decoder worker
    /// and the following Unity upload. Excluding that single picture prevents
    /// a healthy reactive pipeline from reporting a permanent one-frame loss,
    /// while every older undelivered deadline remains visible. Startup frames
    /// can make uploads briefly exceed deadlines, so expected is never allowed
    /// to be lower than the number of pictures already presented.
    inline std::uint64_t ReportablePresentationDeadlines(
        std::uint64_t elapsedDeadlines,
        std::uint64_t presentedFrames,
        std::uint64_t inFlightAllowance = 1)
    {
        const auto dueDeadlines = elapsedDeadlines > inFlightAllowance
            ? elapsedDeadlines - inFlightAllowance
            : 0;
        return std::max(dueDeadlines, presentedFrames);
    }

    /// Accumulates completed presentation deadlines that did not have a new
    /// picture available. This is deliberately stateful: expected-presented is
    /// only the decoder's current backlog and can shrink when a late picture
    /// arrives. A user-facing "Frames Skipped" total must instead remember a
    /// missed deadline permanently and therefore can never count backwards.
    ///
    /// Pictures uploaded before their deadline are retained as credits. A
    /// picture uploaded after a missed deadline can satisfy a later deadline,
    /// but it cannot erase the deadline that was already missed.
    class PresentationMissAccumulator final {
    public:
        void Reset() noexcept
        {
            observedDeadlines_ = 0;
            observedPresentedFrames_ = 0;
            availablePictures_ = 0;
            missedDeadlines_ = 0;
        }

        std::uint64_t Observe(
            std::uint64_t reportableDeadlines,
            std::uint64_t presentedFrames) noexcept
        {
            // Session counters normally only increase. Treat a lower value as
            // an explicit measurement reset rather than allowing unsigned
            // subtraction to turn it into an enormous false miss count.
            if(reportableDeadlines < observedDeadlines_ ||
               presentedFrames < observedPresentedFrames_)
            {
                Reset();
            }

            availablePictures_ +=
                presentedFrames - observedPresentedFrames_;
            const auto newDeadlines =
                reportableDeadlines - observedDeadlines_;
            const auto matchedPictures = std::min(
                newDeadlines,
                availablePictures_);
            missedDeadlines_ += newDeadlines - matchedPictures;
            availablePictures_ -= matchedPictures;
            observedDeadlines_ = reportableDeadlines;
            observedPresentedFrames_ = presentedFrames;
            return missedDeadlines_;
        }

        std::uint64_t MissedDeadlines() const noexcept
        {
            return missedDeadlines_;
        }

    private:
        std::uint64_t observedDeadlines_ = 0;
        std::uint64_t observedPresentedFrames_ = 0;
        std::uint64_t availablePictures_ = 0;
        std::uint64_t missedDeadlines_ = 0;
    };

    inline double MissedFramePercent(
        std::uint64_t expectedFrames,
        std::uint64_t presentedFrames)
    {
        if(expectedFrames == 0 || presentedFrames >= expectedFrames)
            return 0.0;
        return 100.0 * static_cast<double>(expectedFrames - presentedFrames) /
               static_cast<double>(expectedFrames);
    }

    enum class AutomaticPerformanceDecision {
        Wait,
        Reduce,
        Recover
    };

    /// Evaluates one bounded Automatic Performance measurement window. The
    /// previous implementation restarted its timer every time the live
    /// percentage crossed the threshold. Normal frame-by-frame variation could
    /// therefore postpone a 0.5-second attack for more than a minute. A window
    /// now remains intact until the duration for its current aggregate result
    /// has elapsed, then the caller acts and starts a clean window.
    inline AutomaticPerformanceDecision EvaluateAutomaticPerformanceWindow(
        double elapsedSeconds,
        double missedFramePercent,
        double thresholdPercent,
        double attackSeconds,
        double releaseSeconds)
    {
        if(!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
           !std::isfinite(missedFramePercent) ||
           !std::isfinite(thresholdPercent) ||
           !std::isfinite(attackSeconds) ||
           !std::isfinite(releaseSeconds))
            return AutomaticPerformanceDecision::Wait;

        const bool overloaded = missedFramePercent >= thresholdPercent;
        const double requiredSeconds = std::max(
            0.0,
            overloaded ? attackSeconds : releaseSeconds);
        if(elapsedSeconds + 0.000001 < requiredSeconds)
            return AutomaticPerformanceDecision::Wait;
        return overloaded
            ? AutomaticPerformanceDecision::Reduce
            : AutomaticPerformanceDecision::Recover;
    }

    /// Uses the shorter of Beat Saber's song duration and the loaded audio
    /// clip. Some custom levels report slightly different values for those two
    /// clocks, so waiting only for the map duration can leave a completed
    /// AudioSource in a terminal state. A small tolerance lets the menu loop
    /// before Unity tears down the channel at its exact final sample.
    inline bool PreviewReachedLoopBoundary(
        double songPositionSeconds,
        double songDurationSeconds,
        double audioClipDurationSeconds,
        double toleranceSeconds = 0.03)
    {
        const double songEnd = songDurationSeconds > 0.0
            ? songDurationSeconds
            : audioClipDurationSeconds;
        const double clipEnd = audioClipDurationSeconds > 0.0
            ? audioClipDurationSeconds
            : songDurationSeconds;
        const double playableEnd = std::min(songEnd, clipEnd);
        if(playableEnd <= 0.0)
            return false;
        return songPositionSeconds >=
            std::max(0.0, playableEnd - std::max(0.0, toleranceSeconds));
    }

    /// The circuit breaker counts internal failures, not error text. Two
    /// different failures close together are still evidence that the mod is
    /// unstable and should stop interacting with Beat Saber.
    inline bool IsSecondFailureWithin(
        std::chrono::steady_clock::duration elapsed,
        std::chrono::steady_clock::duration window = std::chrono::minutes(3))
    {
        return elapsed >= std::chrono::steady_clock::duration::zero() &&
               elapsed <= window;
    }

    /// Converts yt-dlp byte counters into a UI-safe progress fraction. The
    /// total can be unknown while YouTube is preparing a stream, and some
    /// servers briefly report estimates smaller than the bytes already read.
    inline float DownloadProgressFraction(
        std::uint64_t downloadedBytes,
        std::uint64_t totalBytes)
    {
        if(totalBytes == 0)
            return 0.0f;
        return std::clamp(
            static_cast<float>(downloadedBytes) /
                static_cast<float>(totalBytes),
            0.0f,
            1.0f);
    }
}
