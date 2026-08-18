// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/CinemaInterop.hpp"

#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/Settings.hpp"

extern "C" bool bigscreen_cinema_presentation_active()
{
    return BigScreen::Settings::Instance().ModEnabled() &&
        BigScreen::PlaybackSession::Instance().HasPreparedPresentation();
}

extern "C" bool bigscreen_cinema_allows_custom_platform()
{
    const auto& settings = BigScreen::Settings::Instance();
    const auto& playback = BigScreen::PlaybackSession::Instance();
    // When mapper presentation is disabled, Big Screen must not forward the
    // mapper's platform preference to another mod. In that mode the user's
    // own screen and environment choices are authoritative.
    if(!settings.ModEnabled() || !settings.RespectMapperSettings() ||
       !playback.HasPreparedPresentation())
        return true;

    const auto& config = playback.PreparedBaseConfig();
    return !config || !config->allowCustomPlatform.has_value() ||
        *config->allowCustomPlatform;
}
