// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "BigScreen/VideoLibrary.hpp"

namespace BigScreen {
    // This pinned nightly contains yt-dlp's upstream fix for the August 2026
    // Android-VR HTTP 403 regression. Return to a stable baseline after the
    // next stable yt-dlp release incorporates that fix.
    inline constexpr std::string_view BundledYtDlpVersion = "2026.08.18.122307";
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
        // Exact source tier selected by the player. Big Screen retains and
        // presents this file at its native resolution.
        int requestedHeight = 1080;
        // When YouTube exposes multiple frame rates at one tier, retain the
        // best source no faster than the user's saved presentation ceiling.
        int maximumSourceFps = 30;
    };

    /// Describes one exact BeatSaver revision that Big Screen is allowed to
    /// install into its own managed demo-level directory. The expected hash is
    /// used to select the immutable revision from BeatSaver's map metadata;
    /// SongCore performs the final content identity check after extraction.
    struct MapPackageRequest {
        std::string mapKey;
        std::string expectedHash;
        std::filesystem::path destinationDirectory;
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
        std::string errorCode;
        std::string diagnostic;
        bool metadataOnly = false;
        std::vector<int> availableHeights;
        // Retained in memory across status-file refreshes so Retry/Resume and
        // progress UI continue referring to the exact tier the user selected.
        int requestedHeight = 0;

        bool Active() const {
            return state == DownloadState::Probing ||
                   state == DownloadState::Preparing ||
                   state == DownloadState::Downloading;
        }
    };

    enum class ModReleaseCheckState {
        NotChecked,
        Checking,
        UpToDate,
        UpdateAvailable,
        Unavailable
    };

    struct ModReleaseSnapshot {
        ModReleaseCheckState state = ModReleaseCheckState::NotChecked;
        std::string currentVersion;
        std::string latestVersion;
        std::string message;

        bool Active() const { return state == ModReleaseCheckState::Checking; }
    };

    struct ModReleaseNotice {
        std::string title;
        std::string message;
    };

    /// Runs the official Android CPython build and yt-dlp in-process on one
    /// background thread. Quest's noexec shared storage and Android API 29 W^X
    /// restrictions make an extracted executable interpreter unreliable; the
    /// embedded library preserves standalone operation without shelling out.
    /// Python communicates through atomic JSON status files, keeping every
    /// Unity/Beat Saber object confined to the main game thread.
    class DownloadManager final {
    public:
        static DownloadManager& Instance();

        bool Initialize(std::string& error);
        bool StartProbe(
            std::string levelId,
            std::string sourceUrl,
            std::string& error);
        bool Start(DownloadRequest request, std::string& error);
        bool StartMapPackage(MapPackageRequest request, std::string& error);
        bool StartUpdaterCheck(bool nightly, bool install, std::string& error);
        void StartScheduledUpdaterCheck(bool nightly);
        /// Starts the public GitHub release check at most once per Beat Saber
        /// process. Reopening Big Screen does not generate another request.
        void StartAutomaticModReleaseCheck();
        /// Manual checks bypass the once-per-session automatic guard but never
        /// overlap another release check.
        bool StartModReleaseCheck(std::string& error);
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
        std::string CurrentYtDlpVersion() const;
        ModReleaseSnapshot ModReleaseStatus() const;
        std::optional<std::string> TakeUpdateNotice();
        std::optional<ModReleaseNotice> TakeModReleaseNotice();

    private:
        DownloadManager() = default;
        ~DownloadManager();
        DownloadManager(const DownloadManager&) = delete;
        DownloadManager& operator=(const DownloadManager&) = delete;

        void Run(DownloadRequest request, std::filesystem::path finalPath);
        void RunMapPackage(MapPackageRequest request);
        void RunProbe(std::string levelId, std::string sourceUrl);
        void RunUpdater(bool nightly, bool install);
        void RunModReleaseCheck(bool automatic);
        void RunThumbnailQueue();
        void RunOperationQueue();
        void RunStatusPolling();
        bool QueueOperation(std::function<void()> operation, std::string& error);
        bool StartModReleaseCheck(bool automatic, std::string& error);
        void SetCurrentYtDlpVersion(std::string version);
        void RefreshSnapshotFromDisk();
        void SetFailure(std::string message);

        struct ThumbnailRequest {
            std::string levelId;
            std::string sourceUrl;
            std::filesystem::path destination;
        };

        mutable std::mutex mutex_;
        // Serializes validation and publication of one downloader operation.
        // The operation thread is persistent: UI callbacks enqueue work and
        // never join a prior Python/network worker on Unity's main thread.
        std::mutex startMutex_;
        // Long-lived operation ownership replaces one std::thread per action.
        // operationBusy_ covers both queued and executing work, so clearing it
        // is the final publication step after promotion and persistence.
        std::thread worker_;
        // Reads Python's atomic JSON status mailbox away from Unity's thread.
        std::thread statusWorker_;
        std::mutex operationMutex_;
        std::condition_variable operationWake_;
        std::function<void()> pendingOperation_;
        std::atomic<bool> operationBusy_{false};
        bool stopOperationWorker_ = false;
        std::mutex statusWaitMutex_;
        // Serializes the polling read with the operation worker's mandatory
        // terminal refresh without holding the public snapshot mutex for I/O.
        std::mutex statusReadMutex_;
        std::condition_variable statusWake_;
        bool stopStatusWorker_ = false;
        std::thread modReleaseWorker_;
        // A completed release-check handle may be detached by the next UI
        // request; an active one is never joined by that request.
        std::atomic<bool> modReleaseWorkerFinished_{true};
        std::mutex modReleaseStartMutex_;
        mutable std::mutex modReleaseMutex_;
        ModReleaseSnapshot modReleaseSnapshot_;
        std::optional<ModReleaseNotice> modReleaseNotice_;
        std::atomic<bool> automaticModReleaseCheckStarted_{false};
        std::thread thumbnailWorker_;
        std::mutex thumbnailMutex_;
        std::condition_variable thumbnailWake_;
        std::deque<ThumbnailRequest> thumbnailQueue_;
        std::unordered_set<std::string> requestedThumbnails_;
        bool stopThumbnailWorker_ = false;
        DownloadSnapshot snapshot_;
        std::filesystem::path statusPath_;
        std::filesystem::path cancelPath_;
        std::atomic<bool> initialized_{false};
        std::string initializationErrorCode_ = "BS-DL-INIT-000";
        mutable std::mutex versionMutex_;
        std::string currentUpdateVersion_ = std::string(BundledYtDlpVersion);
        std::optional<std::string> updateNotice_;
        // Set when C++ itself publishes a terminal failure. A filesystem
        // permission/handle error can prevent deletion or truncation of the
        // worker's older JSON; ignoring it until the next explicitly started
        // operation prevents that stale file from reviving an active state.
        bool ignoreStatusFile_ = false;
    };
}
