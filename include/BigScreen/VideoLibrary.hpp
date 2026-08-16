// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "BigScreen/MapVideoConfig.hpp"

namespace GlobalNamespace {
    class BeatmapLevel;
}

namespace BigScreen {
    enum class VideoOrigin {
        Mapper,
        User
    };

    struct StoredVideo {
        std::string sourceUrl;
        std::string fileName;
        std::string title;
        std::string codec;
        // Managed files are owned by Big Screen under Videos/. Map-local
        // files are user-owned assets referenced by a relative filename and
        // must never be deleted when their assignment is removed.
        bool mapLocal = false;
        // Import-folder files are also user-owned, but unlike map-local files
        // they can be assigned to OST, DLC, custom, or WIP maps.
        bool importFile = false;
        // A file-browser selection outside the map and Video Import folders is
        // referenced in place. Big Screen never deletes external files.
        bool externalFile = false;
        std::string externalPath;
        double offsetSeconds = 0.0;
        double playbackRate = 1.0;
        bool fitToSong = false;
        bool blackDuringLeadIn = false;
        double durationSeconds = 0.0;
        std::uint64_t bytes = 0;
        int width = 0;
        int height = 0;
    };

    struct StoredTiming {
        double offsetSeconds = 0.0;
        double playbackRate = 1.0;
        bool fitToSong = false;
        bool blackDuringLeadIn = false;
    };

    struct LevelVideoRecords {
        std::string songName;
        std::string songAuthor;
        std::optional<StoredVideo> mapper;
        std::optional<StoredVideo> user;
        // A mapper-declared local file normally becomes active automatically.
        // Keep a durable opt-out so Remove Video can unlink that assignment
        // without editing the map or deleting the mapper/user-owned MP4.
        bool mapperLocalSuppressed = false;
        // Mapper-local files live inside a map folder rather than the managed
        // video library, so their user-adjusted timing needs a small separate
        // manifest record of its own.
        std::optional<StoredTiming> mapperTiming;
        // Filename of the frame this map's owner picked in the thumbnail
        // picker, empty when none was ever picked. A map holds at most one
        // picked thumbnail; it survives unlinking (so relinking the video
        // restores the artwork) and is deleted only with the video file or
        // through Storage Maintenance after this reference is gone.
        std::string localThumbnail;
    };

    struct VideoDescriptor {
        std::string levelId;
        std::string songName;
        std::string songAuthor;
        double songDurationSeconds = 0.0;
        std::optional<MapVideoConfig> mapperDefinition;
        std::optional<MapVideoConfig> playableConfig;
        std::optional<std::string> downloadUrl;
        // This is the deterministic cache location for the active video's
        // own artwork. It is intentionally independent from Beat Saber's song
        // cover so the library browser never implies that a song has a video
        // merely because it has album art.
        std::optional<std::filesystem::path> thumbnailPath;
        VideoOrigin downloadOrigin = VideoOrigin::Mapper;
        bool hasMapperLocalFile = false;
        bool hasMapperDownload = false;
        bool hasUserOverride = false;
        bool userOverrideIsMapLocal = false;
        bool userOverrideIsImported = false;
        bool userOverrideIsExternal = false;
        std::optional<std::string> activeMapFileName;

        bool CanDownload() const { return downloadUrl.has_value(); }
        bool CanPlay() const { return playableConfig.has_value(); }
    };

    /// Result of probing one MP4/WebM found in a user-readable video folder.
    /// Invalid files remain in the list so the UI can explain why they cannot
    /// be selected instead of silently hiding them.
    struct LocalVideoFile {
        std::string fileName;
        std::filesystem::path path;
        bool compatible = false;
        std::string problem;
        std::string codec;
        double durationSeconds = 0.0;
        std::uint64_t bytes = 0;
        int width = 0;
        int height = 0;
    };

    /// Owns durable video files independently from mapper folders.
    ///
    /// User overrides and mapper-requested downloads are stored separately so
    /// removing an override reveals the mapper's original choice instead of
    /// destroying it. The manifest contains metadata only; MP4s remain normal
    /// visible files under Big Screen/Videos and are never evicted as cache.
    class VideoLibrary final {
    public:
        static VideoLibrary& Instance();

        void Initialize();
        VideoDescriptor Describe(GlobalNamespace::BeatmapLevel* level) const;
        std::optional<MapVideoConfig> ResolvePlayback(
            GlobalNamespace::BeatmapLevel* level) const;

