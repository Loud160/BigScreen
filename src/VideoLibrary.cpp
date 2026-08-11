#include "BigScreen/VideoLibrary.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "GlobalNamespace/BeatmapLevel.hpp"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr auto LibraryRoot =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen";

        const rapidjson::Value* Member(const rapidjson::Value& value, const char* name)
        {
            if(!value.IsObject())
                return nullptr;
            const auto found = value.FindMember(name);
            return found == value.MemberEnd() ? nullptr : &found->value;
        }

        std::string StringOr(const rapidjson::Value& value, const char* name)
        {
            const auto* member = Member(value, name);
            return member && member->IsString()
                ? std::string(member->GetString(), member->GetStringLength())
                : std::string{};
        }

        double NumberOr(const rapidjson::Value& value, const char* name, double fallback)
        {
            const auto* member = Member(value, name);
            return member && member->IsNumber() ? member->GetDouble() : fallback;
        }

        bool BoolOr(const rapidjson::Value& value, const char* name, bool fallback)
        {
            const auto* member = Member(value, name);
            return member && member->IsBool() ? member->GetBool() : fallback;
        }

        StoredVideo ParseStoredVideo(const rapidjson::Value& value)
        {
            StoredVideo video;
            video.sourceUrl = StringOr(value, "sourceUrl");
            video.fileName = StringOr(value, "fileName");
            video.title = StringOr(value, "title");
            video.codec = StringOr(value, "codec");
            video.offsetSeconds = NumberOr(value, "offsetSeconds", 0.0);
            video.playbackRate = std::clamp(
                NumberOr(value, "playbackRate", 1.0),
                0.05,
                8.0);
            video.fitToSong = BoolOr(value, "fitToSong", false);
            video.blackDuringLeadIn = BoolOr(value, "blackDuringLeadIn", false);
            video.durationSeconds = std::max(
                0.0,
                NumberOr(value, "durationSeconds", 0.0));
            video.bytes = static_cast<std::uint64_t>(std::max(
                0.0,
                NumberOr(value, "bytes", 0.0)));
            video.width = static_cast<int>(NumberOr(value, "width", 0.0));
            video.height = static_cast<int>(NumberOr(value, "height", 0.0));
            return video;
        }

        StoredTiming ParseStoredTiming(const rapidjson::Value& value)
        {
            StoredTiming timing;
            timing.offsetSeconds = std::clamp(
                NumberOr(value, "offsetSeconds", 0.0), -60.0, 60.0);
            timing.playbackRate = std::clamp(
                NumberOr(value, "playbackRate", 1.0), 0.05, 8.0);
            timing.fitToSong = BoolOr(value, "fitToSong", false);
            timing.blackDuringLeadIn = BoolOr(value, "blackDuringLeadIn", false);
            return timing;
        }

        void WriteStoredVideo(
            rapidjson::Value& object,
            const StoredVideo& video,
            rapidjson::Document::AllocatorType& allocator)
        {
            auto addString = [&](const char* name, const std::string& value)
            {
                object.AddMember(
                    rapidjson::Value(name, allocator).Move(),
                    rapidjson::Value(value.c_str(),
                                     static_cast<rapidjson::SizeType>(value.size()),
                                     allocator).Move(),
                    allocator);
            };
            addString("sourceUrl", video.sourceUrl);
            addString("fileName", video.fileName);
            addString("title", video.title);
            addString("codec", video.codec);
            object.AddMember("offsetSeconds", video.offsetSeconds, allocator);
            object.AddMember("playbackRate", video.playbackRate, allocator);
            object.AddMember("fitToSong", video.fitToSong, allocator);
            object.AddMember("blackDuringLeadIn", video.blackDuringLeadIn, allocator);
            object.AddMember("durationSeconds", video.durationSeconds, allocator);
            object.AddMember("bytes", static_cast<std::uint64_t>(video.bytes), allocator);
            object.AddMember("width", video.width, allocator);
            object.AddMember("height", video.height, allocator);
        }

        auto FindRecord(
            std::vector<std::pair<std::string, LevelVideoRecords>>& records,
            const std::string& levelId)
        {
            return std::find_if(records.begin(), records.end(), [&](const auto& item)
            {
                return item.first == levelId;
            });
        }

        auto FindRecord(
            const std::vector<std::pair<std::string, LevelVideoRecords>>& records,
            const std::string& levelId)
        {
            return std::find_if(records.begin(), records.end(), [&](const auto& item)
            {
                return item.first == levelId;
            });
        }

        bool StoredFileExists(
            const std::filesystem::path& videoDirectory,
            const std::optional<StoredVideo>& video)
        {
            return video &&
                   !video->fileName.empty() &&
                   std::filesystem::is_regular_file(videoDirectory / video->fileName);
        }
    }

    VideoLibrary& VideoLibrary::Instance()
    {
        static VideoLibrary library;
        return library;
    }

    void VideoLibrary::Initialize()
    {
        std::scoped_lock lock(mutex_);
        rootPath_ = LibraryRoot;
        videoPath_ = rootPath_ / "Videos";
        thumbnailPath_ = rootPath_ / "Thumbnails";
        runtimePath_ = rootPath_ / "Runtime";
        manifestPath_ = rootPath_ / "library.json";
        std::filesystem::create_directories(videoPath_);
        std::filesystem::create_directories(thumbnailPath_);
        std::filesystem::create_directories(runtimePath_);
        LoadLocked();
        PaperLogger.info(
            "Video library ready at '{}' with {} saved level entries",
            rootPath_.string(),
            records_.size());
    }

    VideoDescriptor VideoLibrary::Describe(GlobalNamespace::BeatmapLevel* level) const
    {
        VideoDescriptor descriptor;
        if(!level || !level->levelID)
            return descriptor;

        descriptor.levelId = std::string(level->levelID);
        descriptor.songName = level->songName ? std::string(level->songName) : "Unknown Song";
        descriptor.songAuthor = level->songAuthorName
            ? std::string(level->songAuthorName)
            : std::string{};
        descriptor.songDurationSeconds = level->songDuration;

        if(auto* custom = SongCore::API::Loading::GetLevelByLevelID(descriptor.levelId))
        {
            std::string error;
            descriptor.mapperDefinition = MapVideoConfig::LoadDefinitionFromLevel(
                std::filesystem::path(custom->get_customLevelPath()),
                error);
            if(!error.empty())
                PaperLogger.error("Video metadata rejected for '{}': {}", descriptor.levelId, error);
            if(descriptor.mapperDefinition)
            {
                descriptor.hasMapperLocalFile = descriptor.mapperDefinition->HasLocalVideo();
                descriptor.downloadUrl = descriptor.mapperDefinition->DownloadUrl();
                descriptor.downloadOrigin = VideoOrigin::Mapper;
            }
        }

        std::scoped_lock lock(mutex_);
        const auto found = FindRecord(records_, descriptor.levelId);
        const LevelVideoRecords* saved = found == records_.end() ? nullptr : &found->second;
        descriptor.hasUserOverride = saved && StoredFileExists(videoPath_, saved->user);
        descriptor.hasMapperDownload = saved && StoredFileExists(videoPath_, saved->mapper);

        MapVideoConfig effective = descriptor.mapperDefinition.value_or(MapVideoConfig{});
        if(descriptor.hasUserOverride)
        {
            const auto& record = *saved->user;
            effective.videoPath = videoPath_ / record.fileName;
            effective.offsetSeconds = record.offsetSeconds;
            effective.playbackRate = record.playbackRate;
            effective.fitToSong = record.fitToSong;
            effective.blackDuringLeadIn = record.blackDuringLeadIn;
            effective.declaredDurationSeconds = record.durationSeconds;
            effective.title = record.title.empty()
                ? std::optional<std::string>{}
                : std::optional<std::string>{record.title};
            descriptor.downloadUrl = record.sourceUrl;
            descriptor.downloadOrigin = VideoOrigin::User;
            descriptor.thumbnailPath = AllocateThumbnailPath(
                descriptor.levelId, VideoOrigin::User);
            descriptor.playableConfig = effective;
        }
        else if(descriptor.hasMapperLocalFile)
        {
            if(saved && saved->mapperTiming)
            {
                effective.offsetSeconds = saved->mapperTiming->offsetSeconds;
                effective.playbackRate = saved->mapperTiming->playbackRate;
                effective.fitToSong = saved->mapperTiming->fitToSong;
                effective.blackDuringLeadIn = saved->mapperTiming->blackDuringLeadIn;
            }
            descriptor.thumbnailPath = AllocateThumbnailPath(
                descriptor.levelId, VideoOrigin::Mapper);
            descriptor.playableConfig = effective;
        }
        else if(descriptor.hasMapperDownload)
        {
            const auto& record = *saved->mapper;
            effective.videoPath = videoPath_ / record.fileName;
            effective.offsetSeconds = record.offsetSeconds;
            effective.playbackRate = record.playbackRate;
            effective.fitToSong = record.fitToSong;
            effective.blackDuringLeadIn = record.blackDuringLeadIn;
            effective.declaredDurationSeconds = record.durationSeconds;
            descriptor.thumbnailPath = AllocateThumbnailPath(
                descriptor.levelId, VideoOrigin::Mapper);
            descriptor.playableConfig = effective;
        }
        else if(descriptor.hasMapperDownload || descriptor.hasMapperLocalFile ||
                descriptor.downloadUrl)
        {
            descriptor.thumbnailPath = AllocateThumbnailPath(
                descriptor.levelId, VideoOrigin::Mapper);
        }

        return descriptor;
    }

    std::optional<MapVideoConfig> VideoLibrary::ResolvePlayback(
        GlobalNamespace::BeatmapLevel* level) const
    {
        return Describe(level).playableConfig;
    }

    std::filesystem::path VideoLibrary::AllocateVideoPath(
        const std::string& levelId,
        VideoOrigin origin) const
    {
        std::scoped_lock lock(mutex_);
        const auto suffix = origin == VideoOrigin::User ? "-user.mp4" : "-mapper.mp4";
        return videoPath_ / (StableKey(levelId) + suffix);
    }

    std::filesystem::path VideoLibrary::AllocateThumbnailPath(
        const std::string& levelId,
        VideoOrigin origin) const
    {
        // Library paths are assigned once during startup and remain immutable.
        // This helper is also used while Describe already holds mutex_, so it
        // must not attempt to acquire that non-recursive lock again.
        const auto suffix = origin == VideoOrigin::User
            ? "-user.jpg"
            : "-mapper.jpg";
        return thumbnailPath_ / (StableKey(levelId) + suffix);
    }

    void VideoLibrary::CommitDownload(
        const std::string& levelId,
        const std::string& songName,
        const std::string& songAuthor,
        VideoOrigin origin,
        StoredVideo video)
    {
        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end())
        {
            records_.emplace_back(levelId, LevelVideoRecords{});
            found = std::prev(records_.end());
        }
        found->second.songName = songName;
        found->second.songAuthor = songAuthor;
        auto& target = origin == VideoOrigin::User
            ? found->second.user
            : found->second.mapper;

        // A replacement is committed only after the new file exists. Delete
        // the superseded owned file afterwards so failed downloads can never
        // erase the user's last working video.
        const auto previous = target;
        target = std::move(video);
        SaveLocked();
        if(previous && previous->fileName != target->fileName)
            std::filesystem::remove(videoPath_ / previous->fileName);
    }

    bool VideoLibrary::RemoveUserOverride(const std::string& levelId, bool deleteFile)
    {
        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end() || !found->second.user)
            return false;
        if(deleteFile)
            std::filesystem::remove(videoPath_ / found->second.user->fileName);
        std::filesystem::remove(
            thumbnailPath_ / (StableKey(levelId) + "-user.jpg"));
        found->second.user.reset();
        SaveLocked();
        return true;
    }

    bool VideoLibrary::DeleteMapperDownload(const std::string& levelId)
    {
        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end() || !found->second.mapper)
            return false;
        std::filesystem::remove(videoPath_ / found->second.mapper->fileName);
        std::filesystem::remove(
            thumbnailPath_ / (StableKey(levelId) + "-mapper.jpg"));
        found->second.mapper.reset();
        SaveLocked();
        return true;
    }

    bool VideoLibrary::UpdateTiming(
        const std::string& levelId,
        VideoOrigin origin,
        double offsetSeconds,
        double playbackRate,
        bool fitToSong,
        bool blackDuringLeadIn)
    {
        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end())
        {
            records_.emplace_back(levelId, LevelVideoRecords{});
            found = std::prev(records_.end());
        }
        auto& target = origin == VideoOrigin::User
            ? found->second.user
            : found->second.mapper;
        const auto clampedOffset = std::clamp(offsetSeconds, -60.0, 60.0);
        const auto clampedRate = std::clamp(playbackRate, 0.05, 8.0);
        if(origin == VideoOrigin::Mapper)
        {
            found->second.mapperTiming = StoredTiming{
                clampedOffset,
                clampedRate,
                fitToSong,
                blackDuringLeadIn};
            if(target)
            {
                target->offsetSeconds = clampedOffset;
                target->playbackRate = clampedRate;
                target->fitToSong = fitToSong;
                target->blackDuringLeadIn = blackDuringLeadIn;
            }
        }
        else if(target)
        {
            target->offsetSeconds = clampedOffset;
            target->playbackRate = clampedRate;
            target->fitToSong = fitToSong;
            target->blackDuringLeadIn = blackDuringLeadIn;
        }
        else
        {
            return false;
        }
        SaveLocked();
        PaperLogger.info(
            "Saved {} video timing for '{}': offset {:.2f}s, speed {:.4f}x, fit {}, lead-in {}",
            origin == VideoOrigin::User ? "user" : "mapper",
            levelId,
            clampedOffset,
            clampedRate,
            fitToSong ? "on" : "off",
            blackDuringLeadIn ? "black" : "transparent");
        return true;
    }

    std::vector<std::pair<std::string, LevelVideoRecords>> VideoLibrary::Records() const
    {
        std::scoped_lock lock(mutex_);
        return records_;
    }

    std::uint64_t VideoLibrary::LibraryBytes() const
    {
        std::scoped_lock lock(mutex_);
        std::uint64_t total = 0;
        if(!std::filesystem::exists(videoPath_))
            return total;
        for(const auto& entry : std::filesystem::directory_iterator(videoPath_))
        {
            if(entry.is_regular_file())
                total += entry.file_size();
        }
        return total;
    }

    std::uint64_t VideoLibrary::FreeBytes() const
    {
        std::scoped_lock lock(mutex_);
        std::error_code error;
        const auto info = std::filesystem::space(rootPath_, error);
        return error ? 0 : info.available;
    }

    void VideoLibrary::LoadLocked()
    {
        records_.clear();
        std::ifstream stream(manifestPath_, std::ios::binary);
        if(!stream)
            return;
        const std::string json{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        rapidjson::Document document;
        document.Parse(json.data(), json.size());
        const auto* levels = Member(document, "levels");
        if(document.HasParseError() || !levels || !levels->IsObject())
        {
            PaperLogger.error("Ignoring invalid video library manifest '{}'", manifestPath_.string());
            return;
        }

        for(auto member = levels->MemberBegin(); member != levels->MemberEnd(); ++member)
        {
            if(!member->name.IsString() || !member->value.IsObject())
                continue;
            LevelVideoRecords level;
            level.songName = StringOr(member->value, "songName");
            level.songAuthor = StringOr(member->value, "songAuthor");
            if(const auto* mapper = Member(member->value, "mapper"); mapper && mapper->IsObject())
                level.mapper = ParseStoredVideo(*mapper);
            if(const auto* user = Member(member->value, "user"); user && user->IsObject())
                level.user = ParseStoredVideo(*user);
            if(const auto* timing = Member(member->value, "mapperTiming");
               timing && timing->IsObject())
                level.mapperTiming = ParseStoredTiming(*timing);
            records_.emplace_back(
                std::string(member->name.GetString(), member->name.GetStringLength()),
                std::move(level));
        }
    }

    void VideoLibrary::SaveLocked() const
    {
        rapidjson::Document document(rapidjson::kObjectType);
        auto& allocator = document.GetAllocator();
        document.AddMember("version", 1, allocator);
        rapidjson::Value levels(rapidjson::kObjectType);
        for(const auto& [levelId, record] : records_)
        {
            rapidjson::Value level(rapidjson::kObjectType);
            level.AddMember(
                "songName",
                rapidjson::Value(record.songName.c_str(), allocator).Move(),
                allocator);
            level.AddMember(
                "songAuthor",
                rapidjson::Value(record.songAuthor.c_str(), allocator).Move(),
                allocator);
            if(record.mapper)
            {
                rapidjson::Value mapper(rapidjson::kObjectType);
                WriteStoredVideo(mapper, *record.mapper, allocator);
                level.AddMember("mapper", mapper.Move(), allocator);
            }
            if(record.user)
            {
                rapidjson::Value user(rapidjson::kObjectType);
                WriteStoredVideo(user, *record.user, allocator);
                level.AddMember("user", user.Move(), allocator);
            }
            if(record.mapperTiming)
            {
                rapidjson::Value timing(rapidjson::kObjectType);
                timing.AddMember(
                    "offsetSeconds", record.mapperTiming->offsetSeconds, allocator);
                timing.AddMember(
                    "playbackRate", record.mapperTiming->playbackRate, allocator);
                timing.AddMember(
                    "fitToSong", record.mapperTiming->fitToSong, allocator);
                timing.AddMember(
                    "blackDuringLeadIn", record.mapperTiming->blackDuringLeadIn, allocator);
                level.AddMember("mapperTiming", timing.Move(), allocator);
            }
            levels.AddMember(
                rapidjson::Value(levelId.c_str(),
                                 static_cast<rapidjson::SizeType>(levelId.size()),
                                 allocator).Move(),
                level.Move(),
                allocator);
        }
        document.AddMember("levels", levels.Move(), allocator);

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        const auto temporary = manifestPath_.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
            stream.flush();
            if(!stream)
                throw std::runtime_error("Could not write video library manifest");
        }
        std::filesystem::rename(temporary, manifestPath_);
    }

    std::string VideoLibrary::StableKey(const std::string& levelId)
    {
        // FNV-1a is used only to form a short filesystem-safe key; the complete
        // level ID remains in the manifest and is the authority for lookup.
        std::uint64_t hash = 14695981039346656037ull;
        for(const unsigned char value : levelId)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }
        std::ostringstream text;
        text << std::hex << std::setw(16) << std::setfill('0') << hash;
        return text.str();
    }
}
