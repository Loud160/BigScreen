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
        int sourceWidth = 0;
        int sourceHeight = 0;
        double sourceFps = 0.0;
        int outputWidth = 0;
        int outputHeight = 0;
        int outputFpsLimit = 0;
        std::uint64_t skippedVideoFrames = 0;
        std::uint64_t expectedVideoFrames = 0;
        std::uint64_t totalMissedVideoFrames = 0;
        double averageVideoFramesPerSecond = 0.0;
        double missedVideoFramePercent = 0.0;
        double averageDecodeMilliseconds = 0.0;
        double peakDecodeMilliseconds = 0.0;
    };

    /// Owns the movable performance-information panel shown only while Big
    /// Screen's menu is open. Its transform is intentionally never persisted:
    /// every enable or menu activation recreates it at a known safe position.
    class PerformancePanel final {
    public:
        static PerformancePanel& Instance();

        /// Marks Big Screen's menu active and recreates the panel at its
        /// documented starting position when diagnostics are enabled.
        void ActivateMenu() noexcept;
        /// Recreates the same panel for a video map. The BSML Top handle stays
        /// active so Beat Saber's pointer can move it, including from pause UI.
        void ActivateGameplay() noexcept;
        /// Applies the live toggle. Re-enabling always resets panel placement.
        void SetEnabled(bool enabled) noexcept;
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

        bool CreateAtDefaultPlacement();
        void ActivateForContext(bool gameplay) noexcept;
        void Destroy() noexcept;

        enum class Context { None, Menu, Gameplay };
        Context context_ = Context::None;
        BSML::FloatingScreen* screen_ = nullptr;
        HMUI::ImageView* background_ = nullptr;
        HMUI::ImageView* headsetCard_ = nullptr;
        HMUI::ImageView* videoCard_ = nullptr;
        std::array<HMUI::ImageView*, 4> borders_{};
        TMPro::TextMeshProUGUI* title_ = nullptr;
        TMPro::TextMeshProUGUI* headsetStatistics_ = nullptr;
        TMPro::TextMeshProUGUI* videoStatistics_ = nullptr;
    };
}
