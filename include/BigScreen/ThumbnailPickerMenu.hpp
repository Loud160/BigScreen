// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "BigScreen/FrameDecoder.hpp"

namespace BSML { class SliderSetting; }
namespace GlobalNamespace { class BeatmapLevel; }
namespace HMUI { class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace UnityEngine {
    class GameObject;
    class Texture2D;
}
namespace UnityEngine::UI {
    class Button;
    class LayoutElement;
    class RawImage;
}

namespace BigScreen {
    /// Center-screen frame picker that captures one exact video frame as the
    /// selected map's thumbnail. Scrubbing reuses the normal FrameDecoder
    /// facade in a read-only session: the video file itself is never modified,
    /// and the map's playback assignment and timing are untouched.
    ///
    /// A map owns at most one picked thumbnail at a time. Saving writes the
    /// deterministic per-map PNG (replacing any previous pick in place) and
    /// records it in the video library manifest, which is what keeps Storage
    /// Maintenance from ever flagging a still-referenced picker thumbnail.
    class ThumbnailPickerMenu final {
    public:
        static ThumbnailPickerMenu& Instance();

        void CreateUi(
            HMUI::ViewController* controller,
            std::function<void()> onCancel,
            std::function<void(const std::string& thumbnailPath)> onSaved);
        void ForgetUi();
        /// Opens the selected map's active video for scrubbing. The caller has
        /// already stopped the library preview, so this decoder is the only
        /// reader of the file while the picker is on screen.
        void Show(GlobalNamespace::BeatmapLevel* level);
        /// Stops decoding when the picker leaves the center screen. Safe to
        /// call redundantly; Cancel and save both route through it.
        void Hide();
        void Tick();

    private:
        ThumbnailPickerMenu() = default;
        ThumbnailPickerMenu(const ThumbnailPickerMenu&) = delete;
        ThumbnailPickerMenu& operator=(const ThumbnailPickerMenu&) = delete;

        void RequestSeek(double mediaSeconds);
        void StepFrame(int direction);
        void DismissInstructions();
        void UseCurrentFrame();
        void Cancel();
        void PresentFrame(const VideoFrame& frame);
        void RefreshReadout();
        void SetStatus(const std::string& message, bool isError);

        HMUI::ViewController* controller_ = nullptr;
        std::function<void()> onCancel_;
        std::function<void(const std::string&)> onSaved_;

        TMPro::TextMeshProUGUI* title_ = nullptr;
        TMPro::TextMeshProUGUI* statusText_ = nullptr;
        UnityEngine::UI::RawImage* frameImage_ = nullptr;
        UnityEngine::UI::LayoutElement* frameImageLayout_ = nullptr;
        UnityEngine::Texture2D* frameTexture_ = nullptr;
        BSML::SliderSetting* scrubber_ = nullptr;
        // Retuned to the displayed frame's width so the bar always matches
        // the picture it scrubs, whatever the source's aspect ratio.
        UnityEngine::UI::LayoutElement* scrubberLayout_ = nullptr;
        TMPro::TextMeshProUGUI* scrubberText_ = nullptr;
        // First-open helper shown over the preview area; hidden forever (for
        // this visit) on the first scrub or frame-step interaction.
        TMPro::TextMeshProUGUI* instructionText_ = nullptr;
        UnityEngine::UI::Button* previousFrameButton_ = nullptr;
        UnityEngine::UI::Button* nextFrameButton_ = nullptr;
        UnityEngine::UI::Button* useFrameButton_ = nullptr;

        // The picker owns its own decoder instance so gameplay and menu
        // preview sessions can never inherit a scrubbed position or a
        // half-open file from thumbnail picking.
        FrameDecoder decoder_;
        std::string levelId_;
        std::string songName_;
        std::filesystem::path videoPath_;
        // Unity textures interpret row zero as the bottom of the image while
        // the decoder emits ordinary top-down RGBA. Frames are flipped through
        // this scratch buffer once at upload so the on-screen preview and the
        // encoded PNG share one correct orientation.
        std::vector<std::uint8_t> flipScratch_;
        double durationSeconds_ = 0.0;
        double nominalFrameSeconds_ = 1.0 / 30.0;
        double displayedSeconds_ = 0.0;
        double displayedFrameDuration_ = 0.0;
        // Laser scrubbing fires the slider callback many times per second.
        // Batch those into one decoder request after the handle rests briefly
        // so the worker seeks once instead of chasing every intermediate value.
        double pendingSeekSeconds_ = -1.0;
        float pendingSeekIssueTime_ = 0.0f;
        // Until this instant, arriving frames update only the readout text;
        // the slider handle stays under the player's active drag.
        float scrubberFollowResumeTime_ = 0.0f;
        bool suppressScrubberCallback_ = false;
        // While true, the how-to message owns the preview area: decoded
        // frames stay queued in the decoder mailbox and are presented the
        // instant the first real interaction dismisses the message.
        bool instructionsVisible_ = false;
        bool hasDisplayedFrame_ = false;
        bool decodeFailed_ = false;
        bool visible_ = false;
    };
}
