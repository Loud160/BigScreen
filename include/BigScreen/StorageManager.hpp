#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace BigScreen {
    enum class StorageState { Idle, Scanning, Ready, Cleaning, Completed, Failed };

    struct StorageCleanupItem {
        std::filesystem::path path;
        std::string category;
        std::uint64_t bytes = 0;
    };

    struct StorageSnapshot {
        StorageState state = StorageState::Idle;
        std::string message;
        std::vector<StorageCleanupItem> items;
        std::uint64_t removableBytes = 0;
        std::uint64_t downloadedBytes = 0;
        std::uint64_t importedBytes = 0;
        std::uint64_t freeBytes = 0;
    };

    /// Scans and removes only Big Screen-owned orphan/cache files. Map-folder
    /// and Video Import MP4s are user-owned and are never cleanup candidates.
    class StorageManager final {
    public:
        static StorageManager& Instance();
        ~StorageManager();

        bool StartScan(std::string& error);
        bool StartCleanup(
            const std::vector<std::filesystem::path>& selectedPaths,
            std::string& error);
        StorageSnapshot Snapshot() const;

    private:
        StorageManager() = default;
        StorageManager(const StorageManager&) = delete;
        StorageManager& operator=(const StorageManager&) = delete;
        void ScanWorker();
        void CleanupWorker(std::vector<StorageCleanupItem> approved);

        mutable std::mutex mutex_;
        std::thread worker_;
        StorageSnapshot snapshot_;
    };
}
