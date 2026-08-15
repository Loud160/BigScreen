#pragma once

#include <functional>
#include <string>

namespace BSML { class ModalView; }
namespace HMUI { class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    /// Center-screen asset readiness and launch page for the bundled showcase.
    /// Merely opening this page is read-only: every missing network asset has
    /// its own explicit download button.
    class ShowcaseMenu final {
    public:
        static ShowcaseMenu& Instance();
        void CreateUi(
            HMUI::ViewController* controller,
            std::function<void()> onClose);
        void ForgetUi();
        void Show();
        void Tick();
        /// Removes the confirmation blocker on every navigation boundary.
        /// This is intentionally callable even while the controller is being
        /// dismissed so a retained modal cannot capture input on re-entry.
        void DismissTransientUi() noexcept;

    private:
        ShowcaseMenu() = default;

        void DownloadMap();
        void RecheckMap();
        void DownloadVideo();
        void ConfirmPlay();
        void Play();
        void Refresh();
        void ReportActionFailure(const char* title, const std::string& detail);

        HMUI::ViewController* controller_ = nullptr;
        TMPro::TextMeshProUGUI* chromaStatus_ = nullptr;
        TMPro::TextMeshProUGUI* noodleStatus_ = nullptr;
        TMPro::TextMeshProUGUI* mapStatus_ = nullptr;
        TMPro::TextMeshProUGUI* videoStatus_ = nullptr;
        TMPro::TextMeshProUGUI* downloaderStatus_ = nullptr;
        TMPro::TextMeshProUGUI* activityStatus_ = nullptr;
        UnityEngine::UI::Button* recheckModsButton_ = nullptr;
        UnityEngine::UI::Button* recheckNoodleButton_ = nullptr;
        UnityEngine::UI::Button* mapButton_ = nullptr;
        UnityEngine::UI::Button* videoButton_ = nullptr;
        UnityEngine::UI::Button* playButton_ = nullptr;
        BSML::ModalView* warningModal_ = nullptr;
        std::function<void()> onClose_;
        int tickCounter_ = 0;
    };
}
