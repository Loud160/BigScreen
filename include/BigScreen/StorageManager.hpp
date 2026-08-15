// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
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
    /// and Video Import MP4/WebM files are user-owned and never candidates.
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
        // Serializes start/join/state replacement without making a completed
        // worker wait on the state mutex while the caller is joining it.
        std::mutex startMutex_;
        std::thread worker_;
        StorageSnapshot snapshot_;
    };
}
