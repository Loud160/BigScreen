#include "BigScreen/StorageManager.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_set>

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        std::uint64_t DirectoryBytes(const std::filesystem::path& directory)
        {
            std::uint64_t total = 0;
            std::error_code error;
            for(std::filesystem::directory_iterator it(directory, error), end;
                !error && it != end; it.increment(error))
            {
                std::error_code fileError;
                if(it->is_regular_file(fileError) && !fileError)
                    total += it->file_size(fileError);
            }
            return total;
        }

        bool IsInside(const std::filesystem::path& child,
                      const std::filesystem::path& parent)
        {
            const auto normalizedChild = std::filesystem::absolute(child).lexically_normal();
            const auto normalizedParent = std::filesystem::absolute(parent).lexically_normal();
            auto childPart = normalizedChild.begin();
            for(auto parentPart = normalizedParent.begin();
                parentPart != normalizedParent.end(); ++parentPart, ++childPart)
                if(childPart == normalizedChild.end() || *childPart != *parentPart)
                    return false;
            return true;
        }

        bool IsAbandonedTemporary(const std::filesystem::directory_entry& entry)
        {
            const auto extension = entry.path().extension().string();
            if(extension != ".part" && extension != ".tmp")
                return false;
            std::error_code error;
            const auto age = std::filesystem::file_time_type::clock::now() -
                entry.last_write_time(error);
            return !error && age > std::chrono::hours(1);
        }
    }

    StorageManager& StorageManager::Instance()
    {
        static StorageManager manager;
        return manager;
    }

    StorageManager::~StorageManager()
    {
        if(worker_.joinable()) worker_.join();
    }

    bool StorageManager::StartScan(std::string& error)
    {
        std::scoped_lock lock(mutex_);
        if(snapshot_.state == StorageState::Scanning ||
           snapshot_.state == StorageState::Cleaning)
        {
            error = "A storage operation is already running.";
            return false;
        }
        if(worker_.joinable()) worker_.join();
        snapshot_ = {};
        snapshot_.state = StorageState::Scanning;
        snapshot_.message = "Scanning Big Screen storage...";
        worker_ = std::thread([this]() { ScanWorker(); });
        return true;
    }

    bool StorageManager::StartCleanup(
        const std::vector<std::filesystem::path>& selectedPaths,
        std::string& error)
    {
        std::scoped_lock lock(mutex_);
        if(snapshot_.state != StorageState::Ready)
        {
            error = "Run a storage scan before cleaning.";
            return false;
        }
        if(selectedPaths.empty())
        {
            error = "Select at least one listed file before cleaning.";
            return false;
        }
        if(worker_.joinable()) worker_.join();

        // The UI submits path identities, but deletion authority still comes
        // exclusively from the manager's most recent scan. Intersecting the
        // request with that immutable result prevents a stale or malformed UI
        // path from expanding cleanup beyond files the scanner approved.
        std::unordered_set<std::string> selected;
        selected.reserve(selectedPaths.size());
        for(const auto& path : selectedPaths)
            selected.emplace(path.lexically_normal().string());
        std::vector<StorageCleanupItem> approved;
        approved.reserve(selected.size());
        for(const auto& item : snapshot_.items)
            if(selected.contains(item.path.lexically_normal().string()))
                approved.push_back(item);
        if(approved.empty())
        {
            error = "The selected files are no longer part of the current storage scan. Scan again and retry.";
            return false;
        }

        snapshot_.state = StorageState::Cleaning;
        snapshot_.message = "Removing " + std::to_string(approved.size()) +
            " selected file(s)...";
        worker_ = std::thread([this, approved]() { CleanupWorker(approved); });
        return true;
    }

    StorageSnapshot StorageManager::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        return snapshot_;
    }

    void StorageManager::ScanWorker()
    {
        try
        {
            const auto& library = VideoLibrary::Instance();
            const auto root = library.RootPath();
            const auto videos = library.VideoPath();
            const auto thumbnails = library.ThumbnailPath();
            const auto runtime = library.RuntimePath();
            std::unordered_set<std::string> usedVideos;
            std::unordered_set<std::string> usedThumbnails;
            for(const auto& [levelId, record] : library.Records())
            {
                const auto collect = [&](const std::optional<StoredVideo>& video,
                                         const char* thumbnailSuffix)
                {
                    if(!video || video->fileName.empty() || video->mapLocal ||
                       video->importFile || video->externalFile)
                        return;
                    usedVideos.emplace(video->fileName);
                    usedThumbnails.emplace(
                        library.AllocateThumbnailPath(
                            levelId,
                            thumbnailSuffix[0] == 'u'
                                ? VideoOrigin::User : VideoOrigin::Mapper)
                            .filename().string());
                };
                collect(record.user, "user");
                collect(record.mapper, "mapper");
            }

            StorageSnapshot result;
            result.state = StorageState::Ready;
            result.downloadedBytes = DirectoryBytes(videos);
            result.importedBytes = DirectoryBytes(library.ImportPath());
            result.freeBytes = library.FreeBytes();
            const auto add = [&](const std::filesystem::path& path,
                                 std::string category)
            {
                std::error_code error;
                const auto bytes = std::filesystem::file_size(path, error);
                if(error) return;
                result.items.push_back({path, std::move(category), bytes});
                result.removableBytes += bytes;
            };

            std::error_code iteratorError;
            for(std::filesystem::directory_iterator it(videos, iteratorError), end;
                !iteratorError && it != end; it.increment(iteratorError))
            {
                std::error_code typeError;
                if(!it->is_regular_file(typeError) || typeError) continue;
                if(IsAbandonedTemporary(*it))
                    add(it->path(), "Abandoned download");
                else if(it->path().extension() == ".mp4" &&
                        !usedVideos.contains(it->path().filename().string()))
                    add(it->path(), "Unassigned Big Screen download");
            }
            iteratorError.clear();
            for(std::filesystem::directory_iterator it(thumbnails, iteratorError), end;
                !iteratorError && it != end; it.increment(iteratorError))
            {
                std::error_code typeError;
                if(!it->is_regular_file(typeError) || typeError) continue;
                if(IsAbandonedTemporary(*it) ||
                   !usedThumbnails.contains(it->path().filename().string()))
                    add(it->path(), "Unused thumbnail");
            }
            iteratorError.clear();
            const auto downloaderSnapshot = DownloadManager::Instance().Snapshot();
            const bool updaterActive = downloaderSnapshot.levelId == "__updater__" &&
                downloaderSnapshot.Active();
            for(std::filesystem::directory_iterator it(runtime, iteratorError), end;
                !iteratorError && it != end; it.increment(iteratorError))
            {
                std::error_code typeError;
                if(!it->is_regular_file(typeError) || typeError) continue;
                const auto name = it->path().filename().string();
                if((name == "yt-dlp-next.part" && !updaterActive) ||
                   IsAbandonedTemporary(*it))
                    add(it->path(), "Abandoned temporary file");
            }
            std::sort(result.items.begin(), result.items.end(), [](const auto& a, const auto& b) {
                return a.path.filename().string() < b.path.filename().string();
            });
            result.message = result.items.empty()
                ? "No removable Big Screen files were found."
                : std::to_string(result.items.size()) +
                    " removable file(s) found. Review the list before cleaning.";
            std::scoped_lock lock(mutex_);
            snapshot_ = std::move(result);
        }
        catch(const std::exception& exception)
        {
            ErrorManager::Instance().ReportInternal("scanning storage", exception.what());
            std::scoped_lock lock(mutex_);
            snapshot_.state = StorageState::Failed;
            snapshot_.message = "Storage scan failed. See the Big Screen log for details.";
        }
        catch(...)
        {
            ErrorManager::Instance().ReportInternal(
                "scanning storage", "Unknown native exception");
            std::scoped_lock lock(mutex_);
            snapshot_.state = StorageState::Failed;
            snapshot_.message = "Storage scan failed. See the Big Screen log for details.";
        }
    }

    void StorageManager::CleanupWorker(std::vector<StorageCleanupItem> approved)
    {
        try
        {
            const auto root = VideoLibrary::Instance().RootPath();
            std::size_t removed = 0;
            std::unordered_set<std::string> removedPaths;
            for(const auto& item : approved)
            {
                // Revalidate every exact scan result immediately before
                // deletion. No glob, unresolved environment variable, map
                // directory, or import directory can reach this operation.
                if(!IsInside(item.path, root) ||
                   IsInside(item.path, VideoLibrary::Instance().ImportPath()))
                    continue;
                std::error_code error;
                if(std::filesystem::remove(item.path, error) && !error)
                {
                    ++removed;
                    removedPaths.emplace(item.path.lexically_normal().string());
                }
            }
            std::scoped_lock lock(mutex_);
            snapshot_.items.erase(
                std::remove_if(
                    snapshot_.items.begin(),
                    snapshot_.items.end(),
                    [&removedPaths](const StorageCleanupItem& item)
                    {
                        return removedPaths.contains(
                            item.path.lexically_normal().string());
                    }),
                snapshot_.items.end());
            snapshot_.removableBytes = 0;
            for(const auto& item : snapshot_.items)
                snapshot_.removableBytes += item.bytes;
            snapshot_.downloadedBytes = DirectoryBytes(
                VideoLibrary::Instance().VideoPath());
            snapshot_.importedBytes = DirectoryBytes(
                VideoLibrary::Instance().ImportPath());
            snapshot_.freeBytes = VideoLibrary::Instance().FreeBytes();
            snapshot_.state = snapshot_.items.empty()
                ? StorageState::Completed
                : StorageState::Ready;
            snapshot_.message = "Removed " + std::to_string(removed) +
                " selected Big Screen file(s).";
            if(!snapshot_.items.empty())
                snapshot_.message += " " + std::to_string(snapshot_.items.size()) +
                    " unselected or unavailable file(s) remain.";
        }
        catch(const std::exception& exception)
        {
            ErrorManager::Instance().ReportInternal("cleaning storage", exception.what());
            std::scoped_lock lock(mutex_);
            snapshot_.state = StorageState::Failed;
            snapshot_.message = "Storage cleanup failed. See the Big Screen log for details.";
        }
        catch(...)
        {
            ErrorManager::Instance().ReportInternal(
                "cleaning storage", "Unknown native exception");
            std::scoped_lock lock(mutex_);
            snapshot_.state = StorageState::Failed;
            snapshot_.message = "Storage cleanup failed. See the Big Screen log for details.";
        }
    }
}
