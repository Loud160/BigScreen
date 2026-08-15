// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/PerformancePanel.hpp"
#include "main.hpp"

#include <exception>
#include <format>
#include <cmath>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/Settings.hpp"
#include "HMUI/ImageView.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/Helpers/utilities.hpp"

namespace BigScreen {
    namespace {
        // BSML floating-screen sizes use UI units at approximately 50 units
        // per world meter. The panel is FIXED SIZE and never resizes: the
        // content is laid out to fit these numbers, not the other way
        // around. Every decoration position and every layout height below
        // derives from this one block so the painted frame and the layout
        // regions cannot drift apart.
        constexpr float PanelWidth = 110.0f;
        // Header tab: the panel title. Footer tab: the move instruction.
        // Both are welded onto the body frame's border lines.
        constexpr float HeaderHeight = 8.0f;
        constexpr float FooterHeight = 7.0f;
        // The body holds a column-caption row ("Quest" / "Video") plus the
        // tallest column's 8 statistics rows. Each row gets a fixed share;
        // text auto-sizes down to fit rather than growing the row.
        constexpr float ColumnHeaderHeight = 7.0f;
        constexpr float BodyRowCount = 8.0f;
        constexpr float BodyRowHeight = 7.0f;
        constexpr float BodyHeight =
            ColumnHeaderHeight + BodyRowCount * BodyRowHeight;
        constexpr float ScreenCanvasHeight =
            HeaderHeight + BodyHeight + FooterHeight;
        constexpr float TitleTabWidth = 76.0f;
        constexpr float InstructionTabWidth = 62.0f;
        // The left column's glance block: a large FPS number with its label
        // beneath it, then breathing room before the Minimum/Maximum rows.
        constexpr float FpsValueHeight = 11.0f;
        constexpr float FpsLabelHeight = 4.0f;
        constexpr float FpsSpacerHeight = 2.5f;
        // Column geometry is shared by the body layout AND the painted
        // divider line, so the line always sits exactly between the columns.
        constexpr float BodyPaddingX = 5.0f;
        constexpr float LeftColumnWidth = 34.0f;
        constexpr float ColumnSpacing = 4.0f;
        constexpr float DividerX = -PanelWidth * 0.5f + BodyPaddingX +
            LeftColumnWidth + ColumnSpacing * 0.5f;
        void CenterRect(UnityEngine::RectTransform* rect)
        {
            if(!rect)
                return;
            rect->set_anchorMin({0.5f, 0.5f});
            rect->set_anchorMax({0.5f, 0.5f});
            rect->set_pivot({0.5f, 0.5f});
        }

        void ConfigureImage(
            HMUI::ImageView* image,
            UnityEngine::Vector2 position,
            UnityEngine::Vector2 size,
            UnityEngine::Color color)
        {
            if(!image)
                return;
            image->set_color(color);
            image->set_preserveAspect(false);
            if(auto* rounded = BSML::Utilities::FindSpriteCached("RoundRect10"))
            {
                image->set_sprite(rounded);
                image->set_type(UnityEngine::UI::Image::Type::Sliced);
            }
            // Only BSML's proven native Top handle accepts pointer input. The
            // panel artwork and text must never steal its raycast.
            image->set_raycastTarget(false);
            auto rect = image->get_transform().cast<UnityEngine::RectTransform>();
            CenterRect(rect);
            rect->set_anchoredPosition(position);
            rect->set_sizeDelta(size);
        }

        void ConfigureBorderImage(
            HMUI::ImageView* image,
            UnityEngine::Vector2 position,
            UnityEngine::Vector2 size,
            UnityEngine::Color color)
        {
            if(!image)
                return;
            image->set_color(color);
            image->set_preserveAspect(false);
            image->set_raycastTarget(false);
            auto rect = image->get_transform().cast<UnityEngine::RectTransform>();
            CenterRect(rect);
            rect->set_anchoredPosition(position);
            rect->set_sizeDelta(size);
        }

