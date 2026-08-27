// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/DependencyDiagnostics.hpp"

#include <array>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "BigScreen/DependencyVersion.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/Logger.hpp"
#include "rapidjson/document.h"
#include "scotland2/shared/modloader.h"

namespace BigScreen::DependencyDiagnostics {
    namespace {
        struct Requirement final {
            std::string_view id;
            std::string_view name;
            std::string_view range;
        };

        // Keep this list synchronized with mod.template.json/mod.json. The
        // repository invariant test intentionally enforces that contract so a
        // dependency update cannot leave the startup diagnosis stale.
        constexpr std::array Requirements{
            Requirement{"beatsaber-hook", "Beat Saber Hook", "^6.4.2"},
            Requirement{"songcore", "SongCore", "^1.1.23"},
            Requirement{"bsml", "BSML", "^0.4.54"},
            Requirement{"custom-types", "Custom Types", "^0.18.3"},
        };

        struct LoadedResults final {
            CModResults value{};

            LoadedResults() : value(modloader_get_loaded()) {}
            ~LoadedResults() { modloader_free_results(&value); }
            LoadedResults(const LoadedResults&) = delete;
            LoadedResults& operator=(const LoadedResults&) = delete;
        };

        const CModInfo* Find(
            const CModResults& loaded,
            std::string_view id) noexcept
        {
            for(std::size_t index = 0; index < loaded.size; ++index)
            {
                const auto& info = loaded.array[index].info;
                if(info.id && id == info.id)
                    return &info;
            }
            return nullptr;
        }

        std::optional<std::string> JsonString(
            const rapidjson::Document& document,
            const char* name)
        {
            const auto found = document.FindMember(name);
            if(found == document.MemberEnd() || !found->value.IsString())
                return std::nullopt;
            return std::string(
                found->value.GetString(),
                found->value.GetStringLength());
        }

        bool IsNewerVersion(
            std::string_view candidate,
            std::string_view current) noexcept
        {
            const auto left = ParseSemanticVersion(candidate);
            const auto right = ParseSemanticVersion(current);
            if(!left || !right)
                return false;
            if(left->major != right->major)
                return left->major > right->major;
            if(left->minor != right->minor)
                return left->minor > right->minor;
            if(left->patch != right->patch)
                return left->patch > right->patch;
            return !left->prerelease && right->prerelease;
        }

        std::optional<std::string> FindPackagedVersion(
            std::string_view id) noexcept
        {
            try
            {
                const std::filesystem::path packageRoot =
                    std::filesystem::path(
                        "/sdcard/ModData/com.beatgames.beatsaber/Packages") /
                    BIGSCREEN_GAME_VERSION;
                std::error_code iteratorError;
                std::filesystem::directory_iterator entries(
                    packageRoot,
                    std::filesystem::directory_options::skip_permission_denied,
                    iteratorError);
                if(iteratorError)
                    return std::nullopt;

                std::optional<std::string> selected;
                for(const auto& entry : entries)
                {
                    std::error_code entryError;
                    if(!entry.is_directory(entryError) || entryError)
                        continue;
                    const auto manifest = entry.path() / "mod.json";
                    if(!std::filesystem::is_regular_file(manifest, entryError) ||
                       entryError)
                        continue;
                    const auto size = std::filesystem::file_size(
                        manifest, entryError);
                    if(entryError || size > 1024u * 1024u)
                        continue;

                    std::ifstream input(manifest, std::ios::binary);
                    if(!input)
                        continue;
                    const std::string json{
                        std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>()};
                    rapidjson::Document document;
                    document.Parse(json.data(), json.size());
                    if(document.HasParseError() || !document.IsObject())
                        continue;
                    const auto manifestId = JsonString(document, "id");
                    const auto version = JsonString(document, "version");
                    if(!manifestId || !version || *manifestId != id)
                        continue;
                    if(!selected || IsNewerVersion(*version, *selected))
                        selected = *version;
                }
                return selected;
            }
            catch(...)
            {
                return std::nullopt;
            }
        }

