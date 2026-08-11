#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "BigScreen/VideoLibrary.hpp"

namespace BigScreen {
    enum class DownloadState {
        Idle,
        Probing,
        ProbeCompleted,
        Preparing,
        Downloading,
        Completed,
        UpdateAvailable,
        UpToDate,
        Cancelled,
        Failed
    };

    struct DownloadRequest {
        std::string levelId;
        std::string songName;
        std::string songAuthor;
        std::string sourceUrl;
        VideoOrigin origin = VideoOrigin::Mapper;
        bool explicitContentAllowed = false;
        double offsetSeconds = 0.0;
        double playbackRate = 1.0;
    };

    struct DownloadSnapshot {
        DownloadState state = DownloadState::Idle;
        std::string levelId;
        std::string message;
        std::uint64_t downloadedBytes = 0;
        std::uint64_t totalBytes = 0;
        double speedBytesPerSecond = 0.0;
        double etaSeconds = 0.0;
        std::string title;
        std::string thumbnailPath;
        bool metadataOnly = false;

        bool Active() const {
            return state == DownloadState::Probing ||
                   state == DownloadState::Preparing ||
                   state == DownloadState::Downloading;
        }
    };

    /// Runs the official Android CPython build and yt-dlp on one background
    /// thread. Python communicates through atomic JSON status files, keeping
    /// every Unity/Beat Saber object confined to the main game thread.
    class DownloadManager final {
    public:
        static DownloadManager& Instance();

        bool Initialize(std::string& error);
        bool StartProbe(
            std::string levelId,
            std::string sourceUrl,
            std::string& error);
        bool Start(DownloadRequest request, std::string& error);
        bool StartUpdaterCheck(bool nightly, bool install, std::string& error);
        void StartScheduledUpdaterCheck(bool nightly);
        void Cancel();
        DownloadSnapshot Snapshot();
        bool IsReady() const { return initialized_; }
        const std::string& AvailableUpdateVersion() const { return availableUpdateVersion_; }

    private:
        DownloadManager() = default;
        ~DownloadManager();
        DownloadManager(const DownloadManager&) = delete;
        DownloadManager& operator=(const DownloadManager&) = delete;

        void Run(DownloadRequest request, std::filesystem::path finalPath);
        void RunProbe(std::string levelId, std::string sourceUrl);
        void RunUpdater(bool nightly, bool install);
        void RefreshSnapshotFromDiskLocked();
        void SetFailure(std::string message);

        mutable std::mutex mutex_;
        std::thread worker_;
        DownloadSnapshot snapshot_;
        std::filesystem::path jobPath_;
        std::filesystem::path statusPath_;
        std::filesystem::path cancelPath_;
        std::atomic<bool> initialized_{false};
        std::string availableUpdateVersion_;
        std::string currentUpdateVersion_ = "2026.07.04";
    };
}
