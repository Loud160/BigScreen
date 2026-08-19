// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

namespace BigScreen::RotatingBackupStore {
    struct RestoreResult {
        bool foundValidBackup = false;
        bool restoredPrimary = false;
        std::size_t backupIndex = 0;
        std::string error;
    };

    /// Loads the first valid backup through the caller's domain parser, then
    /// republishes its exact bytes through a flushed sibling path. The caller
    /// may continue with the already-loaded in-memory data even if Android
    /// shared storage refuses the final republish operation.
    template<class LoadBackup>
    RestoreResult RestoreFirstValid(
        const std::filesystem::path& primary,
        const std::array<std::filesystem::path, 2>& backups,
        LoadBackup&& loadBackup)
    {
        for(std::size_t index = 0; index < backups.size(); ++index)
        {
            if(!loadBackup(backups[index]))
                continue;

            RestoreResult result;
            result.foundValidBackup = true;
            result.backupIndex = index;
            const auto temporary = primary.string() + ".restore.tmp";
            std::error_code error;
            std::filesystem::copy_file(
                backups[index], temporary,
                std::filesystem::copy_options::overwrite_existing, error);
            if(!error)
            {
                std::error_code removeError;
                std::filesystem::remove(primary, removeError);
                if(removeError)
                    error = removeError;
                else
                    std::filesystem::rename(temporary, primary, error);
            }
            if(error)
            {
                std::error_code cleanupError;
                std::filesystem::remove(temporary, cleanupError);
                result.error = error.message();
            }
            else
            {
                result.restoredPrimary = true;
            }
            return result;
        }
        return {};
    }

    /// Rotates only files accepted by the caller's real domain validator.
    /// A corrupt primary or backup can therefore never displace a known-good
    /// recovery generation merely because it exists on disk.
    template<class IsValid, class Warn>
    void RotateKnownGood(
        const std::filesystem::path& primary,
        const std::filesystem::path& backup1,
        const std::filesystem::path& backup2,
        IsValid&& isValid,
        Warn&& warn)
    {
        std::error_code error;
        if(isValid(backup1))
        {
            std::filesystem::copy_file(
                backup1, backup2,
                std::filesystem::copy_options::overwrite_existing, error);
            if(error)
                warn(1, 2, error.message());
        }
        error.clear();
        if(isValid(primary))
        {
            std::filesystem::copy_file(
                primary, backup1,
                std::filesystem::copy_options::overwrite_existing, error);
            if(error)
                warn(0, 1, error.message());
        }
    }
}