        std::string_view MinimumVersion(std::string_view range) noexcept
        {
            if(!range.empty() && (range.front() == '^' || range.front() == '='))
                range.remove_prefix(1);
            return range;
        }
    }

    void CheckLoadedDependencies() noexcept
    {
        // Big Screen is an early mod. Its late_load callback runs when the
        // early-mod phase finishes, before Scotland2 has populated the final
        // loaded-object registry for ordinary mods such as SongCore. Wait for
        // the first stable menu update after `late_mods_opened`; otherwise a
        // healthy installation is falsely reported as five missing libraries.
        static std::atomic_bool checked{false};
        if(!late_mods_opened)
            return;
        bool expected = false;
        if(!checked.compare_exchange_strong(
               expected, true, std::memory_order_acq_rel))
            return;

        try
        {
            const LoadedResults loaded;
            std::ostringstream versions;
            std::ostringstream problems;
            std::ostringstream outdated;
            std::size_t problemCount = 0;
            std::size_t outdatedCount = 0;

            for(const auto& requirement : Requirements)
            {
                const auto packagedVersion =
                    FindPackagedVersion(requirement.id);
                const auto* loadedInfo = Find(loaded.value, requirement.id);
                // A registered mod's live Scotland2 identity is the strongest
                // evidence. Library-phase objects intentionally have no
                // identity, so only those fall back to the QMOD manifest MBF
                // or QuestPatcher installed for this Beat Saber version.
                const std::string_view installedVersion =
                    loadedInfo && loadedInfo->version &&
                    loadedInfo->version[0] != '\0'
                        ? std::string_view(loadedInfo->version)
                        : packagedVersion
                            ? std::string_view(*packagedVersion)
                            : std::string_view{};
                if(versions.tellp() > 0)
                    versions << "; ";
                versions << requirement.name << ' '
                         << (installedVersion.empty()
                                 ? "version unavailable"
                                 : installedVersion);

                if(!installedVersion.empty() &&
                   VersionSatisfiesRange(installedVersion, requirement.range))
                    continue;

                ++problemCount;
                if(problems.tellp() > 0)
                    problems << '\n';
                if(installedVersion.empty())
                {
                    // Direct source deployment does not register Big Screen as
                    // a QMOD, but it preflights these shared dependency
                    // package manifests before copying any file. If a custom
                    // development install has no readable manifest, treat the
                    // version as unavailable evidence only: never block mod
                    // startup and never present it as a proven downgrade.
                    problems << "• " << requirement.name
                             << " has no readable installed-package version "
                                "(required " << requirement.range << ").";
                }
                else
                {
                    problems << "• " << requirement.name << ' '
                             << installedVersion << " is installed, but Big "
                                "Screen requires " << requirement.range << '.';
                    if(VersionIsBelowRangeMinimum(
                           installedVersion, requirement.range))
                    {
                        ++outdatedCount;
                        if(outdated.tellp() > 0)
                            outdated << '\n';
                        outdated << "• " << requirement.name << ' '
                                 << installedVersion
                                 << " is installed. Big Screen needs compatible "
                                    "version " << MinimumVersion(requirement.range)
                                 << " or newer.";
                    }
                }
            }

            BigScreenLogger.info(
                "Startup dependency versions: {}",
                versions.str());
            if(problemCount == 0)
                return;

            BigScreenLogger.error(
                "Startup dependency check found {} problem(s): {}",
                problemCount,
                problems.str());
            if(outdatedCount > 0)
            {
                outdated << "\n\nOpen ModsBeforeFriday and update the listed "
                            "dependency before using Big Screen again.";
                ErrorManager::Instance().ReportUserVisible(
                    "Big Screen dependency update required",
                    outdated.str());
            }
        }
        catch(const std::exception& exception)
        {
            // Diagnostics are fail-open. A failure to inspect the loader must
            // never become the reason Beat Saber or Big Screen cannot start.
            BigScreenLogger.warn(
                "Could not inspect loaded dependency versions: {}",
                exception.what());
        }
        catch(...)
        {
            BigScreenLogger.warn(
                "Could not inspect loaded dependency versions");
        }
    }
}
