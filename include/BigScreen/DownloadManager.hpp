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
        Preparing,
        Downloading,
        Completed,
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

        bool Active() const {
            return state == DownloadState::Preparing ||
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
        bool Start(DownloadRequest request, std::string& error);
        void Cancel();
        DownloadSnapshot Snapshot();
        bool IsReady() const { return initialized_; }

    private:
        DownloadManager() = default;
        ~DownloadManager();
        DownloadManager(const DownloadManager&) = delete;
        DownloadManager& operator=(const DownloadManager&) = delete;

        void Run(DownloadRequest request, std::filesystem::path finalPath);
        void RefreshSnapshotFromDiskLocked();
        void SetFailure(std::string message);

        mutable std::mutex mutex_;
        std::thread worker_;
        DownloadSnapshot snapshot_;
        std::filesystem::path jobPath_;
        std::filesystem::path statusPath_;
        std::filesystem::path cancelPath_;
        std::atomic<bool> initialized_{false};
    };
}
