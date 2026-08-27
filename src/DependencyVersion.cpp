// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/DependencyVersion.hpp"

#include <charconv>
#include <compare>

namespace BigScreen {
    namespace {
        void Trim(std::string_view& value) noexcept
        {
            while(!value.empty() &&
                  (value.front() == ' ' || value.front() == '\t' ||
                   value.front() == '\r' || value.front() == '\n'))
                value.remove_prefix(1);
            while(!value.empty() &&
                  (value.back() == ' ' || value.back() == '\t' ||
                   value.back() == '\r' || value.back() == '\n'))
                value.remove_suffix(1);
        }

        bool ReadPart(
            std::string_view& remaining,
            std::uint64_t& destination,
            char separator) noexcept
        {
            const auto position = remaining.find(separator);
            if(position == std::string_view::npos || position == 0)
                return false;
            const auto part = remaining.substr(0, position);
            const auto result = std::from_chars(
                part.data(), part.data() + part.size(), destination);
            if(result.ec != std::errc{} || result.ptr != part.data() + part.size())
                return false;
            remaining.remove_prefix(position + 1);
            return true;
        }

        int Compare(
            const SemanticVersion& left,
            const SemanticVersion& right) noexcept
        {
            if(left.major != right.major)
                return left.major < right.major ? -1 : 1;
            if(left.minor != right.minor)
                return left.minor < right.minor ? -1 : 1;
            if(left.patch != right.patch)
                return left.patch < right.patch ? -1 : 1;
            if(left.prerelease == right.prerelease)
                return 0;
            return left.prerelease ? -1 : 1;
        }
    }

    std::optional<SemanticVersion> ParseSemanticVersion(
        std::string_view text) noexcept
    {
        Trim(text);
        if(!text.empty() && (text.front() == 'v' || text.front() == 'V'))
            text.remove_prefix(1);

        SemanticVersion parsed;
        if(!ReadPart(text, parsed.major, '.') ||
           !ReadPart(text, parsed.minor, '.'))
            return std::nullopt;

        const auto suffix = text.find_first_of("-+");
        const auto patch = text.substr(0, suffix);
        if(patch.empty())
            return std::nullopt;
        const auto result = std::from_chars(
            patch.data(), patch.data() + patch.size(), parsed.patch);
        if(result.ec != std::errc{} || result.ptr != patch.data() + patch.size())
            return std::nullopt;
        if(suffix != std::string_view::npos)
        {
            if(suffix + 1 >= text.size())
                return std::nullopt;
            parsed.prerelease = text[suffix] == '-';
        }
        return parsed;
    }

    bool VersionSatisfiesRange(
        std::string_view version,
        std::string_view range) noexcept
    {
        Trim(range);
        const auto candidate = ParseSemanticVersion(version);
        if(!candidate || range.empty())
            return false;

        if(range.front() == '=')
            range.remove_prefix(1);

        if(!range.empty() && range.front() == '^')
        {
            range.remove_prefix(1);
            const auto lower = ParseSemanticVersion(range);
            if(!lower || Compare(*candidate, *lower) < 0)
                return false;

            SemanticVersion upper;
            if(lower->major > 0)
                upper.major = lower->major + 1;
            else if(lower->minor > 0)
                upper.minor = lower->minor + 1;
            else
                upper.patch = lower->patch + 1;
            return Compare(*candidate, upper) < 0;
        }

        const auto exact = ParseSemanticVersion(range);
        return exact && Compare(*candidate, *exact) == 0;
    }

    bool VersionIsBelowRangeMinimum(
        std::string_view version,
        std::string_view range) noexcept
    {
        Trim(range);
        if(!range.empty() && (range.front() == '^' || range.front() == '='))
            range.remove_prefix(1);
        const auto candidate = ParseSemanticVersion(version);
        const auto minimum = ParseSemanticVersion(range);
        return candidate && minimum && Compare(*candidate, *minimum) < 0;
    }
}
