#include "BigScreen/ScreenPreview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr int PreviewTextureWidth = 512;
        constexpr int PreviewTextureHeight = 288;

        VideoFrame MakePlacementPattern()
        {
            VideoFrame frame;
            frame.width = PreviewTextureWidth;
            frame.height = PreviewTextureHeight;
            frame.rgba.resize(
                static_cast<std::size_t>(PreviewTextureWidth) *
                PreviewTextureHeight *
                4);

            for(int y = 0; y < PreviewTextureHeight; ++y)
            {
                for(int x = 0; x < PreviewTextureWidth; ++x)
                {
                    const float u = x / static_cast<float>(PreviewTextureWidth - 1);
                    const float v = y / static_cast<float>(PreviewTextureHeight - 1);
                    const bool border =
                        x < 5 || x >= PreviewTextureWidth - 5 ||
                        y < 5 || y >= PreviewTextureHeight - 5;
                    const bool centerLine =
                        std::abs(x - PreviewTextureWidth / 2) <= 1 ||
                        std::abs(y - PreviewTextureHeight / 2) <= 1;
                    const bool grid = x % 64 <= 1 || y % 48 <= 1;
                    const auto offset =
                        (static_cast<std::size_t>(y) * PreviewTextureWidth + x) * 4;

                    // A high-contrast border makes the real physical extent
                    // obvious at a distance. The center cross and grid make
                    // curvature and orientation visible without pretending a
                    // small UI thumbnail represents the gameplay screen.
                    frame.rgba[offset + 0] = static_cast<std::uint8_t>(18 + 35 * u);
                    frame.rgba[offset + 1] = static_cast<std::uint8_t>(30 + 45 * v);
                    frame.rgba[offset + 2] = static_cast<std::uint8_t>(60 + 70 * u);
                    if(grid)
                    {
                        frame.rgba[offset + 0] = 35;
                        frame.rgba[offset + 1] = 105;
                        frame.rgba[offset + 2] = 155;
                    }
                    if(centerLine)
                    {
                        frame.rgba[offset + 0] = 245;
                        frame.rgba[offset + 1] = 245;
                        frame.rgba[offset + 2] = 250;
                    }
                    if(border)
                    {
                        frame.rgba[offset + 0] = 0;
                        frame.rgba[offset + 1] = 220;
                        frame.rgba[offset + 2] = 255;
                    }
                    frame.rgba[offset + 3] = 255;
                }
            }
            return frame;
        }
    }

    ScreenPreview& ScreenPreview::Instance()
    {
        static ScreenPreview preview;
        return preview;
    }

    void ScreenPreview::ActivateCurrentState()
    {
        // Capture the selected map before stopping its ordinary song-preview
        // playback. This lets the settings preview use that map's real X/Y/Z,
        // rotation, and mapper-authored base height. If no video map is
        // selected, MapVideoConfig's documented default placement is used.
        CaptureBasePlacement();
        PlaybackSession::Instance().Stop();

        const auto& settings = Settings::Instance();
        if(settings.ModEnabled())
            Refresh();
    }

    void ScreenPreview::SetEnabled(bool enabled)
    {
        enabled = enabled && Settings::Instance().ModEnabled();
        if(!enabled)
        {
            surface_.Destroy();
            return;
        }

        CaptureBasePlacement();
        PlaybackSession::Instance().Stop();
        Refresh();
    }

    void ScreenPreview::Refresh()
    {
        const auto& settings = Settings::Instance();
        if(!settings.ModEnabled())
        {
            surface_.Destroy();
            return;
        }

        CaptureBasePlacement();
        if(!CreateWorldScreen())
            PaperLogger.error("Could not create the full-size settings screen preview");
    }

    void ScreenPreview::Suspend()
    {
        surface_.Destroy();
        baseConfig_.reset();

        // Restore normal song-selection preview behavior when leaving Big
        // Screen's settings flow. This is a no-op when the master switch,
        // selected song, or song previews are disabled.
        SelectionVideoToggle::Instance().MenuPreviewPreferenceChanged();
    }

    void ScreenPreview::CaptureBasePlacement()
    {
        if(baseConfig_)
            return;

        const auto& prepared = PlaybackSession::Instance().PreparedBaseConfig();
        baseConfig_ = prepared ? *prepared : MapVideoConfig{};
    }

    bool ScreenPreview::CreateWorldScreen()
    {
        if(!baseConfig_)
            return false;

        const auto& settings = Settings::Instance();
        auto config = *baseConfig_;
        config.screenPosition.x += settings.ScreenHorizontalOffset();
        config.screenPosition.y += settings.ScreenVerticalOffset();
        config.screenPosition.z += settings.ScreenDistanceOffset();
        config.screenRotation.x += settings.ScreenTiltOffset();
        config.screenHeight *= settings.ScreenScale();
        config.screenCurvature = settings.CurvedScreenEnabled()
            ? settings.ScreenCurvature()
            : 0.0f;
        config.maintainAspectRatioWhenCurved =
            settings.CurvedScreenEnabled() &&
            settings.MaintainCurveAspectRatio();
        config.transparent = settings.TransparencyEnabled();

        const auto pattern = MakePlacementPattern();
        if(!surface_.Create(config, pattern.width, pattern.height))
            return false;
        if(!surface_.Upload(pattern))
        {
            surface_.Destroy();
            return false;
        }

        surface_.SetVisible(true);
        PaperLogger.info(
            "Showing full-size settings preview at ({:.2f}, {:.2f}, {:.2f}), tilt {:.1f}, height {:.2f}",
            config.screenPosition.x,
            config.screenPosition.y,
            config.screenPosition.z,
            config.screenRotation.x,
            config.screenHeight);
        return true;
    }
}