        void HideNativeHandleRenderer(BSML::FloatingScreen* screen)
        {
            if(!screen || !screen->handle)
                return;
            if(auto* renderer = screen->handle
                   ->GetComponent<UnityEngine::MeshRenderer*>())
                renderer->set_enabled(false);
        }

        void ExpandNativeHandleAcrossPanel(BSML::FloatingScreen* screen)
        {
            if(!screen || !screen->handle)
                return;

            // BSML exposes a Full handle, but it is not reliably
            // draggable on Quest. The undocked-screen editor proved that the
            // native Top handle receives controller grabs consistently. Keep
            // that working handler and collider, center it, then expand its
            // hit volume across this panel. All panel artwork and text have
            // raycastTarget disabled, so grabbing any visible area reaches it.
            screen->handle->get_transform()->set_localPosition(
                {0.0f, 0.0f, 0.0f});
            screen->handle->get_transform()->set_localScale(
                {PanelWidth, ScreenCanvasHeight, 2.0f});
        }
    }

    PerformancePanel& PerformancePanel::Instance()
    {
        static PerformancePanel panel;
        return panel;
    }

    void PerformancePanel::ActivateMenu() noexcept
    {
        ActivateForContext(false);
    }

    void PerformancePanel::ActivateGameplay() noexcept
    {
        ActivateForContext(true);
    }

    void PerformancePanel::ActivateForContext(bool gameplay) noexcept
    {
        // Capture the old scene's transform before changing ownership. The
        // next menu or gameplay panel therefore opens exactly where the player
        // deliberately left it in the previous context.
        SaveCurrentPlacement();
        Destroy();
        context_ = gameplay ? Context::Gameplay : Context::Menu;
        if(Settings::Instance().ModEnabled() &&
           Settings::Instance().PerformanceDiagnosticsEnabled())
        {
            try
            {
                if(!CreateAtSavedPlacement())
                    ErrorManager::Instance().RecordError(
                        "Creating the performance panel",
                        "BSML could not create the floating panel");
            }
            catch(const std::exception& exception)
            {
                Destroy();
                ErrorManager::Instance().RecordError(
                    "Creating the performance panel", exception.what());
            }
            catch(...)
            {
                Destroy();
                ErrorManager::Instance().RecordError(
                    "Creating the performance panel",
                    "Unknown native exception");
            }
        }
    }

    void PerformancePanel::SetEnabled(bool enabled) noexcept
    {
        SaveCurrentPlacement();
        Destroy();
        if(!enabled || context_ == Context::None ||
           !Settings::Instance().ModEnabled())
            return;
        try
        {
            if(!CreateAtSavedPlacement())
                ErrorManager::Instance().RecordError(
                    "Enabling the performance panel",
                    "BSML could not create the floating panel");
        }
        catch(const std::exception& exception)
        {
            Destroy();
            ErrorManager::Instance().RecordError(
                "Enabling the performance panel", exception.what());
        }
        catch(...)
        {
            Destroy();
            ErrorManager::Instance().RecordError(
                "Enabling the performance panel",
                "Unknown native exception");
        }
    }

    void PerformancePanel::ResetPlacement() noexcept
    {
        auto& settings = Settings::Instance();
        settings.ResetPerformancePanelPlacement();
        settings.Flush();
        try
        {
            if(!screen_)
                return;
            screen_->get_transform()->SetPositionAndRotation(
                {settings.PerformancePanelPositionX(),
                 settings.PerformancePanelPositionY(),
                 settings.PerformancePanelPositionZ()},
                UnityEngine::Quaternion::Euler({
                    settings.PerformancePanelRotationX(),
                    settings.PerformancePanelRotationY(),
                    settings.PerformancePanelRotationZ()}));
        }
        catch(...)
        {
            ErrorManager::Instance().RecordError(
                "Resetting the performance panel",
                "Unity rejected the default diagnostics-panel transform");
        }
    }

    void PerformancePanel::SuspendMenu() noexcept
    {
        // A gameplay panel may already exist while Beat Saber's menu flow is
        // finishing its scene transition. Never let that late deactivation
        // destroy the new map's panel.
        if(context_ != Context::Menu)
            return;
        SaveCurrentPlacement();
        context_ = Context::None;
        Destroy();
    }

