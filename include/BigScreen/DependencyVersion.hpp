// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace BigScreen {
    struct SemanticVersion final {
        std::uint64_t major = 0;
        std::uint64_t minor = 0;
        std::uint64_t patch = 0;
        bool prerelease = false;
    };

    /// Parses the SemVer subset used by Quest package manifests. A leading
    /// `v`, prerelease suffix, and build metadata are accepted.
    std::optional<SemanticVersion> ParseSemanticVersion(
        std::string_view text) noexcept;

    /// Evaluates the exact and caret ranges emitted by Big Screen's QMOD.
    /// Unknown syntax fails closed so diagnostics never call an unverified
    /// dependency compatible.
    bool VersionSatisfiesRange(
        std::string_view version,
        std::string_view range) noexcept;

    /// Returns true only when both values parse and the installed version is
    /// lower than the range's declared lower bound. Missing, malformed, and
    /// future-major versions must not be presented as an "outdated" popup.
    bool VersionIsBelowRangeMinimum(
        std::string_view version,
        std::string_view range) noexcept;
}
