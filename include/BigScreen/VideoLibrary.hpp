#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

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
        double offsetSeconds = 0.0;
        double playbackRate = 1.0;
        double durationSeconds = 0.0;
        std::uint64_t bytes = 0;
        int width = 0;
        int height = 0;
    };

    struct StoredTiming {
        double offsetSeconds = 0.0;
        double playbackRate = 1.0;
    };

    struct LevelVideoRecords {
        std::string songName;
        std::string songAuthor;
        std::optional<StoredVideo> mapper;
        std::optional<StoredVideo> user;
        // Mapper-local files live inside a map folder rather than the managed
        // video library, so their user-adjusted timing needs a small separate
        // manifest record of its own.
        std::optional<StoredTiming> mapperTiming;
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

        bool CanDownload() const { return downloadUrl.has_value(); }
        bool CanPlay() const { return playableConfig.has_value(); }
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
            VideoOrigin origin) const;
        std::filesystem::path AllocateThumbnailPath(
            const std::string& levelId,
            VideoOrigin origin) const;
        void CommitDownload(
            const std::string& levelId,
            const std::string& songName,
            const std::string& songAuthor,
            VideoOrigin origin,
            StoredVideo video);
        bool RemoveUserOverride(const std::string& levelId, bool deleteFile);
        bool DeleteMapperDownload(const std::string& levelId);
        bool UpdateTiming(
            const std::string& levelId,
            VideoOrigin origin,
            double offsetSeconds,
            double playbackRate);

        std::vector<std::pair<std::string, LevelVideoRecords>> Records() const;
        std::uint64_t LibraryBytes() const;
        std::uint64_t FreeBytes() const;

        const std::filesystem::path& RootPath() const { return rootPath_; }
        const std::filesystem::path& VideoPath() const { return videoPath_; }
        const std::filesystem::path& RuntimePath() const { return runtimePath_; }

    private:
        VideoLibrary() = default;

        void LoadLocked();
        void SaveLocked() const;
        static std::string StableKey(const std::string& levelId);

        mutable std::mutex mutex_;
        std::filesystem::path rootPath_;
        std::filesystem::path videoPath_;
        std::filesystem::path thumbnailPath_;
        std::filesystem::path runtimePath_;
        std::filesystem::path manifestPath_;
        std::vector<std::pair<std::string, LevelVideoRecords>> records_;
    };
}