    void PerformancePanel::SuspendGameplay() noexcept
    {
        if(context_ != Context::Gameplay)
            return;
        SaveCurrentPlacement();
        context_ = Context::None;
        Destroy();
    }

    void PerformancePanel::SetStatistics(
        const PerformancePanelData& data) noexcept
    {
        ApplyRows(&data);
    }

    void PerformancePanel::ShowWaitingMessage() noexcept
    {
        ApplyRows(nullptr);
    }

    void PerformancePanel::ApplyRows(const PerformancePanelData* data) noexcept
    {
        try
        {
            const bool live = data != nullptr;
            const PerformancePanelData zero{};
            const PerformancePanelData& d = live ? *data : zero;
            if(rightHeader_)
                rightHeader_->set_text(std::format(
                    "<color=#75DFFF><b>Video{}</b></color>",
                    live && !d.decoderRuntime.empty()
                        ? " · FFmpeg " + d.decoderRuntime
                        : std::string{}));
            if(fpsValue_)
                fpsValue_->set_text(std::format(
                    "<b>{}</b>",
                    live && d.averageFps > 0.0
                        ? std::format("{:.1f}", d.averageFps)
                        : std::string("--")));
            const auto row = [](TMPro::TextMeshProUGUI* target,
                                const char* label,
                                std::string value)
            {
                if(target)
                    target->set_text(std::format(
                        "<color=#AEBAC8>{}</color>  <b>{}</b>",
                        label,
                        value));
            };
            row(leftRows_[0], "Minimum",
                live && d.sampledFrames > 0
                    ? std::format("{:.1f}", d.minimumFps)
                    : std::string("--"));
            row(leftRows_[1], "Maximum",
                live && d.sampledFrames > 0
                    ? std::format("{:.1f}", d.maximumFps)
                    : std::string("--"));
            row(rightRows_[0], "Decoder",
                live ? (d.decodeMethod == "hardware"
                            ? std::string("Hardware")
                            : std::string("Software"))
                     : std::string("--"));
            row(rightRows_[1], "Source",
                live ? std::format(
                           "{}x{} @ {:.1f} {}",
                           d.sourceWidth, d.sourceHeight, d.sourceFps,
                           d.codec.empty() ? "Unknown" : d.codec)
                     : std::string("--"));
            row(rightRows_[2], "Output",
                live ? std::format(
                           "{}x{} @ {} cap",
                           d.outputWidth, d.outputHeight, d.outputFpsLimit)
                     : std::string("--"));
            row(rightRows_[3], "Frames Skipped",
                live ? std::format("{}", d.totalMissedVideoFrames)
                     : std::string("--"));
            row(rightRows_[4], "Video FPS Average",
                live ? std::format("{:.1f}", d.averageVideoFramesPerSecond)
                     : std::string("--"));
            row(rightRows_[5], "Frame Rate Loss",
                live ? std::format("{:.1f}%", d.missedVideoFramePercent)
                     : std::string("--"));
            row(rightRows_[6], "Decode Average",
                live ? std::format("{:.2f} ms", d.averageDecodeMilliseconds)
                     : std::string("--"));
            row(rightRows_[7], "Decode Peak",
                live ? std::format("{:.2f} ms", d.peakDecodeMilliseconds)
                     : std::string("--"));
        }
        catch(...)
        {
            // A transient TMPro failure must not interrupt preview playback.
            if(!rowFailureLogged_)
            {
                rowFailureLogged_ = true;
                PaperLogger.warn(
                    "Unity rejected a performance-panel text update");
                ErrorManager::Instance().RecordError(
                    "Updating the performance panel",
                    "Unity rejected the performance text update");
            }
        }
    }

