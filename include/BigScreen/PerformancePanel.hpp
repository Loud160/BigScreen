// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace BSML { class FloatingScreen; }
namespace HMUI { class ImageView; }
namespace TMPro { class TextMeshProUGUI; }

namespace BigScreen {
    struct PerformancePanelData {
        bool gameplay = false;
        double minimumFps = 0.0;
        double averageFps = 0.0;
        double maximumFps = 0.0;
        std::uint64_t sampledFrames = 0;
        int videoWidth = 0;
        int videoHeight = 0;
        double sourceFps = 0.0;
        int outputFpsLimit = 0;
        std::uint64_t skippedVideoFrames = 0;
        std::uint64_t expectedVideoFrames = 0;
        std::uint64_t totalMissedVideoFrames = 0;
        double averageVideoFramesPerSecond = 0.0;
        double missedVideoFramePercent = 0.0;
        double averageDecodeMilliseconds = 0.0;
        double peakDecodeMilliseconds = 0.0;
        std::string decodeMethod;
        std::string decoderRuntime;
        std::string codec;
        std::string presentationMethod;
    };

    /// Owns the movable performance-information panel shown in Big Screen's
    /// menu and during video gameplay. Menu and gameplay share one persisted
    /// transform so a placement chosen in the menu remains useful in-map.
    class PerformancePanel final {
    public:
        static PerformancePanel& Instance();

        /// Marks Big Screen's menu active and recreates the panel at its saved
        /// placement when diagnostics are enabled.
        void ActivateMenu() noexcept;
        /// Recreates the same panel for a video map. The BSML Top handle stays
        /// active so Beat Saber's pointer can move it, including from pause UI.
        void ActivateGameplay() noexcept;
        /// Applies the live toggle. Disabling first commits the current
        /// placement; re-enabling restores it.
        void SetEnabled(bool enabled) noexcept;
        /// Restores the shared menu/gameplay transform to its safe default and
        /// moves an active panel immediately without rebuilding its contents.
        void ResetPlacement() noexcept;
        /// Removes the panel when Big Screen's menu is closed.
        void SuspendMenu() noexcept;
        void SuspendGameplay() noexcept;
        /// Replaces the grouped values without rebuilding or moving the UI.
        void SetStatistics(const PerformancePanelData& data) noexcept;
        /// Keeps the panel visible but explains that a preview is not running.
        void ShowWaitingMessage() noexcept;
        /// Completes BSML's controller-relative six-degree-of-freedom motion.
        /// Position remains owned by the native floating-screen handler; this
        /// method supplies the wrist rotation that is unreliable with the
        /// panel-wide hidden hit target used by the diagnostics display.
        void TickInteraction() noexcept;

    private:
        PerformancePanel() = default;

        bool CreateAtSavedPlacement();
        void ActivateForContext(bool gameplay) noexcept;
        void SaveCurrentPlacement() noexcept;
        /// Writes the header subtitle and every fixed-height statistics row.
        /// A null pointer renders the waiting placeholders.
        void ApplyRows(const PerformancePanelData* data) noexcept;
        void Destroy() noexcept;

        enum class Context { None, Menu, Gameplay };
        Context context_ = Context::None;
        BSML::FloatingScreen* screen_ = nullptr;
        HMUI::ImageView* background_ = nullptr;
        HMUI::ImageView* headsetCard_ = nullptr;
        HMUI::ImageView* videoCard_ = nullptr;
        HMUI::ImageView* titleCard_ = nullptr;
        HMUI::ImageView* instructionCard_ = nullptr;
        std::array<HMUI::ImageView*, 4> borders_{};
        std::array<HMUI::ImageView*, 4> titleBorders_{};
        std::array<HMUI::ImageView*, 4> instructionBorders_{};
        HMUI::ImageView* columnDivider_ = nullptr;
        TMPro::TextMeshProUGUI* title_ = nullptr;
        TMPro::TextMeshProUGUI* instruction_ = nullptr;
        // Column captions ("Quest" / "Video") and the left column's large
        // glance-value block: big FPS number with its label beneath it.
        TMPro::TextMeshProUGUI* leftHeader_ = nullptr;
        TMPro::TextMeshProUGUI* rightHeader_ = nullptr;
        TMPro::TextMeshProUGUI* fpsValue_ = nullptr;
        TMPro::TextMeshProUGUI* fpsLabel_ = nullptr;
        // One TMP element per statistics row, each height-locked by a
        // LayoutElement so the columns are laid out to the fixed body box.
        std::array<TMPro::TextMeshProUGUI*, 2> leftRows_{};
        std::array<TMPro::TextMeshProUGUI*, 7> rightRows_{};
        // TMPro/handle failures can occur every frame while Unity tears down a
        // scene. Record each category once per panel lifetime instead of
        // flooding persistent storage with identical diagnostics.
        bool rowFailureLogged_ = false;
        bool interactionFailureLogged_ = false;
    };
}
