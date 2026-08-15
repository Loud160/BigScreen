// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/VideoLibrary.hpp"
#include "BigScreen/Utility.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/ErrorManager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <chrono>

#include "main.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/pixdesc.h"
}

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
            const auto sourceType = StringOr(value, "sourceType");
            video.mapLocal = sourceType == "mapFile";
            video.importFile = sourceType == "importFile";
            video.externalFile = sourceType == "externalFile";
            video.externalPath = StringOr(value, "externalPath");
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
            addString("sourceType", video.mapLocal
                ? "mapFile"
                : (video.importFile
                    ? "importFile"
                    : (video.externalFile ? "externalFile" : "managedFile")));
            if(video.externalFile)
                addString("externalPath", video.externalPath);
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

        bool IsUserOwnedFile(const StoredVideo& video)
        {
            return video.mapLocal || video.importFile || video.externalFile;
        }

        bool RemoveManagedFile(
            const std::filesystem::path& videoDirectory,
            const std::string& fileName)
        {
            // library.json is user-accessible shared storage. Accept only one
            // filename component before a record is allowed to remove a file;
            // reads enforce the identical boundary in ResolveStoredFile.
            const std::filesystem::path relative(fileName);
            if(relative.empty() || relative.is_absolute() ||
               relative.filename() != relative)
            {
                PaperLogger.error(
                    "Refused to remove unsafe managed video path '{}'",
                    fileName);
                ErrorManager::Instance().RecordError(
                    "Removing a managed video",
                    "The library contained an unsafe filename: " + fileName);
                return false;
            }
            std::error_code error;
            const bool removed = std::filesystem::remove(
                videoDirectory / relative, error);
            if(error)
                PaperLogger.warn(
                    "Could not remove managed video '{}': {}",
                    fileName,
                    error.message());
            return removed && !error;
        }

        std::optional<std::filesystem::path> ResolveStoredFile(
            const std::filesystem::path& videoDirectory,
            const std::filesystem::path& importDirectory,
            const std::filesystem::path& levelDirectory,
            const std::optional<StoredVideo>& video)
        {
            if(!video || video->fileName.empty())
                return std::nullopt;

            if(video->externalFile)
            {
                const auto root = std::filesystem::path("/sdcard");
                const auto path = std::filesystem::path(video->externalPath)
                    .lexically_normal();
                if(!path.is_absolute() || !Utility::IsPathInside(path, root) ||
                   !Utility::IsRegularFile(path))
                    return std::nullopt;
                return path;
            }

            const std::filesystem::path relative(video->fileName);
            if(relative.is_absolute() || relative.filename() != relative)
                return std::nullopt;

            const auto parent = video->mapLocal
                ? levelDirectory
                : (video->importFile ? importDirectory : videoDirectory);
            if(parent.empty())
                return std::nullopt;
            const auto resolved = (parent / relative).lexically_normal();
            if(!Utility::IsPathInside(resolved, parent) ||
               !Utility::IsRegularFile(resolved))
                return std::nullopt;
            return resolved;
        }

        std::string FfmpegError(int code)
        {
            char buffer[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(code, buffer, sizeof(buffer));
            return buffer;
        }

        bool IsHdrTransfer(AVColorTransferCharacteristic transfer)
        {
            return transfer == AVCOL_TRC_SMPTE2084 ||
                   transfer == AVCOL_TRC_ARIB_STD_B67;
        }

        std::string UnsupportedPixelReason(const AVCodecParameters* parameters)
        {
            if(!parameters)
                return "This file does not contain readable video parameters.";
            if(IsHdrTransfer(parameters->color_trc))
                return "HDR video is not supported. Re-export it as 8-bit SDR.";
            if(parameters->codec_id == AV_CODEC_ID_HEVC &&
               parameters->profile == FF_PROFILE_HEVC_MAIN_10)
                return "H.265/HEVC Main10 is not supported. Re-export it as 8-bit SDR HEVC Main profile.";
            if(parameters->codec_id == AV_CODEC_ID_VP9 &&
               parameters->profile == FF_PROFILE_VP9_2)
                return "VP9 profile 2 is a 10-bit/HDR format and is not supported. Re-export it as 8-bit SDR VP9 profile 0.";
            const auto format = static_cast<AVPixelFormat>(parameters->format);
            if(format == AV_PIX_FMT_NONE)
                return {};
            const auto* description = av_pix_fmt_desc_get(format);
            if(!description)
                return {};
            if((description->flags & AV_PIX_FMT_FLAG_ALPHA) != 0)
                return "WebM alpha video is not supported. Export an ordinary 8-bit 4:2:0 video without transparency.";
            for(int component = 0; component < description->nb_components; ++component)
            {
                if(description->comp[component].depth > 8)
                    return "10-bit video is not supported. Re-export it as 8-bit SDR.";
            }
            if(description->log2_chroma_w != 1 ||
               description->log2_chroma_h != 1)
                return "Only 8-bit 4:2:0 video is supported. Re-export it using yuv420p.";
            return {};
        }

        LocalVideoFile ProbeLocalVideo(const std::filesystem::path& path)
        {
            LocalVideoFile result;
            result.fileName = path.filename().string();
            result.path = path;
            std::error_code sizeError;
            result.bytes = std::filesystem::file_size(path, sizeError);

            AVFormatContext* format = nullptr;
            int status = avformat_open_input(&format, path.string().c_str(), nullptr, nullptr);
            if(status < 0)
            {
                result.problem =
                    "Big Screen could not open this video. The file may still be copying, may be damaged, or may use an unsupported container. FFmpeg reported: " +
                    FfmpegError(status);
                return result;
            }

            const auto closeFormat = [&]()
            {
                if(format)
                    avformat_close_input(&format);
            };
            status = avformat_find_stream_info(format, nullptr);
            if(status < 0)
            {
                result.problem =
                    "Big Screen could not read this video's stream information. The file may be incomplete or damaged. FFmpeg reported: " +
                    FfmpegError(status);
                closeFormat();
                return result;
            }

            const std::string container = format->iformat && format->iformat->name
                ? format->iformat->name
                : "";
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            const bool mp4Container = container.find("mov") != std::string::npos ||
                container.find("mp4") != std::string::npos;
            const bool webmContainer = container.find("matroska") != std::string::npos ||
                container.find("webm") != std::string::npos;
            if((extension == ".mp4" && !mp4Container) ||
               (extension == ".webm" && !webmContainer))
            {
                result.problem =
                    "This file's extension does not match its internal container. Use MP4 for H.264/H.265 or WebM for VP8/VP9.";
                closeFormat();
                return result;
            }

            const int streamIndex = av_find_best_stream(
                format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if(streamIndex < 0)
            {
                result.problem =
                    "This file does not contain a video stream that Big Screen can decode.";
                closeFormat();
                return result;
            }

            const auto* parameters = format->streams[streamIndex]->codecpar;
            result.width = parameters ? parameters->width : 0;
            result.height = parameters ? parameters->height : 0;
            result.codec = parameters
                ? std::string(avcodec_get_name(parameters->codec_id))
                : "unknown";
            const bool codecMatchesContainer = parameters &&
                ((mp4Container &&
                  (parameters->codec_id == AV_CODEC_ID_H264 ||
                   parameters->codec_id == AV_CODEC_ID_HEVC)) ||
                 (webmContainer &&
                  (parameters->codec_id == AV_CODEC_ID_VP8 ||
                   parameters->codec_id == AV_CODEC_ID_VP9)));
            if(!codecMatchesContainer)
            {
                result.problem =
                    "This video uses " + result.codec +
                    ". Big Screen supports MP4 with H.264/H.265 and WebM with VP8/VP9.";
                closeFormat();
                return result;
            }
            if(const auto pixelProblem = UnsupportedPixelReason(parameters);
               !pixelProblem.empty())
            {
                result.problem = pixelProblem;
                closeFormat();
                return result;
            }
            if(result.width <= 0 || result.height <= 0)
            {
                result.problem = "This video reports an invalid frame size.";
                closeFormat();
                return result;
            }
            const int longEdge = std::max(result.width, result.height);
            const int shortEdge = std::min(result.width, result.height);
            if(longEdge > 2560 || shortEdge > 1440)
            {
                std::ostringstream message;
                message << "This video is " << result.width << 'x' << result.height
                        << ". Big Screen supports videos through 2560x1440 (or 1440x2560 portrait). Larger videos must be re-exported at 1440p or lower.";
                result.problem = message.str();
                closeFormat();
                return result;
            }

            const auto* stream = format->streams[streamIndex];
            if(stream->duration > 0)
                result.durationSeconds = stream->duration * av_q2d(stream->time_base);
            else if(format->duration > 0)
                result.durationSeconds =
                    format->duration / static_cast<double>(AV_TIME_BASE);
            result.compatible = true;
            closeFormat();
            return result;
        }

        /// Enumerates MP4 and WebM candidates without hiding invalid files. Showing a
        /// red HELP row is more useful than silently ignoring a mistyped or
        /// unsupported file that the user deliberately copied to the headset.
        std::vector<LocalVideoFile> DiscoverVideosInDirectory(
            const std::filesystem::path& directory)
        {
            std::vector<LocalVideoFile> files;
            std::error_code iteratorError;
            for(std::filesystem::directory_iterator iterator(directory, iteratorError), end;
                !iteratorError && iterator != end; iterator.increment(iteratorError))
            {
                std::error_code typeError;
                if(!iterator->is_regular_file(typeError) || typeError)
                    continue;
                auto extension = iterator->path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                if(extension == ".mp4" || extension == ".webm")
                    files.push_back(ProbeLocalVideo(iterator->path()));
            }
            std::sort(files.begin(), files.end(), [](const auto& left, const auto& right)
            {
                std::string leftName = left.fileName;
                std::string rightName = right.fileName;
                std::transform(leftName.begin(), leftName.end(), leftName.begin(),
                    [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                std::transform(rightName.begin(), rightName.end(), rightName.begin(),
                    [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                return leftName < rightName;
            });
            return files;
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
        importPath_ = rootPath_ / "Video Import";
        sharedStoragePath_ = "/sdcard";
        manifestPath_ = rootPath_ / "library.json";
        std::filesystem::create_directories(videoPath_);
        std::filesystem::create_directories(thumbnailPath_);
        std::filesystem::create_directories(runtimePath_);
        std::filesystem::create_directories(importPath_);

        // Builds before the private LGPL migration installed this GPL notice
        // into Big Screen's own Runtime directory. QMOD upgrades do not remove
        // renamed fileCopies, so clean up that obsolete mod-owned notice once;
        // leaving it behind would incorrectly describe the current FFmpeg
        // runtime. Never remove videos, library data, or any user-owned file.
        std::error_code obsoleteNoticeError;
        const auto obsoleteGplNotice = runtimePath_ / "FFMPEG-GPL-3.0-OR-LATER.txt";
        const bool removedObsoleteNotice =
            std::filesystem::remove(obsoleteGplNotice, obsoleteNoticeError);
        if(removedObsoleteNotice)
            PaperLogger.info("Removed the obsolete pre-LGPL FFmpeg license notice");
        else if(obsoleteNoticeError)
            PaperLogger.warn(
                "Could not remove obsolete FFmpeg notice '{}': {}",
                obsoleteGplNotice.string(),
                obsoleteNoticeError.message());

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

        {
            std::scoped_lock lock(mutex_);
            const auto cached = descriptorCache_.find(descriptor.levelId);
            if(cached != descriptorCache_.end())
            {
                // Detect manual removal of the selected source without paying
                // the much larger cost of reparsing metadata on every UI tick.
                if(!cached->second.playableConfig ||
                   std::filesystem::is_regular_file(
                       cached->second.playableConfig->videoPath))
                    return cached->second;
                descriptorCache_.erase(cached);
            }
        }

        std::filesystem::path levelDirectory;
        if(auto* custom = SongCore::API::Loading::GetLevelByLevelID(descriptor.levelId))
        {
            levelDirectory = std::filesystem::path(custom->get_customLevelPath());
            std::string error;
            descriptor.mapperDefinition = MapVideoConfig::LoadDefinitionFromLevel(
                levelDirectory,
                error);
            if(!error.empty())
            {
                PaperLogger.error("Video metadata rejected for '{}': {}", descriptor.levelId, error);
                ErrorManager::Instance().RecordError(
                    "Reading video metadata for " + descriptor.levelId,
                    error);
            }
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
        const auto userPath = saved
            ? ResolveStoredFile(videoPath_, importPath_, levelDirectory, saved->user)
            : std::nullopt;
        const auto mapperPath = saved
            ? ResolveStoredFile(videoPath_, importPath_, levelDirectory, saved->mapper)
            : std::nullopt;
        descriptor.hasUserOverride = userPath.has_value();
        descriptor.hasMapperDownload = mapperPath.has_value();
        descriptor.userOverrideIsMapLocal =
            descriptor.hasUserOverride && saved->user->mapLocal;
        descriptor.userOverrideIsImported =
            descriptor.hasUserOverride && saved->user->importFile;
        descriptor.userOverrideIsExternal =
            descriptor.hasUserOverride && saved->user->externalFile;

        MapVideoConfig effective = descriptor.mapperDefinition.value_or(MapVideoConfig{});
        if(descriptor.hasUserOverride)
        {
            const auto& record = *saved->user;
            effective.videoPath = *userPath;
            effective.offsetSeconds = record.offsetSeconds;
            effective.playbackRate = record.playbackRate;
            effective.fitToSong = record.fitToSong;
            effective.blackDuringLeadIn = record.blackDuringLeadIn;
            effective.declaredDurationSeconds = record.durationSeconds;
            effective.title = record.title.empty()
                ? std::optional<std::string>{}
                : std::optional<std::string>{record.title};
            if(record.sourceUrl.empty())
                descriptor.downloadUrl.reset();
            else
                descriptor.downloadUrl = record.sourceUrl;
            descriptor.downloadOrigin = VideoOrigin::User;
            if(IsUserOwnedFile(record))
                descriptor.activeMapFileName = record.fileName;
            else
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
            descriptor.activeMapFileName = effective.videoPath.filename().string();
            descriptor.playableConfig = effective;
        }
        else if(descriptor.hasMapperDownload)
        {
            const auto& record = *saved->mapper;
            effective.videoPath = *mapperPath;
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

        descriptorCache_.insert_or_assign(descriptor.levelId, descriptor);
        return descriptor;
    }

    std::optional<MapVideoConfig> VideoLibrary::ResolvePlayback(
        GlobalNamespace::BeatmapLevel* level) const
    {
        return Describe(level).playableConfig;
    }

    std::filesystem::path VideoLibrary::AllocateVideoPath(
        const std::string& levelId,
        VideoOrigin origin,
        std::string_view extension) const
    {
        std::scoped_lock lock(mutex_);
        std::string normalizedExtension(extension);
        std::transform(
            normalizedExtension.begin(), normalizedExtension.end(),
            normalizedExtension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if(normalizedExtension != ".mp4" && normalizedExtension != ".webm")
            normalizedExtension = ".mp4";
        const auto suffix = origin == VideoOrigin::User
            ? "-user" + normalizedExtension
            : "-mapper" + normalizedExtension;
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
        if(previous && !IsUserOwnedFile(*previous) &&
           previous->fileName != target->fileName)
            RemoveManagedFile(videoPath_, previous->fileName);
    }

    std::vector<LocalVideoFile> VideoLibrary::DiscoverLocalVideos(
        GlobalNamespace::BeatmapLevel* level) const
    {
        std::vector<LocalVideoFile> files;
        if(!level || !level->levelID)
            return files;
        auto* custom = SongCore::API::Loading::GetLevelByLevelID(
            std::string(level->levelID));
        if(!custom)
            return files;

        return DiscoverVideosInDirectory(
            std::filesystem::path(custom->get_customLevelPath()));
    }

    std::vector<LocalVideoFile> VideoLibrary::DiscoverImportedVideos() const
    {
        std::filesystem::path directory;
        {
            std::scoped_lock lock(mutex_);
            directory = importPath_;
        }
        return DiscoverVideosInDirectory(directory);
    }

    LocalVideoFile VideoLibrary::InspectLocalVideo(
        const std::filesystem::path& path) const
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
        if(extension != ".mp4" && extension != ".webm")
        {
            LocalVideoFile result;
            result.fileName = path.filename().string();
            result.path = path;
            result.problem = "Big Screen can select MP4 and WebM video files only.";
            return result;
        }
        return ProbeLocalVideo(path);
    }

    bool VideoLibrary::SetVideoFileOverride(
        GlobalNamespace::BeatmapLevel* level,
        const std::filesystem::path& requestedPath,
        std::string& error)
    {
        error.clear();
        if(!level || !level->levelID)
        {
            error = "Select a song before assigning a local video.";
            return false;
        }

        std::error_code pathError;
        const auto path = std::filesystem::absolute(requestedPath, pathError)
            .lexically_normal();
        std::filesystem::path sharedRoot;
        std::filesystem::path importDirectory;
        {
            std::scoped_lock lock(mutex_);
            sharedRoot = sharedStoragePath_;
            importDirectory = importPath_;
        }
        if(pathError || !path.is_absolute() ||
           !Utility::IsPathInside(path, sharedRoot) ||
           !Utility::IsRegularFile(path))
        {
            error = "The selected video is no longer available in Quest shared storage.";
            return false;
        }

        const auto probe = InspectLocalVideo(path);
        if(!probe.compatible)
        {
            error = probe.problem.empty()
                ? "This local MP4 is not compatible with Big Screen."
                : probe.problem;
            return false;
        }

        std::filesystem::path mapDirectory;
        if(auto* custom = SongCore::API::Loading::GetLevelByLevelID(
               std::string(level->levelID)))
            mapDirectory = std::filesystem::path(custom->get_customLevelPath())
                .lexically_normal();

        StoredVideo video;
        video.fileName = probe.fileName;
        video.title = probe.fileName;
        video.codec = probe.codec;
        video.durationSeconds = probe.durationSeconds;
        video.bytes = probe.bytes;
        video.width = probe.width;
        video.height = probe.height;
        if(!mapDirectory.empty() && path.parent_path() == mapDirectory)
        {
            video.mapLocal = true;
        }
        else if(path.parent_path() == importDirectory.lexically_normal())
        {
            video.importFile = true;
        }
        else
        {
            video.externalFile = true;
            video.externalPath = path.string();
        }

        const std::string levelId(level->levelID);
        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end())
        {
            records_.emplace_back(levelId, LevelVideoRecords{});
            found = std::prev(records_.end());
        }
        found->second.songName = level->songName
            ? std::string(level->songName) : "Unknown Song";
        found->second.songAuthor = level->songAuthorName
            ? std::string(level->songAuthorName) : std::string{};
        const auto previous = found->second.user;
        found->second.user = std::move(video);
        SaveLocked();

        // Only Big Screen-managed downloads are deleted on replacement. Every
        // browser-picked file remains exactly where the user placed it.
        if(previous && !IsUserOwnedFile(*previous))
            RemoveManagedFile(videoPath_, previous->fileName);
        std::filesystem::remove(
            thumbnailPath_ / (StableKey(levelId) + "-user.jpg"));
        PaperLogger.info(
            "Assigned local video '{}' to '{}' from '{}'",
            probe.fileName,
            levelId,
            path.string());
        return true;
    }

    bool VideoLibrary::SetLocalVideoOverride(
        GlobalNamespace::BeatmapLevel* level,
        const std::string& fileName,
        std::string& error)
    {
        error.clear();
        if(!level || !level->levelID)
        {
            error = "Select a custom or WIP map before assigning a local video.";
            return false;
        }
        auto* custom = SongCore::API::Loading::GetLevelByLevelID(
            std::string(level->levelID));
        if(!custom)
        {
            error = "Local map-folder videos are available only for custom and WIP maps.";
            return false;
        }

        const std::filesystem::path relative(fileName);
        if(relative.empty() || relative.is_absolute() || relative.filename() != relative)
        {
            error = "The local video filename is invalid.";
            return false;
        }
        const std::filesystem::path directory(custom->get_customLevelPath());
        const auto path = (directory / relative).lexically_normal();
        if(!Utility::IsPathInside(path, directory) ||
           !Utility::IsRegularFile(path))
        {
            error = "The selected MP4 is no longer present in this map folder.";
            return false;
        }

        const auto probe = ProbeLocalVideo(path);
        if(!probe.compatible)
        {
            error = probe.problem.empty()
                ? "This local MP4 is not compatible with Big Screen."
                : probe.problem;
            return false;
        }

        const std::string levelId(level->levelID);
        StoredVideo video;
        video.fileName = probe.fileName;
        video.title = probe.fileName;
        video.codec = probe.codec;
        video.mapLocal = true;
        video.durationSeconds = probe.durationSeconds;
        video.bytes = probe.bytes;
        video.width = probe.width;
        video.height = probe.height;

        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end())
        {
            records_.emplace_back(levelId, LevelVideoRecords{});
            found = std::prev(records_.end());
        }
        found->second.songName = level->songName
            ? std::string(level->songName)
            : "Unknown Song";
        found->second.songAuthor = level->songAuthorName
            ? std::string(level->songAuthorName)
            : std::string{};
        const auto previous = found->second.user;
        found->second.user = std::move(video);
        SaveLocked();

        // A map-local file is never owned or deleted by Big Screen. A managed
        // YouTube override being explicitly replaced is removed to avoid an
        // inaccessible orphan consuming the headset's storage.
        if(previous && !IsUserOwnedFile(*previous))
            RemoveManagedFile(videoPath_, previous->fileName);
        std::filesystem::remove(
            thumbnailPath_ / (StableKey(levelId) + "-user.jpg"));
        PaperLogger.info(
            "Assigned local map video '{}' to '{}'",
            probe.fileName,
            levelId);
        return true;
    }

    bool VideoLibrary::SetImportedVideoOverride(
        GlobalNamespace::BeatmapLevel* level,
        const std::string& fileName,
        std::string& error)
    {
        error.clear();
        if(!level || !level->levelID)
        {
            error = "Select a song before assigning a video from the import folder.";
            return false;
        }

        const std::filesystem::path relative(fileName);
        if(relative.empty() || relative.is_absolute() || relative.filename() != relative)
        {
            error = "The imported video filename is invalid.";
            return false;
        }
        std::filesystem::path directory;
        {
            std::scoped_lock lock(mutex_);
            directory = importPath_;
        }
        const auto path = (directory / relative).lexically_normal();
        if(!Utility::IsPathInside(path, directory) ||
           !Utility::IsRegularFile(path))
        {
            error = "The selected MP4 is no longer in Big Screen's Video Import folder.";
            return false;
        }

        const auto probe = ProbeLocalVideo(path);
        if(!probe.compatible)
        {
            error = probe.problem.empty()
                ? "This imported MP4 is not compatible with Big Screen."
                : probe.problem;
            return false;
        }

        const std::string levelId(level->levelID);
        StoredVideo video;
        video.fileName = probe.fileName;
        video.title = probe.fileName;
        video.codec = probe.codec;
        video.importFile = true;
        video.durationSeconds = probe.durationSeconds;
        video.bytes = probe.bytes;
        video.width = probe.width;
        video.height = probe.height;

        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end())
        {
            records_.emplace_back(levelId, LevelVideoRecords{});
            found = std::prev(records_.end());
        }
        found->second.songName = level->songName
            ? std::string(level->songName) : "Unknown Song";
        found->second.songAuthor = level->songAuthorName
            ? std::string(level->songAuthorName) : std::string{};
        const auto previous = found->second.user;
        found->second.user = std::move(video);
        SaveLocked();

        // Import files remain user-owned. Only a replaced Big Screen download
        // is deleted; another import or map-folder file is merely unregistered.
        if(previous && !IsUserOwnedFile(*previous))
            RemoveManagedFile(videoPath_, previous->fileName);
        std::filesystem::remove(
            thumbnailPath_ / (StableKey(levelId) + "-user.jpg"));
        PaperLogger.info("Assigned imported video '{}' to '{}'", probe.fileName, levelId);
        return true;
    }

    bool VideoLibrary::RemoveUserOverride(const std::string& levelId, bool deleteFile)
    {
        std::scoped_lock lock(mutex_);
        auto found = FindRecord(records_, levelId);
        if(found == records_.end() || !found->second.user)
            return false;
        if(deleteFile && !IsUserOwnedFile(*found->second.user))
            RemoveManagedFile(videoPath_, found->second.user->fileName);
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
        RemoveManagedFile(videoPath_, found->second.mapper->fileName);
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

    std::uint64_t VideoLibrary::ManagedBytesForLevel(
        const std::string& levelId) const
    {
        std::scoped_lock lock(mutex_);
        const auto found = FindRecord(records_, levelId);
        if(found == records_.end())
            return 0;

        std::uint64_t total = 0;
        std::string countedFile;
        const auto addManagedFile = [&](const std::optional<StoredVideo>& video)
        {
            if(!video || IsUserOwnedFile(*video) || video->fileName.empty() ||
               video->fileName == countedFile)
                return;
            const auto path = videoPath_ / video->fileName;
            std::error_code error;
            const auto bytes = std::filesystem::file_size(path, error);
            if(!error)
            {
                total += bytes;
                countedFile = video->fileName;
            }
        };
        addManagedFile(found->second.mapper);
        addManagedFile(found->second.user);
        return total;
    }

    std::uint64_t VideoLibrary::LibraryBytes() const
    {
        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if(libraryBytesCacheTime_ != std::chrono::steady_clock::time_point{} &&
           now - libraryBytesCacheTime_ < std::chrono::seconds(1))
            return cachedLibraryBytes_;
        std::uint64_t total = 0;
        std::error_code error;
        if(!std::filesystem::exists(videoPath_, error) || error)
            return total;
        std::filesystem::directory_iterator iterator(videoPath_, error);
        const std::filesystem::directory_iterator end;
        while(!error && iterator != end)
        {
            const auto& entry = *iterator;
            std::error_code entryError;
            if(entry.is_regular_file(entryError) && !entryError)
            {
                const auto bytes = entry.file_size(entryError);
                if(!entryError)
                    total += bytes;
            }
            iterator.increment(error);
        }
        cachedLibraryBytes_ = total;
        libraryBytesCacheTime_ = now;
        return cachedLibraryBytes_;
    }

    std::uint64_t VideoLibrary::FreeBytes() const
    {
        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if(freeBytesCacheTime_ != std::chrono::steady_clock::time_point{} &&
           now - freeBytesCacheTime_ < std::chrono::seconds(1))
            return cachedFreeBytes_;
        std::error_code error;
        const auto info = std::filesystem::space(rootPath_, error);
        cachedFreeBytes_ = error ? 0 : info.available;
        freeBytesCacheTime_ = now;
        return cachedFreeBytes_;
    }

    bool VideoLibrary::TryLoadManifestLocked(
        const std::filesystem::path& path,
        std::vector<std::pair<std::string, LevelVideoRecords>>& output) const
    {
        output.clear();
        std::ifstream stream(path, std::ios::binary);
        if(!stream)
            return false;
        const std::string json{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        rapidjson::Document document;
        document.Parse(json.data(), json.size());
        const auto* levels = Member(document, "levels");
        if(document.HasParseError() || !levels || !levels->IsObject())
            return false;

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
            output.emplace_back(
                std::string(member->name.GetString(), member->name.GetStringLength()),
                std::move(level));
        }
        return true;
    }

    void VideoLibrary::LoadLocked()
    {
        records_.clear();
        descriptorCache_.clear();
        libraryBytesCacheTime_ = {};
        freeBytesCacheTime_ = {};
        recoveryScanNeeded_ = false;
        std::error_code existsError;
        const bool primaryExists =
            std::filesystem::exists(manifestPath_, existsError) && !existsError;
        if(primaryExists && TryLoadManifestLocked(manifestPath_, records_))
            return;

        // Preserve the corrupt bytes for troubleshooting before attempting a
        // backup. A timestamped quarantine is never used as an automatic
        // source, so repeated starts cannot cycle damaged data back into use.
        std::error_code error;
        if(primaryExists)
        {
            const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const auto quarantine = manifestPath_.string() +
                ".corrupt-" + std::to_string(stamp);
            std::filesystem::rename(manifestPath_, quarantine, error);
            if(error)
            {
                PaperLogger.error("Could not quarantine invalid library manifest: {}", error.message());
                ErrorManager::Instance().RecordError(
                    "Quarantining an invalid video library",
                    error.message());
            }
        }

        const std::array backups{
            std::filesystem::path(manifestPath_.string() + ".backup1"),
            std::filesystem::path(manifestPath_.string() + ".backup2")};
        if(!primaryExists)
        {
            std::error_code backupError;
            const bool anyBackup = std::any_of(
                backups.begin(), backups.end(), [&](const auto& backup)
                {
                    backupError.clear();
                    return std::filesystem::is_regular_file(
                        backup, backupError) && !backupError;
                });
            if(!anyBackup)
                return; // genuine first run, not a recovery event
        }
        for(std::size_t index = 0; index < backups.size(); ++index)
        {
            std::vector<std::pair<std::string, LevelVideoRecords>> restored;
            if(!TryLoadManifestLocked(backups[index], restored))
                continue;
            records_ = std::move(restored);
            const auto temporary = manifestPath_.string() + ".restore.tmp";
            error.clear();
            std::filesystem::copy_file(
                backups[index], temporary,
                std::filesystem::copy_options::overwrite_existing, error);
            if(!error)
            {
                std::error_code removeError;
                std::filesystem::remove(manifestPath_, removeError);
                if(removeError)
                    error = removeError;
                else
                    std::filesystem::rename(temporary, manifestPath_, error);
            }
            if(error)
            {
                std::error_code cleanupError;
                std::filesystem::remove(temporary, cleanupError);
                const std::string detail =
                    "Backup " + std::to_string(index + 1) +
                    " was loaded in memory, but library.json could not be restored: " +
                    error.message();
                PaperLogger.error("{}", detail);
                ErrorManager::Instance().RecordError(
                    "Restoring the video library backup", detail);
                recoveryNotice_ =
                    "Big Screen recovered your video assignments for this session, but could not rewrite the library file. See the error log before restarting Beat Saber.";
            }
            else
            {
                recoveryNotice_ = primaryExists
                    ? "Big Screen detected a damaged video library and restored the most recent known-good backup. Your video assignments were preserved."
                    : "Big Screen found that the main video library was missing and restored the most recent known-good backup. Your video assignments were preserved.";
                PaperLogger.warn("Recovered video library from backup {}", index + 1);
            }
            return;
        }

        // SongCore may not have finished loading when the mod initializes.
        // Defer filename-to-level reconstruction until the library catalog has
        // real BeatmapLevel objects and stable IDs available.
        recoveryScanNeeded_ = true;
        recoveryNotice_ =
            "Big Screen could not read the video library or either backup. It will rebuild every recoverable downloaded-video assignment after the song library finishes loading.";
        PaperLogger.error("Video library and both known-good backups are invalid");
        ErrorManager::Instance().RecordError(
            "Recovering the video library",
            "The primary library and both known-good backups were invalid");
    }

    void VideoLibrary::SaveLocked() const
    {
        // All record mutations flow through SaveLocked. Invalidate derived UI
        // state here once so no setter can accidentally forget either cache.
        descriptorCache_.clear();
        libraryBytesCacheTime_ = {};
        freeBytesCacheTime_ = {};
        rapidjson::Document document(rapidjson::kObjectType);
        auto& allocator = document.GetAllocator();
        // Schema 3 adds externalFile/externalPath records for videos selected
        // through the Quest file browser. Older version-2 libraries remain
        // readable because all new fields are optional.
        document.AddMember("version", 3, allocator);
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
        // Validate the complete serialized document before it can replace any
        // known-good file. This catches programming errors as well as partial
        // in-memory serialization failures.
        rapidjson::Document validation;
        validation.Parse(buffer.GetString(), buffer.GetSize());
        if(validation.HasParseError())
            throw std::runtime_error("Generated video library JSON was invalid");

        const auto temporary = manifestPath_.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
            stream.flush();
            if(!stream)
                throw std::runtime_error("Could not write video library manifest");
        }
        const auto backup1 = std::filesystem::path(manifestPath_.string() + ".backup1");
        const auto backup2 = std::filesystem::path(manifestPath_.string() + ".backup2");
        std::error_code error;
        std::vector<std::pair<std::string, LevelVideoRecords>> knownGood;
        if(TryLoadManifestLocked(backup1, knownGood))
        {
            std::filesystem::copy_file(
                backup1, backup2,
                std::filesystem::copy_options::overwrite_existing, error);
            if(error)
                PaperLogger.warn(
                    "Could not rotate video library backup 1 to backup 2: {}",
                    error.message());
        }
        error.clear();
        if(TryLoadManifestLocked(manifestPath_, knownGood))
        {
            std::filesystem::copy_file(
                manifestPath_, backup1,
                std::filesystem::copy_options::overwrite_existing, error);
            if(error)
                PaperLogger.warn(
                    "Could not refresh video library backup 1: {}",
                    error.message());
        }

        // Keep the proven shared-storage replacement sequence: remove only the
        // exact manifest path after the fully flushed temporary file and
        // rotating backups exist. LoadLocked treats a missing primary plus an
        // intact backup as recovery, so a crash at this boundary preserves the
        // assignments instead of presenting an empty first-run library.
        error.clear();
        std::filesystem::remove(manifestPath_, error);
        error.clear();
        std::filesystem::rename(temporary, manifestPath_, error);
        if(error)
            throw std::runtime_error(
                "Could not replace video library manifest: " + error.message());
    }

    std::optional<std::string> VideoLibrary::TakeRecoveryNotice()
    {
        std::scoped_lock lock(mutex_);
        auto notice = recoveryNotice_;
        recoveryNotice_.reset();
        return notice;
    }

    void VideoLibrary::RecoverManagedFiles(
        const std::vector<GlobalNamespace::BeatmapLevel*>& installedLevels)
    {
        std::scoped_lock lock(mutex_);
        if(!recoveryScanNeeded_)
            return;

        std::size_t recovered = 0;
        for(auto* level : installedLevels)
        {
            if(!level || !level->levelID)
                continue;
            const std::string levelId(level->levelID);
            LevelVideoRecords record;
            record.songName = level->songName ? std::string(level->songName) : "Unknown Song";
            record.songAuthor = level->songAuthorName
                ? std::string(level->songAuthorName) : std::string{};
            const auto key = StableKey(levelId);
            const auto recoverOne = [&](const char* stem) -> std::optional<StoredVideo>
            {
                std::filesystem::path path;
                std::filesystem::file_time_type newest{};
                for(const auto extension : {".mp4", ".webm"})
                {
                    const auto candidate = videoPath_ /
                        (key + stem + extension);
                    std::error_code timeError;
                    if(!std::filesystem::is_regular_file(candidate, timeError))
                        continue;
                    const auto modified =
                        std::filesystem::last_write_time(candidate, timeError);
                    if(path.empty() || (!timeError && modified > newest))
                    {
                        path = candidate;
                        if(!timeError) newest = modified;
                    }
                }
                if(path.empty())
                    return std::nullopt;
                const auto probe = ProbeLocalVideo(path);
                if(!probe.compatible)
                    return std::nullopt;
                StoredVideo video;
                video.fileName = probe.fileName;
                video.title = probe.fileName;
                video.codec = probe.codec;
                video.durationSeconds = probe.durationSeconds;
                video.bytes = probe.bytes;
                video.width = probe.width;
                video.height = probe.height;
                return video;
            };
            record.user = recoverOne("-user");
            record.mapper = recoverOne("-mapper");
            if(!record.user && !record.mapper)
                continue;
            records_.emplace_back(levelId, std::move(record));
            ++recovered;
        }
        recoveryScanNeeded_ = false;
        SaveLocked();
        recoveryNotice_ = "Big Screen rebuilt " + std::to_string(recovered) +
            " downloaded-video assignment" + (recovered == 1 ? "." : "s.") +
            " Timing was reset to safe defaults because the damaged manifest could not be read.";
        PaperLogger.warn("Rebuilt {} video library entries from managed files", recovered);
    }

    std::string VideoLibrary::StableKey(const std::string& levelId)
    {
        return CoreLogic::StableVideoKey(levelId);
    }
}
