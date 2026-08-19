// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "BigScreen/Utility.hpp"

namespace BigScreen::DownloaderPackage {
    /// Parses yt-dlp's numeric stable/nightly tags. Unknown version formats
    /// deliberately fail closed so cleanup never removes a user-installed
    /// package whose ordering Big Screen cannot prove.
    inline std::optional<std::vector<std::uint64_t>> VersionParts(
        std::string_view text)
    {
        std::vector<std::uint64_t> parts;
        while(!text.empty())
        {
            const auto dot = text.find('.');
            const auto part = text.substr(0, dot);
            if(part.empty() || !std::all_of(
                   part.begin(), part.end(), [](unsigned char value) {
                       return value >= '0' && value <= '9';
                   }))
                return std::nullopt;
            std::uint64_t value = 0;
            const auto parsed = std::from_chars(
                part.data(), part.data() + part.size(), value);
            if(parsed.ec != std::errc{} ||
               parsed.ptr != part.data() + part.size())
                return std::nullopt;
            parts.push_back(value);
            if(dot == std::string_view::npos)
                break;
            text.remove_prefix(dot + 1);
        }
        return parts.empty()
            ? std::nullopt
            : std::optional<std::vector<std::uint64_t>>{std::move(parts)};
    }

    inline bool VersionIsOlder(
        std::string_view candidate,
        std::string_view reference)
    {
        const auto candidateParts = VersionParts(candidate);
        const auto referenceParts = VersionParts(reference);
        if(!candidateParts || !referenceParts)
            return false;
        const auto count = std::max(
            candidateParts->size(), referenceParts->size());
        for(std::size_t index = 0; index < count; ++index)
        {
            const auto left = index < candidateParts->size()
                ? (*candidateParts)[index]
                : 0;
            const auto right = index < referenceParts->size()
                ? (*referenceParts)[index]
                : 0;
            if(left != right)
                return left < right;
        }
        return false;
    }

