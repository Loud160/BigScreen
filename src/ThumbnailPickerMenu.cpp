// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/ThumbnailPickerMenu.hpp"
#include "BigScreen/UiUtility.hpp"
#include "BigScreen/Utility.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/ImageConversion.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/RawImage.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/Backgroundable.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        using UiUtility::EnsureLayout;

        // Content column width. Deliberately narrow: the darkening plate is
        // sized from this, and it should hug the controls with only a small
        // margin rather than spanning the whole center screen.
        constexpr float PanelWidth = 92.0f;
        // The preview box bounds every source shape: wide video fills the
        // width and portrait or rotated video fills the height instead.
        constexpr float FrameBoxWidth = 82.0f;
        constexpr float FrameBoxHeight = 40.0f;
        // Width of a standard 16:9 picture inside the preview box; used for
        // the scrubber before the first frame reveals the real display width.
        constexpr float DefaultFrameWidth = 71.0f;
        // A portrait video's picture can be ~22 units wide; a scrubber that
        // narrow is undraggable, so the bar never shrinks below this.
        constexpr float MinimumScrubberWidth = 48.0f;
        // Plate margin past the content column on each side / top / bottom.
        constexpr float PlatePadding = 2.0f;
        // Sum of the fixed row heights plus inter-row spacing laid out below.
        constexpr float ContentHeight = 5.5f + FrameBoxHeight + 7.0f + 7.5f +
            5.5f + 8.0f + 5 * 0.7f;
        // The scrubber publishes normalized [0, 1] positions; one thousand
        // steps are finer than a VR laser can hold, matching the library
        // preview scrubber's proven increment.
        constexpr float ScrubIncrement = 0.001f;
        // One decoder request after the handle rests. Coalescing here keeps
        // the worker from re-seeking for every intermediate slider value.
        constexpr float ScrubSettleSeconds = 0.15f;
        // Handle-follow suppression window after the last drag callback, the
        // same idea as the library preview's PreviewScrubFollowDelay.
        constexpr float ScrubFollowDelaySeconds = 0.30f;

        void ConfigureLayout(
            UnityEngine::Component* component,
            float preferredWidth,
            float preferredHeight,
            float flexibleWidth = 0.0f)
        {
            auto* layout = EnsureLayout(component);
            if(!layout) return;
            if(preferredWidth >= 0.0f) layout->set_preferredWidth(preferredWidth);
            if(preferredHeight >= 0.0f) layout->set_preferredHeight(preferredHeight);
            layout->set_flexibleWidth(flexibleWidth);
            layout->set_flexibleHeight(0.0f);
        }

        std::string FormatPickerTime(double seconds)
        {
            seconds = std::max(0.0, seconds);
            const auto totalMilliseconds =
                static_cast<long long>(std::llround(seconds * 1000.0));
            const auto minutes = totalMilliseconds / 60'000;
            const auto remainderSeconds = (totalMilliseconds / 1000) % 60;
            const auto milliseconds = totalMilliseconds % 1000;
            char text[32];
            std::snprintf(
                text,
                sizeof(text),
                "%lld:%02lld.%03lld",
                minutes,
                remainderSeconds,
                milliseconds);
            return text;
        }
    }

    ThumbnailPickerMenu& ThumbnailPickerMenu::Instance()
    {
        static ThumbnailPickerMenu menu;
        return menu;
    }

    void ThumbnailPickerMenu::ForgetUi()
    {
        decoder_.Close();
        controller_ = nullptr;
        onCancel_ = {};
        onSaved_ = {};
        title_ = nullptr;
        statusText_ = nullptr;
        frameImage_ = nullptr;
        frameImageLayout_ = nullptr;
        scrubberLayout_ = nullptr;
        instructionText_ = nullptr;
        instructionsVisible_ = false;
        // The texture belongs to the scene being discarded; MenuCore destroys
        // it with the rest of the hierarchy, so only the reference is dropped.
        frameTexture_ = nullptr;
        scrubber_ = nullptr;
        scrubberText_ = nullptr;
        previousFrameButton_ = nullptr;
        nextFrameButton_ = nullptr;
        useFrameButton_ = nullptr;
        levelId_.clear();
        songName_.clear();
        videoPath_.clear();
        flipScratch_.clear();
        durationSeconds_ = 0.0;
        nominalFrameSeconds_ = 1.0 / 30.0;
        displayedSeconds_ = 0.0;
        displayedFrameDuration_ = 0.0;
        pendingSeekSeconds_ = -1.0;
        pendingSeekIssueTime_ = 0.0f;
        scrubberFollowResumeTime_ = 0.0f;
        suppressScrubberCallback_ = false;
        hasDisplayedFrame_ = false;
        decodeFailed_ = false;
        visible_ = false;
    }

    void ThumbnailPickerMenu::CreateUi(
        HMUI::ViewController* controller,
        std::function<void()> onCancel,
        std::function<void(const std::string&)> onSaved)
    {
        controller_ = controller;
        onCancel_ = std::move(onCancel);
        onSaved_ = std::move(onSaved);

        // Darkening plate behind the whole picker. A plain blank-sprite
        // ImageView failed to render on this center view controller, so this
        // uses BSML's Backgroundable with the stock round-rect-panel
        // template instead -- the exact machinery behind the bg attribute on
        // the editor's storage/playback panels, which visibly renders there.
        auto* backgroundObject =
            UnityEngine::GameObject::New_ctor("BigScreenThumbnailPickerPlate");
        backgroundObject->get_transform()->SetParent(
            controller->get_transform(), false);
        if(auto* backgroundable =
               backgroundObject->AddComponent<BSML::Backgroundable*>())
        {
            backgroundable->ApplyBackground("round-rect-panel");
            backgroundable->ApplyColor({0.0f, 0.0f, 0.0f, 1.0f});
            backgroundable->ApplyAlpha(0.88f);
            if(auto* backgroundImage = backgroundable->background)
            {
                backgroundImage->set_gradient(false);
                backgroundImage->set_raycastTarget(false);
            }
        }
        if(auto backgroundRect = backgroundObject->get_transform()
               .try_cast<UnityEngine::RectTransform>().value_or(nullptr))
        {
            // Sized from the content itself: column width plus a small margin
            // and the exact stacked row heights plus the same margin, so the
            // plate reads as this control group's card, not a screen cover.
            backgroundRect->set_anchorMin({0.5f, 0.5f});
            backgroundRect->set_anchorMax({0.5f, 0.5f});
            backgroundRect->set_pivot({0.5f, 0.5f});
            backgroundRect->set_anchoredPosition({0.0f, 0.0f});
            backgroundRect->set_sizeDelta(
                {PanelWidth + 2.0f * PlatePadding,
                 ContentHeight + 2.0f * PlatePadding});
        }
        backgroundObject->get_transform()->SetAsFirstSibling();

        auto* root = BSML::Lite::CreateVerticalLayoutGroup(controller);
        root->set_spacing(0.7f);
        root->set_childControlWidth(true);
        root->set_childControlHeight(true);
        root->set_childForceExpandWidth(true);
        root->set_childForceExpandHeight(false);
        // The column must center in the view controller the same way the
        // plate does, or the controls ride above a plate that appears to
        // extend far below them.
        root->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        if(auto* fitter = root->get_gameObject()
               ->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
        {
            fitter->set_horizontalFit(
                UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
            fitter->set_verticalFit(
                UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
        }
        if(auto rect = root->get_rectTransform())
        {
            rect->set_anchorMin({0.5f, 0.0f});
            rect->set_anchorMax({0.5f, 1.0f});
            rect->set_pivot({0.5f, 0.5f});
            rect->set_anchoredPosition({0.0f, 0.0f});
            rect->set_sizeDelta({PanelWidth, -4.0f});
        }

        title_ = BSML::Lite::CreateText(
            root, "Set Video Thumbnail", TMPro::FontStyles::Bold, 4.0f);
        ConfigureLayout(title_, PanelWidth, 5.5f, 1.0f);
        title_->set_alignment(TMPro::TextAlignmentOptions::Center);
        title_->set_enableWordWrapping(false);
        title_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);

        // A centered row hosts the RawImage so odd aspect ratios keep the
        // preview in the middle instead of hugging the panel's left edge.
        auto* frameRow = BSML::Lite::CreateHorizontalLayoutGroup(root);
        frameRow->set_childControlWidth(true);
        frameRow->set_childControlHeight(true);
        frameRow->set_childForceExpandWidth(false);
        frameRow->set_childForceExpandHeight(false);
        frameRow->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(frameRow, PanelWidth, FrameBoxHeight, 1.0f);

        // RawImage displays the decode texture directly. Unlike ImageView it
        // needs no Sprite wrapper, so replacing pixels never orphans sprites.
        auto* frameObject = UnityEngine::GameObject::New_ctor("BigScreenThumbnailFrame");
        frameObject->get_transform()->SetParent(frameRow->get_transform(), false);
        frameImage_ = frameObject->AddComponent<UnityEngine::UI::RawImage*>();
        frameImage_->set_color({0.06f, 0.07f, 0.09f, 1.0f});
        frameImage_->set_raycastTarget(false);
        frameImageLayout_ = EnsureLayout(frameImage_);
        if(frameImageLayout_)
        {
            frameImageLayout_->set_preferredWidth(FrameBoxWidth);
            frameImageLayout_->set_preferredHeight(FrameBoxHeight);
        }

        // First-open instructions, drawn over the (still dark) preview area.
        // Child of the RawImage so it needs no layout slot of its own and is
        // guaranteed to cover exactly the preview box.
        instructionText_ = BSML::Lite::CreateText(
            frameImage_->get_transform(),
            "Drag the bar below to move through the video.\nUse the two frame buttons to step one frame at a time.\nWhen the frame you want is shown, press Use This Frame to save it as this map's thumbnail.",
            3.2f);
        instructionText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        instructionText_->set_enableWordWrapping(true);
        instructionText_->set_enableAutoSizing(true);
        instructionText_->set_fontSizeMin(2.6f);
        instructionText_->set_fontSizeMax(3.2f);
        instructionText_->set_color({0.92f, 0.95f, 1.0f, 1.0f});
        if(auto instructionRect = instructionText_->get_transform()
               .cast<UnityEngine::RectTransform>())
        {
            instructionRect->set_anchorMin({0.0f, 0.0f});
            instructionRect->set_anchorMax({1.0f, 1.0f});
            instructionRect->set_pivot({0.5f, 0.5f});
            instructionRect->set_anchoredPosition({0.0f, 0.0f});
            // Inset the text from the box edges so lines never touch them.
            instructionRect->set_sizeDelta({-6.0f, -4.0f});
        }

        // The slider fills whatever width it is allocated (the label-slot fix
        // below), so its allocation is deliberately the preview box's width:
        // a centered row constrains the bar to sit exactly under the frame
        // instead of spanning the entire panel.
        auto* scrubberRow = BSML::Lite::CreateHorizontalLayoutGroup(root);
        scrubberRow->set_childControlWidth(true);
        scrubberRow->set_childControlHeight(true);
        scrubberRow->set_childForceExpandWidth(false);
        scrubberRow->set_childForceExpandHeight(false);
        scrubberRow->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(scrubberRow, PanelWidth, 7.0f, 1.0f);
        scrubber_ = BSML::Lite::CreateSliderSetting(
            scrubberRow,
            "",
            ScrubIncrement,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            false,
            {0.0f, 0.0f},
            [this](float value)
            {
                if(suppressScrubberCallback_ || durationSeconds_ <= 0.0)
                    return;
                DismissInstructions();
                RequestSeek(value * durationSeconds_);
            });
        // BSML clones this control from a game settings row and pins that
        // row's LayoutElement to 90 units wide. Overwrite BOTH minimum and
        // preferred width or the template minimum keeps winning and the bar
        // renders wider than the picture above it. PresentFrame retunes this
        // to the actual displayed frame width once the video's shape is known.
        scrubberLayout_ = EnsureLayout(scrubber_);
        if(scrubberLayout_)
        {
            scrubberLayout_->set_minWidth(DefaultFrameWidth);
            scrubberLayout_->set_preferredWidth(DefaultFrameWidth);
            scrubberLayout_->set_flexibleWidth(0.0f);
            scrubberLayout_->set_preferredHeight(7.0f);
            scrubberLayout_->set_flexibleHeight(0.0f);
        }
        scrubber_->formatter = [this](float normalized) -> StringW
        {
            return FormatPickerTime(normalized * durationSeconds_) + " / " +
                FormatPickerTime(durationSeconds_);
        };
        scrubberText_ = scrubber_->text;
        if(scrubberText_)
        {
            scrubberText_->set_fontSize(2.4f);
            scrubberText_->set_alignment(TMPro::TextAlignmentOptions::Center);
            scrubberText_->set_color(UnityEngine::Color::get_white());
            if(auto textRect = scrubberText_->get_transform()
                   .cast<UnityEngine::RectTransform>())
            {
                textRect->set_anchorMin({0.0f, 0.0f});
                textRect->set_anchorMax({1.0f, 1.0f});
                textRect->set_pivot({0.5f, 0.5f});
                textRect->set_anchoredPosition({0.0f, 0.0f});
                textRect->set_sizeDelta({0.0f, 0.0f});
                textRect->SetAsLastSibling();
            }
        }
        // The stock settings slider reserves a fixed 52-unit control anchored
        // to the row's right edge because it normally shares that row with a
        // label. Left unfixed, the bar visibly hangs off the panel's right
        // side. Remove the empty label and stretch the draggable slider over
        // the entire row so it sits centered above the frame-step buttons.
        if(auto title = scrubber_->get_transform()->Find("Title"))
            title->get_gameObject()->SetActive(false);
        if(scrubber_->slider)
        {
            auto sliderRect = scrubber_->slider->get_transform()
                .cast<UnityEngine::RectTransform>();
            sliderRect->set_anchorMin({0.0f, 0.0f});
            sliderRect->set_anchorMax({1.0f, 1.0f});
            sliderRect->set_pivot({0.5f, 0.5f});
            sliderRect->set_anchoredPosition({0.0f, 0.0f});
            sliderRect->set_sizeDelta({0.0f, 0.0f});
        }
        BSML::Lite::AddHoverHint(
            scrubber_,
            "Drag to any point in the video. The two frame buttons then move one frame at a time for an exact pick.");

        auto* stepRow = BSML::Lite::CreateHorizontalLayoutGroup(root);
        stepRow->set_spacing(1.0f);
        stepRow->set_childControlWidth(true);
        stepRow->set_childControlHeight(true);
        stepRow->set_childForceExpandWidth(false);
        stepRow->set_childForceExpandHeight(false);
        stepRow->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(stepRow, PanelWidth, 7.5f, 1.0f);
        previousFrameButton_ = BSML::Lite::CreateUIButton(
            stepRow, "< Previous Frame", {0.0f, 0.0f}, {26.0f, 7.0f},
            [this]() { StepFrame(-1); });
        ConfigureLayout(previousFrameButton_, 26.0f, 7.0f);
        BSML::Lite::SetButtonTextSize(previousFrameButton_, 2.35f);
        nextFrameButton_ = BSML::Lite::CreateUIButton(
            stepRow, "Next Frame >", {0.0f, 0.0f}, {26.0f, 7.0f},
            [this]() { StepFrame(1); });
        ConfigureLayout(nextFrameButton_, 26.0f, 7.0f);
        BSML::Lite::SetButtonTextSize(nextFrameButton_, 2.35f);

        statusText_ = BSML::Lite::CreateText(root, "", 2.55f);
        ConfigureLayout(statusText_, PanelWidth, 5.5f, 1.0f);
        statusText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        statusText_->set_enableWordWrapping(false);
        statusText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);

        auto* footer = BSML::Lite::CreateHorizontalLayoutGroup(root);
        footer->set_spacing(1.2f);
        footer->set_childControlWidth(true);
        footer->set_childControlHeight(true);
        footer->set_childForceExpandWidth(false);
        footer->set_childForceExpandHeight(false);
        footer->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(footer, PanelWidth, 8.0f, 1.0f);
        auto* cancelButton = BSML::Lite::CreateUIButton(
            footer, "Cancel", {0.0f, 0.0f}, {24.0f, 8.0f},
            [this]() { Cancel(); });
        ConfigureLayout(cancelButton, 24.0f, 8.0f);
        BSML::Lite::SetButtonTextSize(cancelButton, 2.6f);
        useFrameButton_ = BSML::Lite::CreateUIButton(
            footer, "Use This Frame", {0.0f, 0.0f}, {30.0f, 8.0f},
            [this]() { UseCurrentFrame(); });
        ConfigureLayout(useFrameButton_, 30.0f, 8.0f);
        BSML::Lite::SetButtonTextSize(useFrameButton_, 2.6f);
        if(auto* useText = useFrameButton_->get_gameObject()
               ->GetComponentInChildren<TMPro::TextMeshProUGUI*>())
            useText->set_color({0.20f, 1.0f, 0.36f, 1.0f});
        BSML::Lite::AddHoverHint(
            useFrameButton_,
            "Saves the displayed frame as this map's thumbnail. The video file itself is read-only here and is never changed.");
    }

    void ThumbnailPickerMenu::Show(GlobalNamespace::BeatmapLevel* level)
    {
        decoder_.Close();
        visible_ = true;
        hasDisplayedFrame_ = false;
        decodeFailed_ = false;
        pendingSeekSeconds_ = -1.0;
        scrubberFollowResumeTime_ = 0.0f;
        // Every visit starts with the how-to message covering the preview.
        // A leftover texture from the previous visit must not show behind it.
        instructionsVisible_ = true;
        if(instructionText_)
            instructionText_->get_gameObject()->SetActive(true);
        if(frameImage_)
        {
            frameImage_->set_texture(nullptr);
            frameImage_->set_color({0.06f, 0.07f, 0.09f, 1.0f});
        }
        if(frameTexture_)
        {
            UnityEngine::Object::Destroy(frameTexture_);
            frameTexture_ = nullptr;
        }
        if(frameImageLayout_)
        {
            frameImageLayout_->set_preferredWidth(FrameBoxWidth);
            frameImageLayout_->set_preferredHeight(FrameBoxHeight);
        }
        if(scrubberLayout_)
        {
            scrubberLayout_->set_minWidth(DefaultFrameWidth);
            scrubberLayout_->set_preferredWidth(DefaultFrameWidth);
        }
        displayedSeconds_ = 0.0;
        displayedFrameDuration_ = 0.0;
        durationSeconds_ = 0.0;
        levelId_.clear();
        songName_.clear();
        videoPath_.clear();
        if(useFrameButton_) useFrameButton_->set_interactable(false);
        if(previousFrameButton_) previousFrameButton_->set_interactable(false);
        if(nextFrameButton_) nextFrameButton_->set_interactable(false);
        if(scrubber_)
        {
            suppressScrubberCallback_ = true;
            scrubber_->set_Value(0.0f);
            suppressScrubberCallback_ = false;
        }

        if(!level || !level->levelID)
        {
            SetStatus("Select a song before setting a thumbnail.", true);
            return;
        }
        levelId_ = std::string(level->levelID);
        songName_ = level->songName
            ? std::string(level->songName) : "Unknown Song";
        if(title_)
            title_->set_text("Set Video Thumbnail - " + songName_);

        const auto descriptor = VideoLibrary::Instance().Describe(level);
        if(!descriptor.playableConfig)
        {
            SetStatus("This map has no playable video to take a frame from.", true);
            return;
        }
        videoPath_ = descriptor.playableConfig->videoPath;

        // 720p bounds both the preview upload cost and the saved PNG size
        // while remaining far sharper than any surface that shows the
        // thumbnail. Sources below the bound are never upscaled.
        std::string error;
        if(!decoder_.Open(videoPath_, 720, error))
        {
            decodeFailed_ = true;
            SetStatus("Could not open the video: " + error, true);
            ErrorManager::Instance().RecordError(
                "Opening a video for the thumbnail picker", error);
            return;
        }
        durationSeconds_ = decoder_.DurationSeconds();
        const auto sourceFps = decoder_.SourceFramesPerSecond();
        nominalFrameSeconds_ = sourceFps > 1.0 ? 1.0 / sourceFps : 1.0 / 30.0;
        SetStatus("Loading the first frame...", false);
        decoder_.Request(0.0);
    }

    void ThumbnailPickerMenu::Hide()
    {
        decoder_.Close();
        visible_ = false;
        hasDisplayedFrame_ = false;
        pendingSeekSeconds_ = -1.0;
    }

    void ThumbnailPickerMenu::Cancel()
    {
        Hide();
        if(onCancel_)
            onCancel_();
    }

    void ThumbnailPickerMenu::RequestSeek(double mediaSeconds)
    {
        if(!decoder_.IsOpen())
            return;
        const auto lastPickable = durationSeconds_ > 0.0
            ? std::max(0.0, durationSeconds_ - nominalFrameSeconds_ * 0.5)
            : mediaSeconds;
        pendingSeekSeconds_ = std::clamp(mediaSeconds, 0.0, lastPickable);
        const auto now = UnityEngine::Time::get_realtimeSinceStartup();
        pendingSeekIssueTime_ = now + ScrubSettleSeconds;
        scrubberFollowResumeTime_ = now + ScrubFollowDelaySeconds;
    }

    void ThumbnailPickerMenu::DismissInstructions()
    {
        if(!instructionsVisible_)
            return;
        instructionsVisible_ = false;
        if(instructionText_)
            instructionText_->get_gameObject()->SetActive(false);
        // The first decoded frame has been waiting in the decoder mailbox
        // behind the message; the next Tick presents it immediately.
    }

    void ThumbnailPickerMenu::StepFrame(int direction)
    {
        // The very first press is treated as "show me the video": it reveals
        // the already-decoded first frame rather than stepping past it.
        DismissInstructions();
        if(!decoder_.IsOpen() || !hasDisplayedFrame_)
            return;
        // Forward lands just inside the next frame's interval using the
        // container's own duration for this frame; backward lands inside the
        // previous interval. Both survive variable-frame-rate timing because
        // the decoder resolves whichever frame covers the requested instant.
        const auto forwardStep = displayedFrameDuration_ > 0.0
            ? displayedFrameDuration_
            : nominalFrameSeconds_;
        const auto target = direction > 0
            ? displayedSeconds_ + forwardStep + nominalFrameSeconds_ * 0.10
            : displayedSeconds_ - nominalFrameSeconds_ * 0.60;
        RequestSeek(target);
        // Frame stepping is an exact operation, not a drag: skip the scrub
        // settle delay AND the handle-follow suppression so the bar snaps to
        // the stepped frame the moment it is decoded, keeping the scrubber in
        // sync with what the preview actually shows.
        pendingSeekIssueTime_ = 0.0f;
        scrubberFollowResumeTime_ = 0.0f;
    }

    void ThumbnailPickerMenu::PresentFrame(const VideoFrame& frame)
    {
        if(!frameImage_ || frame.width <= 0 || frame.height <= 0 ||
           frame.rgba.size() <
               static_cast<std::size_t>(frame.width) * frame.height * 4)
            return;

        if(!frameTexture_ ||
           frameTexture_->get_width() != frame.width ||
           frameTexture_->get_height() != frame.height)
        {
            // A differently-sized source (another map's video) replaces the
            // texture; release the old GPU allocation instead of letting it
            // linger until the whole menu scene is torn down.
            if(frameTexture_)
                UnityEngine::Object::Destroy(frameTexture_);
            frameTexture_ = UnityEngine::Texture2D::New_ctor(
                frame.width,
                frame.height,
                UnityEngine::TextureFormat::RGBA32,
                false,
                false);
            frameImage_->set_texture(frameTexture_);
            frameImage_->set_color(UnityEngine::Color::get_white());
            if(frameImageLayout_)
            {
                // Fit the source aspect inside the fixed preview box.
                const auto aspect = static_cast<float>(frame.width) /
                    static_cast<float>(frame.height);
                float width = FrameBoxHeight * aspect;
                float height = FrameBoxHeight;
                if(width > FrameBoxWidth)
                {
                    width = FrameBoxWidth;
                    height = FrameBoxWidth / aspect;
                }
                frameImageLayout_->set_preferredWidth(width);
                frameImageLayout_->set_preferredHeight(height);
                // Keep the scrubber exactly as wide as the picture it
                // scrubs, with a floor so portrait video stays draggable.
                if(scrubberLayout_)
                {
                    const auto scrubberWidth =
                        std::max(width, MinimumScrubberWidth);
                    scrubberLayout_->set_minWidth(scrubberWidth);
                    scrubberLayout_->set_preferredWidth(scrubberWidth);
                }
            }
        }

        // Flip decoder top-down rows into Unity's bottom-up layout once so
        // the preview needs no UV trick and EncodeToPNG needs no second pass.
        const auto stride = static_cast<std::size_t>(frame.width) * 4;
        flipScratch_.resize(stride * frame.height);
        for(int row = 0; row < frame.height; ++row)
        {
            std::copy_n(
                frame.rgba.data() + static_cast<std::size_t>(row) * stride,
                stride,
                flipScratch_.data() +
                    static_cast<std::size_t>(frame.height - 1 - row) * stride);
        }
        frameTexture_->LoadRawTextureData(
            System::IntPtr(flipScratch_.data()),
            static_cast<std::int32_t>(flipScratch_.size()));
        frameTexture_->Apply(false, false);

        displayedSeconds_ = frame.presentationSeconds;
        displayedFrameDuration_ = frame.durationSeconds;
        hasDisplayedFrame_ = true;
        if(useFrameButton_) useFrameButton_->set_interactable(true);
        if(previousFrameButton_) previousFrameButton_->set_interactable(true);
        if(nextFrameButton_) nextFrameButton_->set_interactable(true);
        RefreshReadout();
    }

    void ThumbnailPickerMenu::RefreshReadout()
    {
        // While the player is still dragging (a seek is pending or was just
        // issued), decoded frames must not yank the handle away from the
        // laser; the readout text alone is enough feedback mid-drag.
        const bool scrubbing = pendingSeekSeconds_ >= 0.0 ||
            UnityEngine::Time::get_realtimeSinceStartup() <
                scrubberFollowResumeTime_;
        if(scrubber_ && durationSeconds_ > 0.0 && !scrubbing)
        {
            suppressScrubberCallback_ = true;
            scrubber_->set_Value(static_cast<float>(
                std::clamp(displayedSeconds_ / durationSeconds_, 0.0, 1.0)));
            suppressScrubberCallback_ = false;
        }
        // Times already live on the scrubber bar itself, so this line carries
        // only the frame position. Frame numbers are derived from the nominal
        // rate and carry a tilde: variable-frame-rate video has no exact
        // universal numbering.
        const auto frameNumber = static_cast<long long>(
            std::llround(displayedSeconds_ / nominalFrameSeconds_)) + 1;
        const auto totalFrames = durationSeconds_ > 0.0
            ? std::max(
                  frameNumber,
                  static_cast<long long>(
                      std::llround(durationSeconds_ / nominalFrameSeconds_)))
            : frameNumber;
        SetStatus(
            "Frame ~" + std::to_string(frameNumber) +
                " of ~" + std::to_string(totalFrames),
            false);
    }

    void ThumbnailPickerMenu::SetStatus(const std::string& message, bool isError)
    {
        if(!statusText_)
            return;
        statusText_->set_text(message);
        statusText_->set_color(isError
            ? UnityEngine::Color{1.0f, 0.36f, 0.32f, 1.0f}
            : UnityEngine::Color{0.78f, 0.83f, 0.90f, 1.0f});
    }

    void ThumbnailPickerMenu::UseCurrentFrame()
    {
        if(!hasDisplayedFrame_ || !frameTexture_ || levelId_.empty())
            return;

        try
        {
            // The displayed texture already holds exactly the pixels the user
            // approved, in PNG row order. Encoding it directly guarantees the
            // saved thumbnail matches the preview and touches no video bytes.
            auto png = UnityEngine::ImageConversion::EncodeToPNG(frameTexture_);
            if(!png || png.size() == 0)
                throw std::runtime_error("Unity returned an empty PNG encode");

            auto& library = VideoLibrary::Instance();
            const auto finalPath = library.LocalThumbnailPath(levelId_);
            const auto temporaryPath =
                std::filesystem::path(finalPath.string() + ".tmp");
            {
                std::ofstream stream(
                    temporaryPath, std::ios::binary | std::ios::trunc);
                stream.write(
                    reinterpret_cast<const char*>(png.begin()),
                    static_cast<std::streamsize>(png.size()));
                stream.flush();
                if(!stream)
                    throw std::runtime_error(
                        "Could not write the thumbnail PNG to storage");
            }
            // A replace is atomic: the previous pick's bytes disappear in the
            // same rename that publishes the new pick, so no reader can see a
            // partial PNG and no second thumbnail file ever accumulates.
            std::error_code renameError;
            std::filesystem::rename(temporaryPath, finalPath, renameError);
            if(renameError)
            {
                std::error_code cleanupError;
                std::filesystem::remove(temporaryPath, cleanupError);
                throw std::runtime_error(
                    "Could not replace the thumbnail file: " +
                    renameError.message());
            }
            library.CommitLocalThumbnail(levelId_);
            PaperLogger.info(
                "Saved a picked thumbnail frame at {:.3f}s for '{}'",
                displayedSeconds_,
                levelId_);

            const auto savedPath = finalPath.string();
            Hide();
            if(onSaved_)
                onSaved_(savedPath);
        }
        catch(const std::exception& exception)
        {
            SetStatus(
                std::string("Could not save the thumbnail: ") +
                    exception.what(),
                true);
            ErrorManager::Instance().RecordError(
                "Saving a picked video thumbnail", exception.what());
        }
    }

    void ThumbnailPickerMenu::Tick()
    {
        if(!visible_ || decodeFailed_ || !decoder_.IsOpen())
            return;

        if(auto error = decoder_.TakeError())
        {
            decodeFailed_ = true;
            SetStatus("Video decoding failed: " + *error, true);
            ErrorManager::Instance().RecordError(
                "Decoding a frame for the thumbnail picker", *error);
            if(useFrameButton_) useFrameButton_->set_interactable(false);
            if(previousFrameButton_) previousFrameButton_->set_interactable(false);
            if(nextFrameButton_) nextFrameButton_->set_interactable(false);
            return;
        }

        if(pendingSeekSeconds_ >= 0.0 &&
           UnityEngine::Time::get_realtimeSinceStartup() >= pendingSeekIssueTime_)
        {
            decoder_.Request(pendingSeekSeconds_);
            pendingSeekSeconds_ = -1.0;
        }

        // While the how-to message owns the preview area, leave decoded
        // frames waiting in the decoder mailbox. The first interaction
        // dismisses the message and the newest frame appears instantly.
        if(instructionsVisible_)
            return;

        VideoFrame frame;
        if(decoder_.TryTake(frame))
        {
            PresentFrame(frame);
            decoder_.Recycle(std::move(frame));
        }
    }
}
