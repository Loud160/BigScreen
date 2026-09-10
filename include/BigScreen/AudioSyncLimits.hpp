// SPDX-License-Identifier: GPL-3.0-only
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
#pragma once
#include <cstddef>
#include <string_view>
namespace BigScreen::AudioSync
{
struct WorkingLimits
{
    std::size_t featureFrames;
    const char* family;
};
inline WorkingLimits LimitsForModel(std::string_view model)
{
    // Keep independently tunable limits, not an inference that Pro and
    // Quest 2 have equal RAM. Unknown models use the conservative profile.
    // One frame is 32 ms and ~88 bytes. Two 24k indices use about 4 MB;
    // two 32k indices about 5.4 MB, independent of full PCM track length.
    // These are initial safety budgets, not measured performance claims.
    return model.find("Quest 3") != std::string_view::npos
               ? WorkingLimits{32000, "Quest 3/3S"}
               : WorkingLimits{24000, "Quest 2/Pro or unknown"};
}
} // namespace BigScreen::AudioSync
