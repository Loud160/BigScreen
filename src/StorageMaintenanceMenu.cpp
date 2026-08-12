#include "BigScreen/StorageMaintenanceMenu.hpp"

#include <sstream>

#include "BigScreen/StorageManager.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"

namespace BigScreen {
    namespace {
        std::string Bytes(std::uint64_t value)
        {
            constexpr double MiB = 1024.0 * 1024.0;
            constexpr double GiB = MiB * 1024.0;
            char buffer[64]{};
            if(value >= static_cast<std::uint64_t>(GiB))
                std::snprintf(buffer, sizeof(buffer), "%.2f GB", value / GiB);
            else
                std::snprintf(buffer, sizeof(buffer), "%.1f MB", value / MiB);
            return buffer;
        }
    }

    StorageMaintenanceMenu& StorageMaintenanceMenu::Instance()
    {
        static StorageMaintenanceMenu menu;
        return menu;
    }

    void StorageMaintenanceMenu::CreateUi(
        HMUI::ViewController* controller,
        std::function<void()> onBack)
    {
        controller_ = controller;
        auto* back = BSML::Lite::CreateUIButton(
            controller, "< Back to Video Library", {-15.0f, 42.0f}, {40.0f, 7.0f},
            [callback = std::move(onBack)]() { if(callback) callback(); });
        (void)back;
        auto* title = BSML::Lite::CreateText(
            controller, "Video Storage Maintenance", 4.5f,
            {0.0f, 34.0f}, {70.0f, 7.0f});
        title->set_alignment(TMPro::TextAlignmentOptions::Center);
        summary_ = BSML::Lite::CreateText(
            controller, "", 3.0f, {0.0f, 25.0f}, {70.0f, 12.0f});
        summary_->set_enableWordWrapping(true);
        summary_->set_alignment(TMPro::TextAlignmentOptions::Center);
        fileList_ = BSML::Lite::CreateText(
            controller, "", 2.6f, {0.0f, -2.0f}, {70.0f, 38.0f});
        fileList_->set_enableWordWrapping(false);
        fileList_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        fileList_->set_alignment(TMPro::TextAlignmentOptions::TopLeft);
        scanButton_ = BSML::Lite::CreateUIButton(
            controller, "Scan Storage", {21.0f, -38.0f}, {26.0f, 8.0f}, []() {
                std::string error;
                StorageManager::Instance().StartScan(error);
            });
        cleanButton_ = BSML::Lite::CreateUIButton(
            controller, "Clean Selected Files", {51.0f, -38.0f}, {30.0f, 8.0f},
            [this]() { if(confirmationModal_) confirmationModal_->Show(); });

        confirmationModal_ = BSML::Lite::CreateModal(
            controller, {68.0f, 34.0f}, nullptr, true);
        auto* prompt = BSML::Lite::CreateText(
            confirmationModal_,
            "<b>Remove the listed Big Screen files?</b>\n\n"
            "Only the files shown in the maintenance list will be removed. Map-folder videos, imported videos, assigned videos, and required runtime files are protected.",
            3.0f, {0.0f, 4.0f}, {60.0f, 20.0f});
        prompt->set_enableWordWrapping(true);
        prompt->set_alignment(TMPro::TextAlignmentOptions::Center);
        BSML::Lite::CreateUIButton(
            confirmationModal_->get_transform(), "Cancel", {20.0f, -27.0f}, {20.0f, 7.0f},
            [this]() { if(confirmationModal_) confirmationModal_->Hide(); });
        BSML::Lite::CreateUIButton(
            confirmationModal_->get_transform(), "Remove", {48.0f, -27.0f}, {20.0f, 7.0f},
            [this]() {
                if(confirmationModal_) confirmationModal_->Hide();
                std::string error;
                StorageManager::Instance().StartCleanup(error);
            });
        Refresh();
    }

    void StorageMaintenanceMenu::Show()
    {
        std::string error;
        StorageManager::Instance().StartScan(error);
        Refresh();
    }

    void StorageMaintenanceMenu::Tick()
    {
        if(++tickCounter_ < 20) return;
        tickCounter_ = 0;
        Refresh();
    }

    void StorageMaintenanceMenu::Refresh()
    {
        if(!summary_ || !fileList_) return;
        const auto snapshot = StorageManager::Instance().Snapshot();
        const auto fingerprint = snapshot.items.size() ^
            (static_cast<std::size_t>(snapshot.state) << 24) ^
            static_cast<std::size_t>(snapshot.removableBytes);
        if(fingerprint == lastFingerprint_ &&
           snapshot.state != StorageState::Scanning &&
           snapshot.state != StorageState::Cleaning)
            return;
        lastFingerprint_ = fingerprint;
        summary_->set_text(
            snapshot.message + "\nDownloads: " + Bytes(snapshot.downloadedBytes) +
            "   Imports: " + Bytes(snapshot.importedBytes) +
            "   Free: " + Bytes(snapshot.freeBytes) +
            "   Removable: " + Bytes(snapshot.removableBytes));
        std::ostringstream rows;
        for(const auto& item : snapshot.items)
            rows << item.category << "  |  " << Bytes(item.bytes)
                 << "  |  " << item.path.filename().string() << '\n';
        fileList_->set_text(rows.str().empty()
            ? "No files are currently listed for removal."
            : rows.str());
        const bool busy = snapshot.state == StorageState::Scanning ||
                          snapshot.state == StorageState::Cleaning;
        scanButton_->set_interactable(!busy);
        cleanButton_->set_interactable(
            snapshot.state == StorageState::Ready && !snapshot.items.empty());
    }
}