    inline std::string NormalizeChannel(
        std::string channel,
        std::string_view version)
    {
        std::transform(channel.begin(), channel.end(), channel.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
        if(channel == "stable" || channel == "nightly")
            return channel;
        const auto parts = VersionParts(version);
        return parts && parts->size() > 3 ? "nightly" : "stable";
    }

    /// Filesystem transaction used when a downloaded yt-dlp candidate becomes
    /// active. Destruction before Accept() restores the prior active package
    /// and returns the candidate to `next`, while Reject() restores the prior
    /// package and records the rejected version so startup will not retry it.
    /// This class is Unity-free so its rollback guarantees can be exercised by
    /// host tests against real files rather than asserted as source text.
    class Activation final {
    public:
        explicit Activation(std::filesystem::path runtime)
            : runtime_(std::move(runtime)),
              next_(runtime_ / "yt-dlp-next"),
              active_(runtime_ / "yt-dlp-active"),
              previous_(runtime_ / "yt-dlp-previous") {}

        ~Activation()
        {
            if(attempted_ && !accepted_)
                RestoreForRetry();
        }

        bool Promote(std::string& error)
        {
            if(!Utility::IsRegularFile(next_))
                return true;
            attempted_ = true;
            hadActive_ = Utility::IsRegularFile(active_);
            std::ifstream version(next_.string() + ".version");
            if(version) std::getline(version, candidateVersion_);
            std::ifstream channel(next_.string() + ".channel");
            if(channel) std::getline(channel, candidateChannel_);
            candidateChannel_ = NormalizeChannel(
                std::move(candidateChannel_), candidateVersion_);

            if(!Remove(previous_, error) ||
               !Remove(previous_.string() + ".version", error) ||
               !Remove(previous_.string() + ".channel", error))
                return false;
            if(hadActive_)
            {
                if(!Rename(active_, previous_, error))
                    return false;
                originalMoved_ = true;
                if(Utility::IsRegularFile(active_.string() + ".version") &&
                   !Rename(
                       active_.string() + ".version",
                       previous_.string() + ".version",
                       error))
                    return false;
                if(Utility::IsRegularFile(active_.string() + ".channel") &&
                   !Rename(
                       active_.string() + ".channel",
                       previous_.string() + ".channel",
                       error))
                    return false;
            }
            if(!Rename(next_, active_, error))
                return false;
            candidateActive_ = true;
            if(Utility::IsRegularFile(next_.string() + ".version") &&
               !Rename(
                   next_.string() + ".version",
                   active_.string() + ".version",
                   error))
                return false;
            if(Utility::IsRegularFile(next_.string() + ".channel") &&
               !Rename(
                   next_.string() + ".channel",
                   active_.string() + ".channel",
                   error))
                return false;
            return true;
        }

        bool Promoted() const { return candidateActive_; }
        const std::string& CandidateVersion() const { return candidateVersion_; }
        const std::string& CandidateChannel() const { return candidateChannel_; }

        void Accept()
        {
            accepted_ = true;
            std::error_code ignored;
            std::filesystem::remove(
                runtime_ / "yt-dlp-rejected.version", ignored);
        }

        bool Reject(std::string& error)
        {
            if(!candidateActive_)
                return true;
            if(!Remove(active_, error) ||
               !Remove(active_.string() + ".version", error) ||
               !Remove(active_.string() + ".channel", error))
                return false;
            candidateActive_ = false;
            if(originalMoved_)
            {
                if(!Rename(previous_, active_, error))
                    return false;
                originalMoved_ = false;
                if(Utility::IsRegularFile(previous_.string() + ".version") &&
                   !Rename(
                       previous_.string() + ".version",
                       active_.string() + ".version",
                       error))
                    return false;
                if(Utility::IsRegularFile(previous_.string() + ".channel") &&
                   !Rename(
                       previous_.string() + ".channel",
                       active_.string() + ".channel",
                       error))
                    return false;
            }
            if(!WriteRejectedVersion(candidateVersion_, error))
                return false;
            accepted_ = true;
            return true;
        }

    private:
        bool Remove(const std::filesystem::path& path, std::string& error)
        {
            std::error_code fileError;
            std::filesystem::remove(path, fileError);
            if(!fileError) return true;
            error = "Could not remove " + path.filename().string() + ": " +
                    fileError.message();
            return false;
        }

        bool Rename(
            const std::filesystem::path& from,
            const std::filesystem::path& to,
            std::string& error)
        {
            std::error_code fileError;
            std::filesystem::rename(from, to, fileError);
            if(!fileError) return true;
            error = "Could not activate " + from.filename().string() + ": " +
                    fileError.message();
            return false;
        }

        bool WriteRejectedVersion(
            const std::string& version,
            std::string& error) const
        {
            std::ofstream rejected(
                runtime_ / "yt-dlp-rejected.version",
                std::ios::binary | std::ios::trunc);
            rejected << (version.empty() ? "unknown" : version);
            rejected.flush();
            if(rejected) return true;
            error = "Could not record the rejected yt-dlp version.";
            return false;
        }

        void RestoreForRetry() noexcept
        {
            std::error_code ignored;
            if(candidateActive_)
            {
                std::filesystem::remove(next_, ignored);
                ignored.clear();
                std::filesystem::rename(active_, next_, ignored);
                ignored.clear();
                if(std::filesystem::is_regular_file(
                       active_.string() + ".version", ignored))
                {
                    ignored.clear();
                    std::filesystem::remove(
                        next_.string() + ".version", ignored);
                    ignored.clear();
                    std::filesystem::rename(
                        active_.string() + ".version",
                        next_.string() + ".version",
                        ignored);
                }
                ignored.clear();
                if(std::filesystem::is_regular_file(
                       active_.string() + ".channel", ignored))
                {
                    ignored.clear();
                    std::filesystem::remove(
                        next_.string() + ".channel", ignored);
                    ignored.clear();
                    std::filesystem::rename(
                        active_.string() + ".channel",
                        next_.string() + ".channel",
                        ignored);
                }
                candidateActive_ = false;
            }
            if(originalMoved_)
            {
                ignored.clear();
                std::filesystem::remove(active_, ignored);
                ignored.clear();
                std::filesystem::rename(previous_, active_, ignored);
                ignored.clear();
                if(std::filesystem::is_regular_file(
                       previous_.string() + ".version", ignored))
                {
                    ignored.clear();
                    std::filesystem::remove(
                        active_.string() + ".version", ignored);
                    ignored.clear();
                    std::filesystem::rename(
                        previous_.string() + ".version",
                        active_.string() + ".version",
                        ignored);
                }
                ignored.clear();
                if(std::filesystem::is_regular_file(
                       previous_.string() + ".channel", ignored))
                {
                    ignored.clear();
                    std::filesystem::remove(
                        active_.string() + ".channel", ignored);
                    ignored.clear();
                    std::filesystem::rename(
                        previous_.string() + ".channel",
                        active_.string() + ".channel",
                        ignored);
                }
                originalMoved_ = false;
            }
        }

        std::filesystem::path runtime_;
        std::filesystem::path next_;
        std::filesystem::path active_;
        std::filesystem::path previous_;
        std::string candidateVersion_;
        std::string candidateChannel_;
        bool attempted_ = false;
        bool accepted_ = false;
        bool hadActive_ = false;
        bool originalMoved_ = false;
        bool candidateActive_ = false;
    };
}