    void PerformancePanel::TickInteraction() noexcept
    {
        try
        {
            if(!screen_ || !screen_->handle)
                return;
            auto* handle = screen_->handle
                ->GetComponent<BSML::FloatingScreenHandle*>();
            if(!handle || !handle->_grabbingController)
                return;
            auto anchor = handle->_grabbingController
                ->get_viewAnchorTransform();
            if(!anchor)
                return;

            // BSML normally performs this rotation in FloatingScreenHandle's
            // Update. On Quest, that portion does not consistently
            // follow wrist rotation after the Top collider is expanded to make
            // this entire panel grabbable, although translation still works.
            // Reapply BSML's own captured controller-relative quaternion here;
            // no alternate gesture or coordinate system is introduced.
            const auto targetRotation = UnityEngine::Quaternion::op_Multiply(
                anchor->get_rotation(), handle->_grabRot);
            const float blend = std::min(
                1.0f,
                5.0f * UnityEngine::Time::get_unscaledDeltaTime());
            screen_->get_transform()->set_rotation(UnityEngine::Quaternion::Lerp(
                screen_->get_transform()->get_rotation(),
                targetRotation,
                blend));
        }
        catch(...)
        {
            // A panel may be destroyed during the same Unity frame as a menu
            // or gameplay transition. Interaction is optional diagnostics UI,
            // so a stale transient reference must never interrupt playback.
            if(!interactionFailureLogged_)
            {
                interactionFailureLogged_ = true;
                PaperLogger.warn(
                    "Unity rejected performance-panel interaction during a transition");
                ErrorManager::Instance().RecordError(
                    "Moving the performance panel",
                    "Unity rejected the panel interaction during a scene transition");
            }
        }
    }

    void PerformancePanel::SaveCurrentPlacement() noexcept
    {
        try
        {
            if(!screen_ || context_ == Context::None)
                return;
            const auto position = screen_->get_transform()->get_position();
            const auto rotation = screen_->get_transform()->get_eulerAngles();
            if(!std::isfinite(position.x) || !std::isfinite(position.y) ||
               !std::isfinite(position.z) || !std::isfinite(rotation.x) ||
               !std::isfinite(rotation.y) || !std::isfinite(rotation.z))
            {
                return;
            }
            auto& settings = Settings::Instance();
            settings.SetPerformancePanelPlacement(
                position.x, position.y, position.z,
                rotation.x, rotation.y, rotation.z);
            // This boundary occurs only when the toggle/context changes, so a
            // durable write here does not turn controller motion into repeated
            // flash writes. It also survives Beat Saber being closed directly
            // after leaving Big Screen's menu.
            settings.Flush();
        }
        catch(...)
        {
            if(!interactionFailureLogged_)
            {
                interactionFailureLogged_ = true;
                ErrorManager::Instance().RecordError(
                    "Saving the performance panel",
                    "Unity rejected the diagnostics-panel transform during a scene transition");
            }
        }
    }

