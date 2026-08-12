#include "BigScreen/PauseMenuLayoutSelector.hpp"

#include <algorithm>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/Settings.hpp"
#include "GlobalNamespace/PauseMenuManager.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "main.hpp"

namespace BigScreen {
    PauseMenuLayoutSelector& PauseMenuLayoutSelector::Instance()
    {
        static PauseMenuLayoutSelector selector;
        return selector;
    }

    void PauseMenuLayoutSelector::CreateUi(
        GlobalNamespace::PauseMenuManager* pauseMenu)
    {
        if((selector_ || videoScreenToggle_) || !pauseMenu)
            return;

        auto parent = pauseMenu->__cordl_internal_get__pauseContainerTransform();
        if(!parent)
        {
            PaperLogger.warn("Pause menu had no container for Screen Layout");
            return;
        }

        selector_ = BSML::Lite::CreateIncrementSetting(
            parent,
            "Screen Layout",
            0,
            1.0f,
            static_cast<float>(Settings::Instance().ActiveScreenLayout() + 1),
            1.0f,
            5.0f,
            UnityEngine::Vector2{52.0f, -6.0f},
            [this](float value) { LayoutChanged(value); });
        if(selector_)
        {
            selector_->get_gameObject()->set_name(
                "Big Screen Pause Screen Layout Selector");
            if(auto rect = selector_->get_transform().cast<UnityEngine::RectTransform>())
            {
                // Keep the control beside Beat Saber's central pause buttons.
                // Fixed bounds prevent it from changing the stock layout.
                rect->set_anchoredPosition({52.0f, -6.0f});
                rect->set_sizeDelta({42.0f, 8.0f});
            }
            BSML::Lite::AddHoverHint(
                selector_,
                "Changes the screen to Layout 1 through 5 immediately. Video playback stays synchronized with the song.");
        }

        videoScreenToggle_ = BSML::Lite::CreateToggle(
            parent,
            "Video Screen",
            true,
            UnityEngine::Vector2{52.0f, -16.0f},
            [this](bool enabled) { VideoScreenChanged(enabled); });
        if(videoScreenToggle_)
        {
            videoScreenToggle_->get_gameObject()->set_name(
                "Big Screen Pause Video Screen Toggle");
            if(auto rect = videoScreenToggle_->get_transform().cast<UnityEngine::RectTransform>())
            {
                rect->set_anchoredPosition({52.0f, -16.0f});
                rect->set_sizeDelta({42.0f, 8.0f});
            }
            BSML::Lite::AddHoverHint(
                videoScreenToggle_,
                "Shows or hides only the video screen for the current play. Map lighting and environment settings stay active, and your global Video In Map setting is not changed.");
        }

        MenuShown();
        PaperLogger.info("Created pause-menu Screen Layout and Video Screen controls");
    }

    void PauseMenuLayoutSelector::MenuShown()
    {
        if(!selector_ && !videoScreenToggle_)
            return;

        auto& playback = PlaybackSession::Instance();
        const auto& settings = Settings::Instance();
        const bool videoAvailable =
            settings.ModEnabled() &&
            playback.IsGameplayActive() &&
            playback.HasPreparedVideo();

        if(videoScreenToggle_)
        {
            videoScreenToggle_->get_gameObject()->SetActive(videoAvailable);
            if(videoAvailable)
            {
                videoScreenToggle_->currentValue = playback.GameplayScreenEnabled();
                if(videoScreenToggle_->toggle)
                    videoScreenToggle_->toggle->SetIsOnWithoutNotify(
                        playback.GameplayScreenEnabled());
                videoScreenToggle_->set_interactable(true);
            }
        }

        const bool layoutAvailable =
            videoAvailable && !playback.MapperPresentationActive();
        if(selector_)
        {
            selector_->get_gameObject()->SetActive(layoutAvailable);
            if(layoutAvailable)
            {
                selector_->set_Value(
                    static_cast<float>(settings.ActiveScreenLayout() + 1));
                selector_->set_interactable(true);
            }
        }
    }

    void PauseMenuLayoutSelector::LayoutChanged(float value)
    {
        ErrorManager::Instance().Guard("changing the paused screen layout", [&]() {
            const int layout = std::clamp(static_cast<int>(value) - 1, 0, 4);
            auto& settings = Settings::Instance();
            const int previousLayout = settings.ActiveScreenLayout();
            if(layout == previousLayout)
                return;

            settings.SetActiveScreenLayout(layout);
            if(!PlaybackSession::Instance().ApplyActiveScreenLayoutLive())
            {
                // Keep the persistent setting, visible selector, and actual
                // screen in agreement if Unity rejects the replacement mesh.
                settings.SetActiveScreenLayout(previousLayout);
                if(selector_)
                    selector_->set_Value(static_cast<float>(previousLayout + 1));
                return;
            }
            if(selector_)
                selector_->set_Value(static_cast<float>(layout + 1));
        });
    }

    void PauseMenuLayoutSelector::VideoScreenChanged(bool enabled)
    {
        const bool changed = ErrorManager::Instance().Guard(
            "changing the paused video screen",
            [&]() {
                PlaybackSession::Instance().SetGameplayScreenEnabled(enabled);
            });
        if(!changed && videoScreenToggle_)
        {
            const bool current = PlaybackSession::Instance().GameplayScreenEnabled();
            videoScreenToggle_->currentValue = current;
            if(videoScreenToggle_->toggle)
                videoScreenToggle_->toggle->SetIsOnWithoutNotify(current);
        }
    }

    void PauseMenuLayoutSelector::ForgetUi()
    {
        // PauseMenuManager owns and destroys the Unity object. Forget the
        // pointer so the next gameplay scene can create its own selector.
        selector_ = nullptr;
        videoScreenToggle_ = nullptr;
    }
}
