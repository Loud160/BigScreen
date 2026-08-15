// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace BSML { class ModalView; class ToggleSetting; }
namespace HMUI { class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace UnityEngine { class GameObject; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    struct StorageSnapshot;

    /// Center-panel review page for safe, user-confirmed storage maintenance.
    class StorageMaintenanceMenu final {
    public:
        static StorageMaintenanceMenu& Instance();
        void CreateUi(HMUI::ViewController* controller, std::function<void()> onBack);
        void ForgetUi();
        void Show();
        void Tick();

    private:
        StorageMaintenanceMenu() = default;
        void BeginScan();
        void BeginCleanup();
        void RebuildFileRows(const StorageSnapshot& snapshot);
        void RefreshSelectionState(const StorageSnapshot& snapshot);
        void Refresh();

        HMUI::ViewController* controller_ = nullptr;
        TMPro::TextMeshProUGUI* summary_ = nullptr;
        TMPro::TextMeshProUGUI* confirmationText_ = nullptr;
        UnityEngine::GameObject* fileListContent_ = nullptr;
        UnityEngine::UI::Button* scanButton_ = nullptr;
        UnityEngine::UI::Button* cleanButton_ = nullptr;
        BSML::ModalView* confirmationModal_ = nullptr;
        std::vector<UnityEngine::GameObject*> fileRows_;
        std::unordered_set<std::string> selectedPaths_;
        std::size_t lastFingerprint_ = 0;
        bool selectAllOnNextResult_ = true;
        int tickCounter_ = 0;
    };
}
