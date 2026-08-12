#include "BigScreen/ScreenPreview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "HMUI/ImageView.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr int PreviewTextureWidth = 512;
        constexpr int PreviewTextureHeight = 288;
        constexpr float UiUnitsPerMeter = 50.0f;

        std::string AspectRatioLabel(float width, float height)
        {
            const float ratio = width / std::max(height, 0.0001f);
            struct CommonRatio { float value; const char* label; };
            constexpr CommonRatio common[] = {
                {16.0f / 9.0f, "16:9"}, {20.0f / 9.0f, "20:9"},
                {21.0f / 9.0f, "21:9"}, {4.0f / 3.0f, "4:3"},
                {3.0f / 2.0f, "3:2"}, {1.0f, "1:1"},
                {9.0f / 16.0f, "9:16"}};
            for(const auto& candidate : common)
                if(std::abs(ratio - candidate.value) < 0.025f)
                    return candidate.label;
            return std::format("{:.2f}:1", ratio);
        }

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
            DestroyEditorUi();
            surface_.Destroy();
            return;
        }

        CaptureBasePlacement();
        PlaybackSession::Instance().Stop();
        Refresh();
    }

    void ScreenPreview::Refresh()
    {
        // Any ordinary setting change ends an in-progress free-placement
        // transaction. Unsaved controller movement must never be mixed with a
        // layout switch, reset, Chroma toggle, or geometry slider callback.
        DestroyEditorUi();
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
        DestroyEditorUi();
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
        const auto& layout = settings.ActiveLayout();
        if(settings.AdvancedOptionsEnabled() && layout.undocked)
        {
            config.screenPosition = {
                layout.undockedPositionX,
                layout.undockedPositionY,
                layout.undockedPositionZ};
            config.screenRotation = {
                layout.undockedRotationX,
                layout.undockedRotationY,
                layout.undockedRotationZ};
            config.screenHeight = layout.undockedHeight;
            config.screenWidthOverride = layout.undockedWidth;
        }
        else
        {
            config.screenPosition.x += settings.ScreenHorizontalOffset();
            config.screenPosition.y += settings.ScreenVerticalOffset();
            config.screenPosition.z += settings.ScreenDistanceOffset();
            config.screenRotation.x += settings.ScreenTiltOffset();
            if(settings.AdvancedOptionsEnabled())
                config.screenRotation.z += settings.ScreenRoll();
            config.screenHeight *= settings.ScreenScale();
            config.screenWidthOverride.reset();
        }
        config.screenCurvature = settings.CurvedScreenEnabled()
            ? settings.ScreenCurvature()
            : 0.0f;
        config.maintainAspectRatioWhenCurved =
            settings.CurvedScreenEnabled() &&
            settings.MaintainCurveAspectRatio();
        config.transparent = settings.TransparencyEnabled();
        if(settings.AdvancedOptionsEnabled())
        {
            config.videoRotation = settings.VideoRotation();
            config.videoZoom = settings.VideoZoom();
            config.videoOffsetX = settings.VideoOffsetX();
            config.videoOffsetY = settings.VideoOffsetY();
            config.videoTilt = settings.VideoTilt();
            config.stretchVideoToFit = settings.StretchVideoToFit();
        }

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

    void ScreenPreview::BeginUndockedEditing()
    {
        const auto& settings = Settings::Instance();
        if(!settings.ModEnabled() || !settings.AdvancedOptionsEnabled() ||
           !settings.UndockedScreenEnabled())
            return;

        DestroyEditorUi();
        Refresh();
        if(!baseConfig_)
            return;

        auto config = *baseConfig_;
        const auto& layout = settings.ActiveLayout();
        config.screenPosition = {
            layout.undockedPositionX,
            layout.undockedPositionY,
            layout.undockedPositionZ};
        config.screenRotation = {
            layout.undockedRotationX,
            layout.undockedRotationY,
            layout.undockedRotationZ};
        config.screenHeight = layout.undockedHeight;
        config.screenWidthOverride = layout.undockedWidth;
        config.screenCurvature = settings.CurvedScreenEnabled()
            ? settings.ScreenCurvature() : 0.0f;
        config.maintainAspectRatioWhenCurved =
            settings.CurvedScreenEnabled() &&
            settings.MaintainCurveAspectRatio();
        config.transparent = settings.TransparencyEnabled();
        config.videoRotation = settings.VideoRotation();
        config.videoZoom = settings.VideoZoom();
        config.videoOffsetX = settings.VideoOffsetX();
        config.videoOffsetY = settings.VideoOffsetY();
        config.videoTilt = settings.VideoTilt();
        config.stretchVideoToFit = settings.StretchVideoToFit();
        editorConfig_ = config;

        // The editable screen intentionally shows only its frame and controls.
        // Hiding ScreenSurface also removes its texture from the ray path and
        // makes placement safe over either menu panel while editing.
        surface_.SetVisible(false);
        if(!CreateEditorUi())
        {
            PaperLogger.error("Could not create the undocked screen editor");
            DestroyEditorUi();
            Refresh();
            return;
        }
        SettingsMenu::Instance().RefreshControls();
        PaperLogger.info("Started undocked editor for Layout {}",
            settings.ActiveScreenLayout() + 1);
    }

    bool ScreenPreview::CreateEditorUi()
    {
        if(!editorConfig_ || !editorConfig_->screenWidthOverride)
            return false;
        const float width = *editorConfig_->screenWidthOverride;
        const float height = editorConfig_->screenHeight;
        const auto rotation = UnityEngine::Quaternion::Euler({
            editorConfig_->screenRotation.x,
            editorConfig_->screenRotation.y,
            editorConfig_->screenRotation.z});

        editorScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
            {width * UiUnitsPerMeter, height * UiUnitsPerMeter},
            true,
            {editorConfig_->screenPosition.x,
             editorConfig_->screenPosition.y,
             editorConfig_->screenPosition.z},
            rotation,
            0.0f,
            false);
        if(!editorScreen_)
            return false;
        editorScreen_->get_gameObject()->set_name("Big Screen Undocked Editor");
        editorScreen_->set_HandleSide(BSML::Side::Full);
        editorScreen_->set_HighlightHandle(true);

        const auto blank = BSML::Utilities::ImageResources::GetBlankSprite();
        for(auto& border : editorBorders_)
        {
            border = BSML::Lite::CreateImage(editorScreen_->get_transform(), blank);
            if(border)
            {
                border->set_color({0.0f, 0.80f, 1.0f, 0.95f});
                border->set_preserveAspect(false);
                border->set_raycastTarget(false);
            }
        }

        editorInstructions_ = BSML::Lite::CreateText(
            editorScreen_->get_transform(),
            "Point at the frame and hold the trigger to move or rotate it.\nDrag the lower-right handle to resize. Save Screen finishes editing.",
            TMPro::FontStyles::Normal, 4.0f);
        if(editorInstructions_)
        {
            editorInstructions_->set_alignment(TMPro::TextAlignmentOptions::Center);
            editorInstructions_->set_enableWordWrapping(true);
            editorInstructions_->set_raycastTarget(false);
        }
        editorAspectText_ = BSML::Lite::CreateText(
            editorScreen_->get_transform(), "", TMPro::FontStyles::Bold, 5.0f);
        if(editorAspectText_)
        {
            editorAspectText_->set_alignment(TMPro::TextAlignmentOptions::Center);
            editorAspectText_->set_raycastTarget(false);
        }
        editorSaveButton_ = BSML::Lite::CreateUIButton(
            editorScreen_->get_transform(), "Save Screen",
            {0.0f, 0.0f}, {34.0f, 8.0f},
            []() { ScreenPreview::Instance().SaveUndockedEditing(); });

        resizeHandleScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
            {8.0f, 8.0f}, true,
            editorScreen_->get_transform()->get_position(),
            editorScreen_->get_transform()->get_rotation(),
            0.0f, false);
        if(!resizeHandleScreen_)
            return false;
        resizeHandleScreen_->get_gameObject()->set_name(
            "Big Screen Undocked Resize Handle");
        resizeHandleScreen_->set_HandleSide(BSML::Side::Full);
        resizeHandleScreen_->set_HighlightHandle(true);
        if(auto* marker = BSML::Lite::CreateImage(
               resizeHandleScreen_->get_transform(), blank,
               {0.0f, 0.0f}, {7.0f, 7.0f}))
        {
            marker->set_color({0.0f, 0.80f, 1.0f, 1.0f});
            marker->set_preserveAspect(false);
            marker->set_raycastTarget(false);
        }

        UpdateEditorOverlayLayout();
        PlaceResizeHandle();
        return true;
    }

    void ScreenPreview::UpdateEditorOverlayLayout()
    {
        if(!editorScreen_ || !editorConfig_ ||
           !editorConfig_->screenWidthOverride)
            return;
        const float width =
            *editorConfig_->screenWidthOverride * UiUnitsPerMeter;
        const float height = editorConfig_->screenHeight * UiUnitsPerMeter;
        editorScreen_->set_ScreenSize({width, height});

        const auto setBorder = [](HMUI::ImageView* border,
                                  UnityEngine::Vector2 position,
                                  UnityEngine::Vector2 size)
        {
            if(!border)
                return;
            auto rect = border->get_transform().cast<UnityEngine::RectTransform>();
            rect->set_anchoredPosition(position);
            rect->set_sizeDelta(size);
        };
        constexpr float thickness = 1.5f;
        setBorder(editorBorders_[0], {0.0f, height * 0.5f}, {width, thickness});
        setBorder(editorBorders_[1], {0.0f, -height * 0.5f}, {width, thickness});
        setBorder(editorBorders_[2], {-width * 0.5f, 0.0f}, {thickness, height});
        setBorder(editorBorders_[3], {width * 0.5f, 0.0f}, {thickness, height});

        if(editorInstructions_)
        {
            auto rect = editorInstructions_->get_rectTransform();
            rect->set_anchoredPosition({0.0f, height * 0.18f});
            rect->set_sizeDelta({std::max(40.0f, width - 12.0f), 24.0f});
        }
        if(editorAspectText_)
        {
            editorAspectText_->set_text(
                "Screen Aspect Ratio  " + AspectRatioLabel(width, height));
            auto rect = editorAspectText_->get_rectTransform();
            rect->set_anchoredPosition({0.0f, -height * 0.04f});
            rect->set_sizeDelta({std::max(36.0f, width - 12.0f), 10.0f});
        }
        if(editorSaveButton_)
        {
            auto rect = editorSaveButton_->get_transform()
                .cast<UnityEngine::RectTransform>();
            rect->set_anchoredPosition({0.0f, -height * 0.22f});
            rect->set_sizeDelta({34.0f, 8.0f});
        }
    }

    void ScreenPreview::PlaceResizeHandle()
    {
        if(!editorScreen_ || !resizeHandleScreen_ || !editorConfig_ ||
           !editorConfig_->screenWidthOverride)
            return;
        const float halfWidth =
            *editorConfig_->screenWidthOverride * UiUnitsPerMeter * 0.5f;
        const float halfHeight =
            editorConfig_->screenHeight * UiUnitsPerMeter * 0.5f;
        resizeHandleScreen_->get_transform()->SetPositionAndRotation(
            editorScreen_->get_transform()->TransformPoint(
                {halfWidth, -halfHeight, -0.5f}),
            editorScreen_->get_transform()->get_rotation());
    }

    void ScreenPreview::TickUndockedEditor()
    {
        if(!editorScreen_ || !resizeHandleScreen_ || !editorConfig_)
            return;
        if(!Settings::Instance().ModEnabled() ||
           !Settings::Instance().AdvancedOptionsEnabled() ||
           !Settings::Instance().UndockedScreenEnabled())
        {
            CancelUndockedEditing();
            return;
        }

        auto* resizeHandle = resizeHandleScreen_->handle
            ? resizeHandleScreen_->handle
                  ->GetComponent<BSML::FloatingScreenHandle*>()
            : nullptr;
        const bool resizing = resizeHandle && resizeHandle->_grabbingController;
        if(resizing)
        {
            const auto local = editorScreen_->get_transform()
                ->InverseTransformPoint(
                    resizeHandleScreen_->get_transform()->get_position());
            const float width = std::clamp(
                std::abs(local.x) * 2.0f / UiUnitsPerMeter, 0.5f, 50.0f);
            const float height = std::clamp(
                std::abs(local.y) * 2.0f / UiUnitsPerMeter, 0.5f, 50.0f);
            if(std::abs(width - *editorConfig_->screenWidthOverride) > 0.005f ||
               std::abs(height - editorConfig_->screenHeight) > 0.005f)
            {
                editorConfig_->screenWidthOverride = width;
                editorConfig_->screenHeight = height;
                UpdateEditorOverlayLayout();
                surface_.UpdateGeometry(*editorConfig_);
            }
        }

        surface_.SetWorldTransform(
            editorScreen_->get_transform()->get_position(),
            editorScreen_->get_transform()->get_rotation());
        if(!resizing)
            PlaceResizeHandle();
    }

    void ScreenPreview::SaveUndockedEditing()
    {
        if(!editorScreen_ || !editorConfig_ ||
           !editorConfig_->screenWidthOverride)
            return;
        const auto position = editorScreen_->get_transform()->get_position();
        const auto rotation = editorScreen_->get_transform()
            ->get_rotation().get_eulerAngles();
        Settings::Instance().SaveUndockedScreen(
            position.x, position.y, position.z,
            rotation.x, rotation.y, rotation.z,
            *editorConfig_->screenWidthOverride,
            editorConfig_->screenHeight);
        DestroyEditorUi();
        Refresh();
        SettingsMenu::Instance().RefreshControls();
        PaperLogger.info("Saved undocked screen placement");
    }

    void ScreenPreview::CancelUndockedEditing()
    {
        if(!editorScreen_ && !resizeHandleScreen_)
            return;
        DestroyEditorUi();
        Refresh();
        SettingsMenu::Instance().RefreshControls();
        PaperLogger.info("Cancelled unsaved undocked screen placement");
    }

    void ScreenPreview::DestroyEditorUi()
    {
        if(resizeHandleScreen_)
            UnityEngine::Object::Destroy(resizeHandleScreen_->get_gameObject());
        if(editorScreen_)
            UnityEngine::Object::Destroy(editorScreen_->get_gameObject());
        resizeHandleScreen_ = nullptr;
        editorScreen_ = nullptr;
        editorBorders_.fill(nullptr);
        editorInstructions_ = nullptr;
        editorAspectText_ = nullptr;
        editorSaveButton_ = nullptr;
        editorConfig_.reset();
    }
}
