#pragma once

#include <functional>

namespace BSML { class ModalView; }
namespace HMUI { class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    /// Right-panel review page for safe, user-confirmed storage maintenance.
    class StorageMaintenanceMenu final {
    public:
        static StorageMaintenanceMenu& Instance();
        void CreateUi(HMUI::ViewController* controller, std::function<void()> onBack);
        void Show();
        void Tick();

    private:
        StorageMaintenanceMenu() = default;
        void Refresh();

        HMUI::ViewController* controller_ = nullptr;
        TMPro::TextMeshProUGUI* summary_ = nullptr;
        TMPro::TextMeshProUGUI* fileList_ = nullptr;
        UnityEngine::UI::Button* scanButton_ = nullptr;
        UnityEngine::UI::Button* cleanButton_ = nullptr;
        BSML::ModalView* confirmationModal_ = nullptr;
        std::size_t lastFingerprint_ = 0;
        int tickCounter_ = 0;
    };
}