        std::filesystem::path AllocateVideoPath(
            const std::string& levelId,
            VideoOrigin origin,
            std::string_view extension = ".mp4") const;
        std::filesystem::path AllocateThumbnailPath(
            const std::string& levelId,
            VideoOrigin origin) const;
        /// Deterministic location of the map's picker-chosen thumbnail PNG.
        /// One path per map is the mechanism behind the one-thumbnail rule:
        /// saving a new pick atomically replaces the old file in place.
        std::filesystem::path LocalThumbnailPath(
            const std::string& levelId) const;
        /// Records that LocalThumbnailPath now holds this map's picked
        /// artwork. The caller has already written the PNG bytes.
        bool CommitLocalThumbnail(const std::string& levelId);
        /// Deletes the picked thumbnail file and forgets it. Used when the
        /// user permanently deletes the local video the artwork came from.
        bool RemoveLocalThumbnail(const std::string& levelId);
        void CommitDownload(
            const std::string& levelId,
            const std::string& songName,
            const std::string& songAuthor,
            VideoOrigin origin,
            StoredVideo video);
        std::vector<LocalVideoFile> DiscoverLocalVideos(
            GlobalNamespace::BeatmapLevel* level) const;
        std::vector<LocalVideoFile> DiscoverImportedVideos() const;
        /// Validates one user-selected MP4/WebM without changing the library.
        LocalVideoFile InspectLocalVideo(
            const std::filesystem::path& path) const;
        /// Assigns any compatible MP4/WebM under Quest shared storage. Files in a
        /// map or Video Import folder retain their existing source identity;
        /// other files are referenced in place and remain user-owned.
        bool SetVideoFileOverride(
            GlobalNamespace::BeatmapLevel* level,
            const std::filesystem::path& path,
            std::string& error);
        bool SetLocalVideoOverride(
            GlobalNamespace::BeatmapLevel* level,
            const std::string& fileName,
            std::string& error);
        bool SetImportedVideoOverride(
            GlobalNamespace::BeatmapLevel* level,
            const std::string& fileName,
            std::string& error);
        bool RemoveUserOverride(const std::string& levelId, bool deleteFile);
        /// Prevents a mapper-declared local file from being selected for this
        /// map. This changes only library metadata and never removes the MP4.
        bool SuppressMapperLocalVideo(const std::string& levelId);
        /// Removes a mapper download record, optionally deleting its managed
        /// file. When deleteFile is false, the orphan remains available to the
        /// file browser and Storage Maintenance.
        bool RemoveMapperDownload(const std::string& levelId, bool deleteFile);
        bool DeleteMapperDownload(const std::string& levelId);
        /// Deletes an explicitly selected local file after enforcing the
        /// shared-storage and supported-container boundaries.
        bool DeleteLocalVideoFile(
            const std::filesystem::path& path,
            std::string& error) const;
        bool UpdateTiming(
            const std::string& levelId,
            VideoOrigin origin,
            double offsetSeconds,
            double playbackRate,
            bool fitToSong,
            bool blackDuringLeadIn);

        std::vector<std::pair<std::string, LevelVideoRecords>> Records() const;
        /// Returns only Big Screen-owned downloads associated with one map.
        /// Map-folder MP4s are deliberately excluded because the editor
        /// reports those separately as user-owned local storage.
        std::uint64_t ManagedBytesForLevel(const std::string& levelId) const;
        std::uint64_t LibraryBytes() const;
        std::uint64_t FreeBytes() const;
        /// Returns every thumbnail currently referenced either by persistent
        /// records or by a resolved mapper/local descriptor in this session.
        std::vector<std::string> ReferencedThumbnailFileNames() const;
        /// Returns and clears a one-time startup recovery message for the UI.
        std::optional<std::string> TakeRecoveryNotice();
        /// Rebuilds manifest entries for managed MP4s after all backups failed.
        /// Level metadata is supplied only after SongCore has completed loading.
        void RecoverManagedFiles(
            const std::vector<GlobalNamespace::BeatmapLevel*>& installedLevels);

        const std::filesystem::path& RootPath() const { return rootPath_; }
        const std::filesystem::path& VideoPath() const { return videoPath_; }
        const std::filesystem::path& RuntimePath() const { return runtimePath_; }
        const std::filesystem::path& ImportPath() const { return importPath_; }
        const std::filesystem::path& SharedStoragePath() const {
            return sharedStoragePath_;
        }
        const std::filesystem::path& ThumbnailPath() const { return thumbnailPath_; }

    private:
        VideoLibrary() = default;

        void LoadLocked();
        void SaveLocked();
        bool TryLoadManifestLocked(
            const std::filesystem::path& path,
            std::vector<std::pair<std::string, LevelVideoRecords>>& output) const;
        static std::string StableKey(const std::string& levelId);

        mutable std::mutex mutex_;
        std::filesystem::path rootPath_;
        std::filesystem::path videoPath_;
        std::filesystem::path thumbnailPath_;
        std::filesystem::path runtimePath_;
        std::filesystem::path importPath_;
        std::filesystem::path sharedStoragePath_;
        std::filesystem::path manifestPath_;
        std::vector<std::pair<std::string, LevelVideoRecords>> records_;
        // Last records proven durable in library.json. SaveLocked restores
        // this snapshot if serialization or replacement fails so UI state can
        // never get ahead of the manifest on disk.
        std::vector<std::pair<std::string, LevelVideoRecords>> persistedRecords_;
        // Describe() is used by several menu controls. Map metadata and saved
        // assignments do not change between those calls, so cache the fully
        // resolved result and invalidate it whenever this class commits a
        // manifest mutation. This removes repeated JSON parsing and file probes
        // from Unity's menu update path.
        mutable std::unordered_map<std::string, VideoDescriptor> descriptorCache_;
        mutable std::uint64_t cachedLibraryBytes_ = 0;
        mutable std::uint64_t cachedFreeBytes_ = 0;
        mutable std::chrono::steady_clock::time_point libraryBytesCacheTime_{};
        mutable std::chrono::steady_clock::time_point freeBytesCacheTime_{};
        bool recoveryScanNeeded_ = false;
        std::optional<std::string> recoveryNotice_;
    };
}
