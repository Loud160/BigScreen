#include "BigScreen/PerformancePanel.hpp"

#include <exception>
#include <format>

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
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/Helpers/utilities.hpp"

namespace BigScreen {
    namespace {
        // BSML floating-screen sizes use UI units at approximately 50 units
        // per world meter. This fixed 110 x 46 panel is large enough to read
        // from normal menu distance without covering the full center view.
        constexpr float PanelWidth = 110.0f;
        constexpr float PanelHeight = 54.0f;
        // Keep the default above the note lanes and the central Beat Saber UI.
        // Both menu and gameplay use this exact reset position.
        constexpr UnityEngine::Vector3 DefaultPosition{0.0f, 3.05f, 4.25f};

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
            // Only BSML's proven native Top handle accepts pointer input. The
            // panel artwork and text must never steal its raycast.
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

            // BSML 0.4.43 exposes a Full handle, but it is not reliably
            // draggable on Quest. The undocked-screen editor proved that the
            // native Top handle receives controller grabs consistently. Keep
            // that working handler and collider, center it, then expand its
            // hit volume across this panel. All panel artwork and text have
            // raycastTarget disabled, so grabbing any visible area reaches it.
            screen->handle->get_transform()->set_localPosition(
                {0.0f, 0.0f, 0.0f});
            screen->handle->get_transform()->set_localScale(
                {PanelWidth, PanelHeight, 2.0f});
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
        context_ = gameplay ? Context::Gameplay : Context::Menu;
        // Menu re-entry is also a reset boundary. Do not retain a location
        // from the previous visit or map where the user may no longer find it.
        Destroy();
        if(Settings::Instance().ModEnabled() &&
           Settings::Instance().PerformanceDiagnosticsEnabled())
        {
            try
            {
                if(!CreateAtDefaultPlacement())
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
        Destroy();
        if(!enabled || context_ == Context::None ||
           !Settings::Instance().ModEnabled())
            return;
        try
        {
            if(!CreateAtDefaultPlacement())
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

    void PerformancePanel::SuspendMenu() noexcept
    {
        // A gameplay panel may already exist while Beat Saber's menu flow is
        // finishing its scene transition. Never let that late deactivation
        // destroy the new map's panel.
        if(context_ != Context::Menu)
            return;
        context_ = Context::None;
        Destroy();
    }

    void PerformancePanel::SuspendGameplay() noexcept
    {
        if(context_ != Context::Gameplay)
            return;
        context_ = Context::None;
        Destroy();
    }

    void PerformancePanel::SetStatistics(
        const PerformancePanelData& data) noexcept
    {
        try
        {
            if(headsetStatistics_)
            {
                const auto minimum = data.sampledFrames > 0
                    ? std::format("{:.1f}", data.minimumFps)
                    : std::string("--");
                const auto maximum = data.sampledFrames > 0
                    ? std::format("{:.1f}", data.maximumFps)
                    : std::string("--");
                headsetStatistics_->set_text(std::format(
                    "<color=#75DFFF><b>{}</b></color>\n"
                    "<size=150%><b>{}</b></size>\n"
                    "<size=82%>Average FPS</size>\n"
                    "<color=#AEBAC8>Minimum</color>  <b>{}</b>\n"
                    "<color=#AEBAC8>Maximum</color>  <b>{}</b>",
                    data.gameplay ? "Gameplay" : "Menu preview",
                    data.averageFps > 0.0
                        ? std::format("{:.1f}", data.averageFps)
                        : std::string("--"),
                    minimum,
                    maximum));
            }
            if(videoStatistics_)
            {
                videoStatistics_->set_text(std::format(
                    "<color=#75DFFF><b>Video</b></color>\n"
                    "<color=#AEBAC8>Source</color>  <b>{}x{} @ {:.1f}</b>\n"
                    "<color=#AEBAC8>Output</color>  <b>{}x{} @ {} cap</b>\n"
                    "<color=#AEBAC8>Frames Skipped</color>  <b>{}</b>\n"
                    "<color=#AEBAC8>Video FPS Average</color>  <b>{:.1f}</b>\n"
                    "<color=#AEBAC8>Frame Rate Loss</color>  <b>{:.1f}%</b>\n"
                    "<color=#AEBAC8>Decode Average</color>  <b>{:.2f} ms</b>\n"
                    "<color=#AEBAC8>Decode Peak</color>  <b>{:.2f} ms</b>",
                    data.sourceWidth,
                    data.sourceHeight,
                    data.sourceFps,
                    data.outputWidth,
                    data.outputHeight,
                    data.outputFpsLimit,
                    data.totalMissedVideoFrames,
                    data.averageVideoFramesPerSecond,
                    data.missedVideoFramePercent,
                    data.averageDecodeMilliseconds,
                    data.peakDecodeMilliseconds));
            }
        }
        catch(...)
        {
            // A transient TMPro failure must not interrupt preview playback.
            ErrorManager::Instance().RecordError(
                "Updating the performance panel",
                "Unity rejected the performance text update");
        }
    }

    void PerformancePanel::ShowWaitingMessage() noexcept
    {
        try
        {
            if(headsetStatistics_)
                headsetStatistics_->set_text(
                    "<color=#75DFFF><b>FRAME RATE</b></color>\n\n"
                    "Waiting for active playback...");
            if(videoStatistics_)
                videoStatistics_->set_text(
                    "<color=#75DFFF><b>Video</b></color>\n\n"
                    "Start a video preview to begin measuring.");
        }
        catch(...)
        {
            ErrorManager::Instance().RecordError(
                "Resetting the performance panel",
                "Unity rejected the waiting text update");
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
            // Update. On Quest/BSML 0.4.43 that portion does not consistently
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
        }
    }

    bool PerformancePanel::CreateAtDefaultPlacement()
    {
        screen_ = BSML::FloatingScreen::CreateFloatingScreen(
            {PanelWidth, PanelHeight},
            true,
            DefaultPosition,
            UnityEngine::Quaternion::Euler({0.0f, 0.0f, 0.0f}),
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
        background_ = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        ConfigureImage(
            background_, {0.0f, 0.0f}, {PanelWidth, PanelHeight},
            {0.025f, 0.055f, 0.095f, 0.98f});

        constexpr float borderThickness = 1.0f;
        borders_[0] = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        borders_[1] = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        borders_[2] = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        borders_[3] = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        const UnityEngine::Color borderColor{0.0f, 0.80f, 1.0f, 1.0f};
        ConfigureImage(borders_[0], {0.0f, PanelHeight * 0.5f - 0.5f},
                       {PanelWidth, borderThickness}, borderColor);
        ConfigureImage(borders_[1], {0.0f, -PanelHeight * 0.5f + 0.5f},
                       {PanelWidth, borderThickness}, borderColor);
        ConfigureImage(borders_[2], {-PanelWidth * 0.5f + 0.5f, 0.0f},
                       {borderThickness, PanelHeight}, borderColor);
        ConfigureImage(borders_[3], {PanelWidth * 0.5f - 0.5f, 0.0f},
                       {borderThickness, PanelHeight}, borderColor);

        headsetCard_ = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        ConfigureImage(
            headsetCard_, {-34.0f, -3.0f},
            {38.0f, 40.0f}, {0.055f, 0.105f, 0.165f, 1.0f});
        videoCard_ = BSML::Lite::CreateImage(screen_->get_transform(), whitePixel);
        ConfigureImage(
            videoCard_, {20.0f, -3.0f},
            {66.0f, 40.0f}, {0.055f, 0.105f, 0.165f, 1.0f});

        title_ = BSML::Lite::CreateText(
            screen_->get_transform(),
            "PERFORMANCE INFORMATION    |    GRAB ANYWHERE TO MOVE",
            TMPro::FontStyles::Bold,
            5.0f);
        if(title_)
        {
            title_->set_alignment(TMPro::TextAlignmentOptions::Center);
            title_->set_enableWordWrapping(false);
            title_->set_raycastTarget(false);
            auto rect = title_->get_rectTransform();
            CenterRect(rect);
            rect->set_anchoredPosition({0.0f, PanelHeight * 0.5f - 5.0f});
            rect->set_sizeDelta({PanelWidth - 8.0f, 8.0f});
        }

        headsetStatistics_ = BSML::Lite::CreateText(
            screen_->get_transform(), "", TMPro::FontStyles::Normal, 5.7f);
        videoStatistics_ = BSML::Lite::CreateText(
            screen_->get_transform(), "", TMPro::FontStyles::Normal, 5.0f);
        if(!headsetStatistics_ || !videoStatistics_)
        {
            Destroy();
            return false;
        }
        headsetStatistics_->set_alignment(TMPro::TextAlignmentOptions::Center);
        // Waiting/status sentences must wrap inside their card. The live
        // statistics already contain explicit line breaks, so enabling this
        // does not change their grouping unless a localized value is long.
        headsetStatistics_->set_enableWordWrapping(true);
        headsetStatistics_->set_raycastTarget(false);
        auto headsetRect = headsetStatistics_->get_rectTransform();
        CenterRect(headsetRect);
        headsetRect->set_anchoredPosition({-34.0f, -3.0f});
        headsetRect->set_sizeDelta({34.0f, 36.0f});

        videoStatistics_->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
        videoStatistics_->set_enableWordWrapping(true);
        videoStatistics_->set_raycastTarget(false);
        auto videoRect = videoStatistics_->get_rectTransform();
        CenterRect(videoRect);
        videoRect->set_anchoredPosition({20.0f, -3.0f});
        videoRect->set_sizeDelta({60.0f, 36.0f});

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
        background_ = nullptr;
        headsetCard_ = nullptr;
        videoCard_ = nullptr;
        borders_.fill(nullptr);
        title_ = nullptr;
        headsetStatistics_ = nullptr;
        videoStatistics_ = nullptr;
    }
}
