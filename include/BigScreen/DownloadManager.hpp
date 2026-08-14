#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>

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
        bool fitToSong = false;
        bool blackDuringLeadIn = false;
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
        void QueueVideoThumbnail(
            std::string levelId,
            std::string sourceUrl,
            std::filesystem::path destination);
        void Cancel();
        DownloadSnapshot Snapshot();
        bool IsReady() const { return initialized_; }
        /// Short, stable diagnostic shown when an operation is requested after
        /// startup initialization failed. The detailed exception remains in
        /// Big Screen's persistent log under the same error code.
        std::string UnavailableMessage() const;
        std::optional<std::string> TakeUpdateNotice();

    private:
        DownloadManager() = default;
        ~DownloadManager();
        DownloadManager(const DownloadManager&) = delete;
        DownloadManager& operator=(const DownloadManager&) = delete;

        void Run(DownloadRequest request, std::filesystem::path finalPath);
        void RunProbe(std::string levelId, std::string sourceUrl);
        void RunUpdater(bool nightly, bool install);
        void RunThumbnailQueue();
        void RefreshSnapshotFromDiskLocked();
        void SetFailure(std::string message);

        struct ThumbnailRequest {
            std::string levelId;
            std::string sourceUrl;
            std::filesystem::path destination;
        };

        mutable std::mutex mutex_;
        // Serializes the check/join/recheck/start sequence. Unity currently
        // invokes starts on its main thread, but keeping that assumption out
        // of the manager prevents a future background caller from assigning
        // over a joinable std::thread and terminating the process.
        std::mutex startMutex_;
        std::thread worker_;
        std::thread thumbnailWorker_;
        std::mutex thumbnailMutex_;
        std::condition_variable thumbnailWake_;
        std::deque<ThumbnailRequest> thumbnailQueue_;
        std::unordered_set<std::string> requestedThumbnails_;
        bool stopThumbnailWorker_ = false;
        DownloadSnapshot snapshot_;
        std::filesystem::path jobPath_;
        std::filesystem::path statusPath_;
        std::filesystem::path cancelPath_;
        std::atomic<bool> initialized_{false};
        std::string initializationErrorCode_ = "BS-DL-INIT-000";
        std::string currentUpdateVersion_ = "2026.07.04";
        std::optional<std::string> updateNotice_;
        std::chrono::steady_clock::time_point lastStatusRefresh_{};
    };
}
