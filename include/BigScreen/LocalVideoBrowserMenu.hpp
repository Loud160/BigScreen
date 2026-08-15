#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "BigScreen/VideoLibrary.hpp"

namespace BSML { class ModalView; }
namespace GlobalNamespace { class BeatmapLevel; }
namespace HMUI { class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace UnityEngine { class GameObject; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    /// Center-screen Quest file browser used to assign a local MP4 or WebM to any
    /// selected song. Directory enumeration and FFmpeg probing run off Unity's
    /// UI thread; only immutable scan snapshots are rendered by Tick().
    class LocalVideoBrowserMenu final {
    public:
        static LocalVideoBrowserMenu& Instance();
        ~LocalVideoBrowserMenu();

        void CreateUi(
            HMUI::ViewController* controller,
            std::function<void()> onCancel,
            std::function<void(const std::string&)> onAssigned);
        void ForgetUi();
        void Show(GlobalNamespace::BeatmapLevel* level);
        void Tick();

    private:
        enum class ScanState { Idle, Scanning, Ready, Failed };
        struct ScanSnapshot {
            ScanState state = ScanState::Idle;
            std::filesystem::path directory;
            std::vector<std::filesystem::path> directories;
            std::vector<LocalVideoFile> videos;
            std::string message;
            std::uint64_t version = 0;
        };

        LocalVideoBrowserMenu() = default;
        LocalVideoBrowserMenu(const LocalVideoBrowserMenu&) = delete;
        LocalVideoBrowserMenu& operator=(const LocalVideoBrowserMenu&) = delete;

        void StartScan(const std::filesystem::path& directory);
        void ScanWorker(std::filesystem::path directory, std::uint64_t request);
        ScanSnapshot Snapshot() const;
        void Refresh(const ScanSnapshot& snapshot);
        void RebuildBreadcrumbs(const std::filesystem::path& directory);
        void RebuildRows(const ScanSnapshot& snapshot);
        void SelectVideo(const std::filesystem::path& path);
        void SetSelectedVideo();
        void ShowHelp(const LocalVideoFile& file);
        void Cancel();

        HMUI::ViewController* controller_ = nullptr;
        GlobalNamespace::BeatmapLevel* selectedLevel_ = nullptr;
        std::function<void()> onCancel_;
        std::function<void(const std::string&)> onAssigned_;
        TMPro::TextMeshProUGUI* title_ = nullptr;
        TMPro::TextMeshProUGUI* statusText_ = nullptr;
        TMPro::TextMeshProUGUI* helpText_ = nullptr;
        UnityEngine::GameObject* breadcrumbContent_ = nullptr;
        UnityEngine::GameObject* listContent_ = nullptr;
        UnityEngine::UI::Button* upButton_ = nullptr;
        UnityEngine::UI::Button* setButton_ = nullptr;
        BSML::ModalView* helpModal_ = nullptr;
        std::vector<UnityEngine::GameObject*> breadcrumbObjects_;
        std::vector<UnityEngine::GameObject*> rowObjects_;
        std::filesystem::path selectedPath_;
        std::filesystem::path rootPath_;
        std::optional<std::filesystem::path> pendingDirectory_;

        mutable std::mutex mutex_;
        std::thread worker_;
        ScanSnapshot snapshot_;
        std::uint64_t nextRequest_ = 0;
        std::uint64_t renderedVersion_ = 0;
        int tickCounter_ = 0;
    };
}
