#include "BigScreen/ScreenPreview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/ErrorManager.hpp"
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
#include "UnityEngine/MeshRenderer.hpp"
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
        // ScreenSurface intentionally places video pixels 1.5 cm in front of
        // its frame. A preview on exactly the editor's plane therefore covers
        // the world-space canvas and makes its handles appear unresponsive.
        // Two editor UI units equal 4 cm in this FloatingScreen transform,
        // leaving the moving preview visible just behind every edit control.
        constexpr float EditorVideoDepthOffsetUi = 2.0f;
        // The grip is 12 UI units wide. A 5.5-unit inset shifts it half a unit
        // right and down across the frame borders, producing a deliberate
        // overlap that makes the control look anchored to the corner.
        constexpr float ResizeHandleInsetUi = 5.5f;

        void CenterRect(UnityEngine::RectTransform* rect)
        {
            if(!rect)
                return;
            rect->set_anchorMin({0.5f, 0.5f});
            rect->set_anchorMax({0.5f, 0.5f});
            rect->set_pivot({0.5f, 0.5f});
        }

        void HideNativeHandleRenderer(BSML::FloatingScreen* screen)
        {
            if(!screen || !screen->handle)
                return;
            auto* renderer = screen->handle
                ->GetComponent<UnityEngine::MeshRenderer*>();
            // The native cube retains the proven movement collider and BSML
            // pointer handler. Only its renderer is hidden; Big Screen's cyan
            // UI bar supplies the visible target without touching the native
            // interaction state that previously worked on this headset.
            if(renderer)
                renderer->set_enabled(false);
        }

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

        auto& settings = Settings::Instance();
        if(settings.ModEnabled())
            Refresh();
    }

    void ScreenPreview::SetEnabled(bool enabled)
    {
        enabled = enabled && Settings::Instance().ModEnabled();
        if(!enabled)
        {
            Settings::Instance().CancelScreenEditTransaction();
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
        // While unlocked, Screen-tab changes are applied to the editor instead
        // of rebuilding (and therefore destroying) it. Save/Cancel owns the
        // lifetime and persistence boundary for the complete edit session.
        if(IsUndockedEditing())
        {
            RefreshUndockedEditingFromSettings();
            return;
        }
        const auto& settings = Settings::Instance();
        if(!settings.ModEnabled())
        {
            surface_.Destroy();
            return;
        }

        CaptureBasePlacement();
        if(!CreateWorldScreen())
        {
            PaperLogger.error("Could not create the full-size settings screen preview");
            ErrorManager::Instance().RecordError(
                "Creating the settings screen preview",
                "Unity could not create the full-size preview surface");
        }
    }

    void ScreenPreview::Suspend()
    {
        Settings::Instance().CancelScreenEditTransaction();
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

        auto& settings = Settings::Instance();
        auto config = *baseConfig_;
        // ScreenPreview owns a cached immutable mapper baseline so settings
        // can be adjusted after playback stops. When mapper control is off,
        // strip authored geometry before applying the selected layout; Reset
        // Screen must therefore return to the actual back-wall defaults.
        if(config.hasMapperPresentation && !settings.AllowChromaOverride())
            config.ResetPresentationToDefaults();
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
        config.letterboxTransparent = settings.AdvancedOptionsEnabled() &&
            settings.LetterboxTransparencyEnabled();
        config.videoOpacity = settings.VideoOpacity();
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
        auto& settings = Settings::Instance();
        if(!settings.ModEnabled() || !settings.AdvancedOptionsEnabled() ||
           !settings.UndockedScreenEnabled())
            return;

        // Position Screen remains visible beside Cancel Positioning. Ignore a
        // second activation instead of destroying and recreating an editor
        // the user may already be holding with a controller.
        if(IsUndockedEditing())
            return;

        const bool libraryPreviewActive =
            PlaybackSession::Instance().IsLibraryPreviewActive();
        if(libraryPreviewActive)
        {
            // The playing Video Library surface is the only preview that
            // should be visible while positioning. Do not create the separate
            // checkerboard settings surface behind it.
            surface_.Destroy();
            baseConfig_.reset();
            CaptureBasePlacement();
        }
        else
        {
            Refresh();
        }
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
        config.letterboxTransparent = settings.LetterboxTransparencyEnabled();
        config.videoOpacity = settings.VideoOpacity();
        config.videoRotation = settings.VideoRotation();
        config.videoZoom = settings.VideoZoom();
        config.videoOffsetX = settings.VideoOffsetX();
        config.videoOffsetY = settings.VideoOffsetY();
        config.videoTilt = settings.VideoTilt();
        config.stretchVideoToFit = settings.StretchVideoToFit();
        editorConfig_ = config;
        editorAppliedLayout_ = layout;
        editorLayoutIndex_ = settings.ActiveScreenLayout();

        settings.BeginScreenEditTransaction();

        // The editable screen intentionally shows only its frame and controls.
        // Hiding ScreenSurface also removes its texture from the ray path and
        // makes placement safe over either menu panel while editing.
        surface_.SetVisible(false);
        if(!CreateEditorUi())
        {
            PaperLogger.error("Could not create the undocked screen editor");
            ErrorManager::Instance().RecordError(
                "Creating the undocked screen editor",
                "Unity could not create the screen editor controls");
            settings.CancelScreenEditTransaction();
            DestroyEditorUi();
            if(libraryPreviewActive)
                PlaybackSession::Instance().RestoreLibraryPreviewDisplay(false);
            else
                Refresh();
            return;
        }
        if(libraryPreviewActive)
        {
            if(!ApplyLibraryPreviewEditorDisplay(true))
            {
                PaperLogger.warn(
                    "Could not attach the active library preview to the screen editor");
            }
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
        // A Full handle is technically draggable, but BSML deliberately does
        // not render it. It also sits behind every editor control, making the
        // entire screen feel like one unexplained invisible hit target. A Top
        // handle gives movement one predictable grab region while leaving the
        // Save button and resize handle independent.
        editorScreen_->set_HandleSide(BSML::Side::Top);
        editorScreen_->set_HighlightHandle(true);

        // BSML 0.4.43's BlankSprite uses Texture2D::blackTexture. Multiplying
        // that sprite by cyan still produces black, so it cannot be used for
        // colored editor geometry. WhitePixel accepts the intended tint and
        // makes the frame/handles reliably cyan on Quest.
        const auto whitePixel =
            BSML::Utilities::ImageResources::GetWhitePixel();
        for(auto& border : editorBorders_)
        {
            border = BSML::Lite::CreateImage(
                editorScreen_->get_transform(), whitePixel);
            if(border)
            {
                border->set_color({0.0f, 0.80f, 1.0f, 0.95f});
                border->set_preserveAspect(false);
                border->set_raycastTarget(false);
                CenterRect(border->get_transform()
                    .cast<UnityEngine::RectTransform>());
            }
        }

        editorMoveBar_ = BSML::Lite::CreateImage(
            editorScreen_->get_transform(), whitePixel);
        if(editorMoveBar_)
        {
            // Cyan is reserved for the screen boundary. Warm amber identifies
            // editor parts that can actually be grabbed with the controller.
            editorMoveBar_->set_color({1.0f, 0.56f, 0.08f, 1.0f});
            editorMoveBar_->set_preserveAspect(false);
            editorMoveBar_->set_raycastTarget(false);
            CenterRect(editorMoveBar_->get_transform()
                .cast<UnityEngine::RectTransform>());
        }
        editorMoveText_ = BSML::Lite::CreateText(
            editorScreen_->get_transform(),
            "GRAB ORANGE BAR TO MOVE OR ROTATE",
            TMPro::FontStyles::Bold, 4.5f);
        if(editorMoveText_)
        {
            editorMoveText_->set_alignment(TMPro::TextAlignmentOptions::Center);
            editorMoveText_->set_enableWordWrapping(false);
            editorMoveText_->set_raycastTarget(false);
            CenterRect(editorMoveText_->get_rectTransform());
        }

        editorInstructions_ = BSML::Lite::CreateText(
            editorScreen_->get_transform(),
            "Hold the trigger on the orange bar to position the screen.\nDrag the orange corner grip to resize it.",
            TMPro::FontStyles::Normal, 6.0f);
        if(editorInstructions_)
        {
            editorInstructions_->set_alignment(TMPro::TextAlignmentOptions::Center);
            editorInstructions_->set_enableWordWrapping(true);
            editorInstructions_->set_raycastTarget(false);
            CenterRect(editorInstructions_->get_rectTransform());
        }
        editorAspectText_ = BSML::Lite::CreateText(
            editorScreen_->get_transform(), "", TMPro::FontStyles::Bold, 7.0f);
        if(editorAspectText_)
        {
            editorAspectText_->set_alignment(TMPro::TextAlignmentOptions::Center);
            editorAspectText_->set_raycastTarget(false);
            CenterRect(editorAspectText_->get_rectTransform());
        }
        editorSaveButton_ = BSML::Lite::CreateUIButton(
            editorScreen_->get_transform(), "Save Screen",
            {0.0f, 0.0f}, {40.0f, 10.0f},
            []() { ScreenPreview::Instance().SaveUndockedEditing(); });
        if(editorSaveButton_)
        {
            CenterRect(editorSaveButton_->get_transform()
                .cast<UnityEngine::RectTransform>());
            BSML::Lite::SetButtonTextSize(editorSaveButton_, 4.5f);
        }

        resizeHandleScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
            {14.0f, 14.0f}, true,
            editorScreen_->get_transform()->get_position(),
            editorScreen_->get_transform()->get_rotation(),
            0.0f, false);
        if(!resizeHandleScreen_)
            return false;
        resizeHandleScreen_->get_gameObject()->set_name(
            "Big Screen Undocked Resize Handle");
        // Reuse the exact native Top handle that successfully moves the main
        // screen. The small canvas is offset below the corner so this handle
        // lands directly on the visible resize marker. Full handles were not
        // reliably draggable on this Quest/BSML combination.
        resizeHandleScreen_->set_HandleSide(BSML::Side::Top);
        resizeHandleScreen_->set_HighlightHandle(true);
        if(resizeHandleScreen_->handle)
        {
            // BSML's normal Top handle is intentionally a thin strip. Expand
            // this native handle's BoxCollider to the full visible 13x13
            // marker and center it inside this helper canvas. Leaving a Top
            // handle at y=7 put half of the visible marker and its label beyond
            // the RectMask2D boundary, which made the text appear clipped.
            resizeHandleScreen_->handle->get_transform()
                ->set_localPosition({0.0f, 0.0f, 0.0f});
            resizeHandleScreen_->handle->get_transform()
                ->set_localScale({13.0f, 13.0f, 2.0f});
        }
        // A conventional lower-right resize grip is clearer than a floating
        // panel or labeled square. Draw three diagonal strokes over a fully
        // transparent canvas; the screen's right and bottom borders already
        // provide the two attached edges of the control.
        struct GripStroke
        {
            UnityEngine::Vector2 position;
            float length;
        };
        constexpr GripStroke gripStrokes[] = {
            {{2.4f, -2.4f}, 3.0f},
            {{0.8f, -0.8f}, 5.2f},
            {{-0.8f, 0.8f}, 7.4f}};
        for(const auto& stroke : gripStrokes)
        {
            if(auto* line = BSML::Lite::CreateImage(
                   resizeHandleScreen_->get_transform(), whitePixel,
                   stroke.position, {stroke.length, 0.75f}))
            {
                line->set_color({1.0f, 0.68f, 0.18f, 1.0f});
                line->set_preserveAspect(false);
                line->set_raycastTarget(false);
                CenterRect(line->get_transform()
                    .cast<UnityEngine::RectTransform>());
                line->get_rectTransform()->set_localEulerAngles(
                    {0.0f, 0.0f, 45.0f});
            }
        }

        UpdateEditorOverlayLayout();
        HideNativeHandleRenderer(editorScreen_);
        HideNativeHandleRenderer(resizeHandleScreen_);
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
        // set_ScreenSize asks BSML to update its move handle and turns the
        // primitive renderer back on. Hide only that renderer again; the Box
        // Collider and FloatingScreenHandle remain untouched and draggable.
        HideNativeHandleRenderer(editorScreen_);

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
        // Keep the complete border inside the canvas bounds. Placing its
        // center exactly on an edge caused half of the old 1.5-unit line to be
        // clipped by the floating screen's mask, which could make the frame
        // effectively disappear at a distance.
        constexpr float thickness = 0.75f;
        const float borderInset = thickness * 0.5f;
        setBorder(editorBorders_[0],
            {0.0f, height * 0.5f - borderInset}, {width, thickness});
        setBorder(editorBorders_[1],
            {0.0f, -height * 0.5f + borderInset}, {width, thickness});
        setBorder(editorBorders_[2],
            {-width * 0.5f + borderInset, 0.0f}, {thickness, height});
        setBorder(editorBorders_[3],
            {width * 0.5f - borderInset, 0.0f}, {thickness, height});

        if(editorMoveBar_)
        {
            auto rect = editorMoveBar_->get_transform()
                .cast<UnityEngine::RectTransform>();
            const float barWidth = width * 0.8f;
            const float barHeight = std::max(4.0f, height / 15.0f);
            const float barY = height * 0.5f;
            rect->set_anchoredPosition({0.0f, barY});
            rect->set_sizeDelta({barWidth, barHeight});
        }
        if(editorMoveText_)
        {
            auto rect = editorMoveText_->get_rectTransform();
            // Keep the complete six-unit text box below the edge-mounted move
            // bar. Its old center was only 2.5 units below the canvas top, so
            // the mask cut off the upper portion of every glyph.
            const float barHeight = std::max(4.0f, height / 15.0f);
            rect->set_anchoredPosition({
                0.0f,
                height * 0.5f - std::max(6.5f, barHeight + 1.5f)});
            rect->set_sizeDelta({std::max(46.0f, width * 0.70f), 6.0f});
        }

        if(editorInstructions_)
        {
            auto rect = editorInstructions_->get_rectTransform();
            rect->set_anchoredPosition({0.0f, height * 0.17f});
            rect->set_sizeDelta({std::max(54.0f, width - 16.0f), 28.0f});
        }
        if(editorAspectText_)
        {
            editorAspectText_->set_text(
                "Screen Aspect Ratio  " + AspectRatioLabel(width, height));
            auto rect = editorAspectText_->get_rectTransform();
            rect->set_anchoredPosition({0.0f, -height * 0.04f});
            rect->set_sizeDelta({std::max(48.0f, width - 16.0f), 12.0f});
        }
        if(editorSaveButton_)
        {
            auto rect = editorSaveButton_->get_transform()
                .cast<UnityEngine::RectTransform>();
            rect->set_anchoredPosition({0.0f, -height * 0.24f});
            rect->set_sizeDelta({40.0f, 10.0f});
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
                {halfWidth - ResizeHandleInsetUi,
                 -halfHeight + ResizeHandleInsetUi,
                 -0.5f}),
            editorScreen_->get_transform()->get_rotation());
    }

    bool ScreenPreview::ApplyLibraryPreviewEditorDisplay(bool rebuildGeometry)
    {
        if(!editorScreen_ || !editorConfig_ ||
           !PlaybackSession::Instance().IsLibraryPreviewActive())
            return false;

        // The editor transform is authoritative while it is being dragged.
        // Do not mutate editorConfig_ with the temporary depth separation:
        // SaveUndockedEditing must persist the visible frame's exact location,
        // and RestoreLibraryPreviewDisplay removes this offset on save/cancel.
        auto displayConfig = *editorConfig_;
        auto transform = editorScreen_->get_transform();
        const auto displayPosition = transform->TransformPoint(
            {0.0f, 0.0f, EditorVideoDepthOffsetUi});
        auto displayRotation = transform->get_rotation();
        const auto displayEuler = displayRotation.get_eulerAngles();
        displayConfig.screenPosition = {
            displayPosition.x, displayPosition.y, displayPosition.z};
        displayConfig.screenRotation = {
            displayEuler.x, displayEuler.y, displayEuler.z};
        return PlaybackSession::Instance().ApplyLibraryPreviewEditorDisplay(
            displayConfig, rebuildGeometry);
    }

    void ScreenPreview::TickUndockedEditor()
    {
        if(!editorScreen_ || !resizeHandleScreen_ || !editorConfig_)
            return;
        if(!Settings::Instance().ModEnabled())
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
            // The native drag target and cyan marker are both centered on the
            // helper canvas, so its origin is the new lower-right corner.
            const auto markerWorld = resizeHandleScreen_->get_transform()
                ->TransformPoint({0.0f, 0.0f, 0.0f});
            const auto local = editorScreen_->get_transform()
                ->InverseTransformPoint(markerWorld);
            // The marker center is intentionally inset from the actual
            // lower-right corner. Add that inset back before converting the
            // dragged position into physical screen size.
            const float width = std::clamp(
                (std::abs(local.x) + ResizeHandleInsetUi) *
                    2.0f / UiUnitsPerMeter,
                0.5f, 50.0f);
            const float height = std::clamp(
                (std::abs(local.y) + ResizeHandleInsetUi) *
                    2.0f / UiUnitsPerMeter,
                0.5f, 50.0f);
            if(std::abs(width - *editorConfig_->screenWidthOverride) > 0.005f ||
               std::abs(height - editorConfig_->screenHeight) > 0.005f)
            {
                editorConfig_->screenWidthOverride = width;
                editorConfig_->screenHeight = height;
                UpdateEditorOverlayLayout();
                if(PlaybackSession::Instance().IsLibraryPreviewActive())
                {
                    ApplyLibraryPreviewEditorDisplay(true);
                }
                else
                {
                    surface_.UpdateGeometry(*editorConfig_);
                }
            }
        }

        const auto editorPosition =
            editorScreen_->get_transform()->get_position();
        auto editorRotation =
            editorScreen_->get_transform()->get_rotation();
        editorConfig_->screenPosition = {
            editorPosition.x, editorPosition.y, editorPosition.z};
        const auto editorEuler = editorRotation.get_eulerAngles();
        editorConfig_->screenRotation = {
            editorEuler.x, editorEuler.y, editorEuler.z};
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
        {
            ApplyLibraryPreviewEditorDisplay(false);
        }
        else
        {
            surface_.SetWorldTransform(editorPosition, editorRotation);
        }
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
        Settings::Instance().CommitScreenEditTransaction();
        const bool libraryPreviewActive =
            PlaybackSession::Instance().IsLibraryPreviewActive();
        DestroyEditorUi();
        if(libraryPreviewActive)
            PlaybackSession::Instance().RestoreLibraryPreviewDisplay(true);
        else
            Refresh();
        SettingsMenu::Instance().RefreshControls();
        PaperLogger.info("Saved undocked screen placement");
    }

    void ScreenPreview::CancelUndockedEditing()
    {
        if(!editorScreen_ && !resizeHandleScreen_)
            return;
        Settings::Instance().CancelScreenEditTransaction();
        const bool libraryPreviewActive =
            PlaybackSession::Instance().IsLibraryPreviewActive();
        DestroyEditorUi();
        if(libraryPreviewActive)
            PlaybackSession::Instance().RestoreLibraryPreviewDisplay(false);
        else
            Refresh();
        SettingsMenu::Instance().RefreshControls();
        PaperLogger.info("Cancelled unsaved undocked screen placement");
    }

    void ScreenPreview::StageCurrentUndockedPlacement()
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
    }

    void ScreenPreview::RefreshUndockedEditingFromSettings()
    {
        if(!editorScreen_ || !editorConfig_ || !editorAppliedLayout_ ||
           !editorConfig_->screenWidthOverride)
            return;

        const auto& settings = Settings::Instance();
        const auto& layout = settings.ActiveLayout();
        auto transform = editorScreen_->get_transform();

        if(editorLayoutIndex_ != settings.ActiveScreenLayout())
        {
            // The previous layout's controller placement is staged by the
            // selector callback. Load the newly selected layout as another
            // draft within the same all-layout transaction.
            transform->SetPositionAndRotation(
                {layout.undockedPositionX,
                 layout.undockedPositionY,
                 layout.undockedPositionZ},
                UnityEngine::Quaternion::Euler({
                    layout.undockedRotationX,
                    layout.undockedRotationY,
                    layout.undockedRotationZ}));
            editorConfig_->screenWidthOverride = layout.undockedWidth;
            editorConfig_->screenHeight = layout.undockedHeight;
            editorLayoutIndex_ = settings.ActiveScreenLayout();
        }
        else
        {
            // Basic canvas controls remain useful while unlocked. Apply only
            // the delta since the preceding callback so controller placement
            // remains the baseline rather than snapping back to the wall.
            const auto& previous = *editorAppliedLayout_;
            const auto position = transform->get_position();
            transform->set_position({
                position.x + layout.horizontalOffset - previous.horizontalOffset,
                position.y + layout.verticalOffset - previous.verticalOffset,
                position.z + layout.distanceOffset - previous.distanceOffset});
            const auto rotation = transform->get_rotation().get_eulerAngles();
            transform->set_rotation(UnityEngine::Quaternion::Euler({
                rotation.x + layout.tiltOffset - previous.tiltOffset,
                rotation.y,
                rotation.z + layout.screenRoll - previous.screenRoll}));
            if(previous.scale > 0.0001f &&
               std::abs(layout.scale - previous.scale) > 0.0001f)
            {
                const float ratio = layout.scale / previous.scale;
                *editorConfig_->screenWidthOverride *= ratio;
                editorConfig_->screenHeight *= ratio;
            }
        }

        editorConfig_->screenCurvature = layout.curved
            ? layout.curvature : 0.0f;
        editorConfig_->maintainAspectRatioWhenCurved =
            layout.curved && layout.maintainAspectRatio;
        editorConfig_->letterboxTransparent = layout.letterboxTransparency;
        editorConfig_->videoOpacity = layout.videoOpacity;
        editorConfig_->videoRotation = layout.videoRotation;
        editorConfig_->videoZoom = layout.videoZoom;
        editorConfig_->videoOffsetX = layout.videoOffsetX;
        editorConfig_->videoOffsetY = layout.videoOffsetY;
        editorConfig_->videoTilt = layout.videoTilt;
        editorConfig_->stretchVideoToFit = layout.stretchVideoToFit;
        editorAppliedLayout_ = layout;

        UpdateEditorOverlayLayout();
        PlaceResizeHandle();
        if(PlaybackSession::Instance().IsLibraryPreviewActive())
            ApplyLibraryPreviewEditorDisplay(true);
        else
            surface_.UpdateGeometry(*editorConfig_);
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
        editorMoveBar_ = nullptr;
        editorInstructions_ = nullptr;
        editorMoveText_ = nullptr;
        editorAspectText_ = nullptr;
        editorSaveButton_ = nullptr;
        editorAppliedLayout_.reset();
        editorLayoutIndex_ = -1;
        editorConfig_.reset();
    }
}
