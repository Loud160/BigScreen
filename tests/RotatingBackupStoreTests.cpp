// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#include "BigScreen/RotatingBackupStore.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
    namespace fs = std::filesystem;

    void Write(const fs::path& path, const std::string& value)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << value;
        assert(stream.good());
    }

    std::string Read(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
    }

    bool IsValid(const fs::path& path)
    {
        return Read(path).starts_with("valid:");
    }

    struct TemporaryDirectory final {
        TemporaryDirectory()
        {
            const auto stamp = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            path = fs::temp_directory_path() /
                ("bigscreen-rotating-store-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
        fs::path path;
    };
}

int main()
{
    using namespace BigScreen::RotatingBackupStore;

    // A successful save rotates only validator-approved generations.
    {
        TemporaryDirectory directory;
        const auto primary = directory.path / "library.json";
        const auto backup1 = directory.path / "library.json.backup1";
        const auto backup2 = directory.path / "library.json.backup2";
        Write(primary, "valid:primary");
        Write(backup1, "valid:previous");
        int warnings = 0;
        RotateKnownGood(
            primary, backup1, backup2, IsValid,
            [&](int, int, const std::string&) { ++warnings; });
        assert(warnings == 0);
        assert(Read(backup1) == "valid:primary");
        assert(Read(backup2) == "valid:previous");
    }

    // A corrupt newest backup is skipped and the older known-good generation
    // is atomically republished as the primary file.
    {
        TemporaryDirectory directory;
        const auto primary = directory.path / "library.json";
        const std::array backups{
            directory.path / "library.json.backup1",
            directory.path / "library.json.backup2"};
        Write(primary, "corrupt-primary");
        Write(backups[0], "corrupt-backup");
        Write(backups[1], "valid:older");
        std::string loaded;
        const auto result = RestoreFirstValid(
            primary, backups, [&](const auto& candidate)
            {
                loaded = Read(candidate);
                return loaded.starts_with("valid:");
            });
        assert(result.foundValidBackup);
        assert(result.restoredPrimary);
        assert(result.backupIndex == 1);
        assert(loaded == "valid:older");
        assert(Read(primary) == "valid:older");
        assert(!fs::exists(primary.string() + ".restore.tmp"));
    }

    // A missing primary is a recoverable interrupted-write state whenever a
    // known-good backup remains.
    {
        TemporaryDirectory directory;
        const auto primary = directory.path / "library.json";
        const std::array backups{
            directory.path / "library.json.backup1",
            directory.path / "library.json.backup2"};
        Write(backups[0], "valid:newest");
        const auto result = RestoreFirstValid(
            primary, backups, IsValid);
        assert(result.foundValidBackup);
        assert(result.restoredPrimary);
        assert(result.backupIndex == 0);
        assert(Read(primary) == "valid:newest");
    }
}
