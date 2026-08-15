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
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

namespace BigScreen::Utility {
    /// Component-wise containment check for user-authored and persisted paths.
    /// The error-code overloads keep malformed or unavailable paths from
    /// throwing through a Beat Saber hook or menu callback.
    inline bool IsPathInside(
        const std::filesystem::path& child,
        const std::filesystem::path& parent)
    {
        std::error_code error;
        const auto normalizedChild =
            std::filesystem::absolute(child, error).lexically_normal();
        if(error)
            return false;
        const auto normalizedParent =
            std::filesystem::absolute(parent, error).lexically_normal();
        if(error)
            return false;

        auto childPart = normalizedChild.begin();
        for(auto parentPart = normalizedParent.begin();
            parentPart != normalizedParent.end();
            ++parentPart, ++childPart)
        {
            if(childPart == normalizedChild.end() || *childPart != *parentPart)
                return false;
        }
        return true;
    }

    /// File existence checks on Quest shared storage must not throw when a
    /// user removes media, disconnects MTP, or Android temporarily denies a
    /// path between listing and selection.
    inline bool IsRegularFile(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) && !error;
    }

    inline std::string FormatMegabytes(std::uint64_t bytes, int precision = 1)
    {
        std::ostringstream text;
        text << std::fixed << std::setprecision(precision)
             << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MB";
        return text.str();
    }

    inline std::string FormatStorageSize(
        std::uint64_t bytes,
        int megabytePrecision = 1,
        int gigabytePrecision = 1)
    {
        constexpr std::uint64_t Gibibyte = 1024ULL * 1024ULL * 1024ULL;
        if(bytes < Gibibyte)
            return FormatMegabytes(bytes, megabytePrecision);

        std::ostringstream text;
        text << std::fixed << std::setprecision(gigabytePrecision)
             << static_cast<double>(bytes) / static_cast<double>(Gibibyte)
             << " GB";
        return text.str();
    }
}