    bool PerformancePanel::CreateAtSavedPlacement()
    {
        const auto& settings = Settings::Instance();
        screen_ = BSML::FloatingScreen::CreateFloatingScreen(
            {PanelWidth, ScreenCanvasHeight},
            true,
            {settings.PerformancePanelPositionX(),
             settings.PerformancePanelPositionY(),
             settings.PerformancePanelPositionZ()},
            UnityEngine::Quaternion::Euler({
                settings.PerformancePanelRotationX(),
                settings.PerformancePanelRotationY(),
                settings.PerformancePanelRotationZ()}),
            0.0f,
            false);
        if(!screen_)
            return false;

        screen_->get_gameObject()->set_name("Big Screen Performance Panel");
        // Use the native handle path proven by the undocked screen editor. Its
        // collider is expanded below so the complete panel remains grabbable.
        screen_->set_HandleSide(BSML::Side::Top);
        screen_->set_HighlightHandle(false);
        ExpandNativeHandleAcrossPanel(screen_);

        const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
        const UnityEngine::Color borderColor{0.0f, 0.80f, 1.0f, 1.0f};
        const UnityEngine::Color panelColor{0.025f, 0.055f, 0.095f, 0.98f};
        constexpr float borderThickness = 1.0f;
        constexpr float halfWidth = PanelWidth * 0.5f;
        constexpr float halfCanvas = ScreenCanvasHeight * 0.5f;
        // The painted body frame occupies exactly the vertical band the
        // content layout assigns to its Body region (between the header and
        // footer strips), so decoration and content share one coordinate
        // truth and cannot overlap each other.
        constexpr float bodyTop = halfCanvas - HeaderHeight;
        constexpr float bodyBottom = -halfCanvas + FooterHeight;
        constexpr float bodyCenterY = (bodyTop + bodyBottom) * 0.5f;

        for(auto& border : borders_)
            border = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        ConfigureBorderImage(
            borders_[0], {0.0f, bodyTop - 0.5f},
            {PanelWidth, borderThickness}, borderColor);
        ConfigureBorderImage(
            borders_[1], {0.0f, bodyBottom + 0.5f},
            {PanelWidth, borderThickness}, borderColor);
        ConfigureBorderImage(
            borders_[2], {-halfWidth + 0.5f, bodyCenterY},
            {borderThickness, BodyHeight}, borderColor);
        ConfigureBorderImage(
            borders_[3], {halfWidth - 0.5f, bodyCenterY},
            {borderThickness, BodyHeight}, borderColor);
        ConfigureImage(
            background_ = BSML::Lite::CreateImage(
                screen_->get_transform(), whitePixel),
            {0.0f, bodyCenterY}, {PanelWidth - 2.0f, BodyHeight - 2.0f},
            panelColor);

        // The title and instruction are small tabs styled exactly like the
        // main body: the same rounded dark fill inset inside the same cyan
        // outline. Each tab is placed so the edge nearest the panel lands
        // exactly on the main frame's own border strip, and its fill runs
        // flush to that strip, so the tab reads as welded onto the frame
        // rather than floating beside it.
        const auto createTab = [&](HMUI::ImageView*& card,
                                   auto& edges,
                                   UnityEngine::Vector2 center,
                                   UnityEngine::Vector2 size)
        {
            // Fill first: the outline strips created afterwards draw over the
            // fill's edges, hiding the rounded sprite's corner curvature at
            // the shared border exactly as the main background does.
            card = BSML::Lite::CreateImage(
                screen_->get_transform(), whitePixel);
            ConfigureImage(
                card, center,
                {size.x - 2.0f * borderThickness, size.y - borderThickness},
                panelColor);
            for(auto& edge : edges)
                edge = BSML::Lite::CreateImage(
                    screen_->get_transform(), whitePixel);
            const float left = center.x - size.x * 0.5f;
            const float right = center.x + size.x * 0.5f;
            const float bottom = center.y - size.y * 0.5f;
            const float top = center.y + size.y * 0.5f;
            ConfigureBorderImage(
                edges[0], {center.x, top}, {size.x, borderThickness}, borderColor);
            ConfigureBorderImage(
                edges[1], {center.x, bottom}, {size.x, borderThickness}, borderColor);
            ConfigureBorderImage(
                edges[2], {left, center.y}, {borderThickness, size.y}, borderColor);
            ConfigureBorderImage(
                edges[3], {right, center.y}, {borderThickness, size.y}, borderColor);
        };
        // The welded tab decorations sit on the frame's own border lines and
        // now exactly cover the layout's Header/Footer regions.
        const UnityEngine::Vector2 titleCenter{
            0.0f, (bodyTop - 0.5f) + HeaderHeight * 0.5f};
        const UnityEngine::Vector2 instructionCenter{
            0.0f, (bodyBottom + 0.5f) - FooterHeight * 0.5f};
        createTab(
            titleCard_, titleBorders_, titleCenter,
            {TitleTabWidth, HeaderHeight});
        createTab(
            instructionCard_, instructionBorders_, instructionCenter,
            {InstructionTabWidth, FooterHeight});

        headsetCard_ = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        ConfigureImage(
            headsetCard_, {-34.0f, bodyCenterY},
            {38.0f, BodyHeight - 2.0f}, panelColor);
        videoCard_ = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        ConfigureImage(
            videoCard_, {20.0f, bodyCenterY},
            {66.0f, BodyHeight - 2.0f}, panelColor);
        // A dim vertical rule between the two columns, positioned from the
        // same constants the layout uses for the column widths.
        columnDivider_ = BSML::Lite::CreateImage(
            screen_->get_transform(), whitePixel);
        ConfigureBorderImage(
            columnDivider_, {DividerX, bodyCenterY},
            {0.6f, BodyHeight - 6.0f},
            UnityEngine::Color{0.0f, 0.80f, 1.0f, 0.35f});

        // ---- Content: one nested layout, sized TO the fixed panel ------
        // Header, body, and footer are rows of a single VerticalLayoutGroup,
        // so they can never overlap; the body receives exactly the leftover
        // height and each statistics row a fixed share of it, with TMP
        // auto-size shrinking long values instead of overflowing. Nothing
        // below this point sets anchoredPosition or sizeDelta — every child
        // is governed by a LayoutElement.
        const auto neutralizeFitter = [](auto* group)
        {
            // BSML::Lite layout groups arrive with a ContentSizeFitter. The
            // panel is fixed-size: nothing may auto-grow.
            if(auto* fitter = group->get_gameObject()
                   ->template GetComponent<UnityEngine::UI::ContentSizeFitter*>())
            {
                fitter->set_horizontalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
                fitter->set_verticalFit(
                    UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
            }
        };
        const auto layoutElementFor = [](auto* component)
            -> UnityEngine::UI::LayoutElement*
        {
            // get_gameObject returns a UnityW wrapper, not a raw pointer.
            auto object = component->get_gameObject();
            auto* element =
                object->template GetComponent<UnityEngine::UI::LayoutElement*>();
            if(!element)
                element = object
                    ->template AddComponent<UnityEngine::UI::LayoutElement*>();
            return element;
        };
        const auto setLayoutHeight = [&](auto* component,
                                         float preferred,
                                         float flexible)
        {
            if(auto* element = layoutElementFor(component))
            {
                element->set_preferredHeight(preferred);
                element->set_flexibleHeight(flexible);
            }
        };

        auto* rootLayout = BSML::Lite::CreateVerticalLayoutGroup(
            screen_->get_transform());
        if(!rootLayout)
        {
            Destroy();
            return false;
        }
        neutralizeFitter(rootLayout);
        rootLayout->set_spacing(0.0f);
        rootLayout->set_padding(UnityEngine::RectOffset::New_ctor(0, 0, 0, 0));
        rootLayout->set_childControlWidth(true);
        // childControlHeight with childForceExpandHeight OFF is the pair
        // that makes header/footer honor their fixed preferredHeight while
        // the body's flexibleHeight eats exactly the remainder.
        rootLayout->set_childControlHeight(true);
        rootLayout->set_childForceExpandWidth(true);
        rootLayout->set_childForceExpandHeight(false);
        rootLayout->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
        {
            // The one permitted manual rect: the fixed Root itself.
            auto rootRect = rootLayout->get_transform()
                .cast<UnityEngine::RectTransform>();
            CenterRect(rootRect);
            rootRect->set_anchoredPosition({0.0f, 0.0f});
            rootRect->set_sizeDelta({PanelWidth, ScreenCanvasHeight});
        }

        // The header region holds only the panel title, vertically centered
        // by TMP alignment inside its fixed-height row.
        title_ = BSML::Lite::CreateText(
            rootLayout->get_transform(),
            "Big Screen Performance",
            TMPro::FontStyles::Bold,
            5.0f);
        if(title_)
        {
            title_->set_alignment(TMPro::TextAlignmentOptions::Center);
            title_->set_enableWordWrapping(false);
            title_->set_raycastTarget(false);
            title_->set_enableAutoSizing(true);
            title_->set_fontSizeMin(3.2f);
            title_->set_fontSizeMax(5.0f);
            setLayoutHeight(title_, HeaderHeight, 0.0f);
        }

        auto* body = BSML::Lite::CreateHorizontalLayoutGroup(
            rootLayout->get_transform());
        if(!body)
        {
            Destroy();
            return false;
        }
        neutralizeFitter(body);
        body->set_spacing(ColumnSpacing);
        body->set_padding(UnityEngine::RectOffset::New_ctor(
            static_cast<int>(BodyPaddingX), static_cast<int>(BodyPaddingX),
            1, 1));
        body->set_childControlWidth(true);
        body->set_childControlHeight(true);
        body->set_childForceExpandWidth(false);
        body->set_childForceExpandHeight(true);
        body->set_childAlignment(UnityEngine::TextAnchor::UpperLeft);
        if(auto* bodyElement = layoutElementFor(body))
        {
            bodyElement->set_preferredHeight(BodyHeight);
            bodyElement->set_flexibleHeight(1.0f);
        }

        const auto createColumn = [&](float preferredWidth,
                                      float flexibleWidth)
            -> UnityEngine::UI::VerticalLayoutGroup*
        {
            auto* column = BSML::Lite::CreateVerticalLayoutGroup(
                body->get_transform());
            if(!column)
                return nullptr;
            neutralizeFitter(column);
            column->set_spacing(0.0f);
            column->set_padding(UnityEngine::RectOffset::New_ctor(0, 0, 0, 0));
            column->set_childControlWidth(true);
            column->set_childControlHeight(true);
            column->set_childForceExpandWidth(true);
            // Rows top-align at their fixed height instead of stretching to
            // split the leftover: a 3-row column keeps its intended empty
            // space at the bottom rather than drifting rows apart.
            column->set_childForceExpandHeight(false);
            column->set_childAlignment(UnityEngine::TextAnchor::UpperLeft);
            if(auto* element = layoutElementFor(column))
            {
                element->set_preferredWidth(preferredWidth);
                element->set_flexibleWidth(flexibleWidth);
                element->set_flexibleHeight(1.0f);
            }
            return column;
        };
        const auto createRow = [&](auto parent) -> TMPro::TextMeshProUGUI*
        {
            auto* text = BSML::Lite::CreateText(
                parent, "", TMPro::FontStyles::Normal, 4.2f);
            if(!text)
                return nullptr;
            text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            text->set_enableWordWrapping(false);
            text->set_raycastTarget(false);
            // A long value shrinks to fit its fixed row instead of pushing
            // later rows past the bottom border.
            text->set_enableAutoSizing(true);
            text->set_fontSizeMin(2.4f);
            text->set_fontSizeMax(4.2f);
            setLayoutHeight(text, BodyRowHeight, 0.0f);
            return text;
        };
        const auto createColumnHeader = [&](auto parent, const char* caption)
            -> TMPro::TextMeshProUGUI*
        {
            auto* text = BSML::Lite::CreateText(
                parent,
                std::format("<color=#75DFFF><b>{}</b></color>", caption),
                TMPro::FontStyles::Normal,
                4.6f);
            if(!text)
                return nullptr;
            text->set_alignment(TMPro::TextAlignmentOptions::Center);
            text->set_enableWordWrapping(false);
            text->set_raycastTarget(false);
            text->set_enableAutoSizing(true);
            text->set_fontSizeMin(3.0f);
            text->set_fontSizeMax(4.6f);
            setLayoutHeight(text, ColumnHeaderHeight, 0.0f);
            return text;
        };

        auto* leftColumn = createColumn(LeftColumnWidth, 0.0f);
        auto* rightColumn = createColumn(0.0f, 1.0f);
        if(!leftColumn || !rightColumn)
        {
            Destroy();
            return false;
        }
        leftHeader_ = createColumnHeader(leftColumn->get_transform(), "Quest");
        // The headline number users glance at mid-song: large, centered,
        // with its label beneath and breathing room before Minimum/Maximum.
        fpsValue_ = BSML::Lite::CreateText(
            leftColumn->get_transform(), "", TMPro::FontStyles::Bold, 8.0f);
        if(fpsValue_)
        {
            fpsValue_->set_alignment(TMPro::TextAlignmentOptions::Center);
            fpsValue_->set_enableWordWrapping(false);
            fpsValue_->set_raycastTarget(false);
            fpsValue_->set_enableAutoSizing(true);
            fpsValue_->set_fontSizeMin(4.0f);
            fpsValue_->set_fontSizeMax(8.0f);
            setLayoutHeight(fpsValue_, FpsValueHeight, 0.0f);
        }
        fpsLabel_ = BSML::Lite::CreateText(
            leftColumn->get_transform(),
            "<color=#AEBAC8>Average FPS</color>",
            TMPro::FontStyles::Normal,
            3.2f);
        if(fpsLabel_)
        {
            fpsLabel_->set_alignment(TMPro::TextAlignmentOptions::Center);
            fpsLabel_->set_enableWordWrapping(false);
            fpsLabel_->set_raycastTarget(false);
            fpsLabel_->set_enableAutoSizing(true);
            fpsLabel_->set_fontSizeMin(2.4f);
            fpsLabel_->set_fontSizeMax(3.2f);
            setLayoutHeight(fpsLabel_, FpsLabelHeight, 0.0f);
        }
        if(auto* spacer = BSML::Lite::CreateText(
               leftColumn->get_transform(), "",
               TMPro::FontStyles::Normal, 2.0f))
        {
            spacer->set_raycastTarget(false);
            setLayoutHeight(spacer, FpsSpacerHeight, 0.0f);
        }
        for(auto& row : leftRows_)
            row = createRow(leftColumn->get_transform());
        rightHeader_ = createColumnHeader(rightColumn->get_transform(), "Video");
        for(auto& row : rightRows_)
            row = createRow(rightColumn->get_transform());

        instruction_ = BSML::Lite::CreateText(
            rootLayout->get_transform(),
            "Grab anywhere to move",
            TMPro::FontStyles::Normal,
            3.8f);
        if(instruction_)
        {
            instruction_->set_alignment(TMPro::TextAlignmentOptions::Center);
            instruction_->set_enableWordWrapping(false);
            instruction_->set_raycastTarget(false);
            instruction_->set_enableAutoSizing(true);
            instruction_->set_fontSizeMin(2.4f);
            instruction_->set_fontSizeMax(3.8f);
            setLayoutHeight(instruction_, FooterHeight, 0.0f);
        }

        bool contentCreated = title_ && instruction_ && leftHeader_ &&
            rightHeader_ && fpsValue_ && fpsLabel_;
        for(auto* row : leftRows_)
            contentCreated = contentCreated && row;
        for(auto* row : rightRows_)
            contentCreated = contentCreated && row;
        if(!contentCreated)
        {
            Destroy();
            return false;
        }

        // Preserve the expanded native collider and pointer handler while
        // hiding only its primitive renderer behind the styled panel.
        HideNativeHandleRenderer(screen_);
        ShowWaitingMessage();
        return true;
    }

    void PerformancePanel::Destroy() noexcept
    {
        try
        {
            if(screen_)
                UnityEngine::Object::Destroy(screen_->get_gameObject());
        }
        catch(...)
        {
            // Destruction is best-effort during menu transitions. Clearing
            // every pointer prevents a stale Unity object from being reused.
        }
        screen_ = nullptr;
        rowFailureLogged_ = false;
        interactionFailureLogged_ = false;
        background_ = nullptr;
        headsetCard_ = nullptr;
        videoCard_ = nullptr;
        titleCard_ = nullptr;
        instructionCard_ = nullptr;
        borders_.fill(nullptr);
        titleBorders_.fill(nullptr);
        instructionBorders_.fill(nullptr);
        columnDivider_ = nullptr;
        title_ = nullptr;
        instruction_ = nullptr;
        leftHeader_ = nullptr;
        rightHeader_ = nullptr;
        fpsValue_ = nullptr;
        fpsLabel_ = nullptr;
        leftRows_.fill(nullptr);
        rightRows_.fill(nullptr);
    }
}
