// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <utility>

#include "BigScreen/DownloadManager.hpp"

namespace BigScreen {
    /// Builds the transport request shared by the Video Library editor and the
    /// Solo/Campaign song-selection shortcut. Each UI still owns its dialogs
    /// and progress presentation; format, timing, FPS, and content policy come
    /// from this one construction path.
    inline DownloadRequest MakeVideoDownloadRequest(
        std::string levelId,
        std::string songName,
        std::string songAuthor,
        std::string sourceUrl,
        VideoOrigin origin,
        bool explicitContentAllowed,
        const MapVideoConfig* timing,
        int requestedHeight,
        int maximumSourceFps)
    {
        DownloadRequest request;
        request.levelId = std::move(levelId);
        request.songName = std::move(songName);
        request.songAuthor = std::move(songAuthor);
        request.sourceUrl = std::move(sourceUrl);
        request.origin = origin;
        request.explicitContentAllowed = explicitContentAllowed;
        request.requestedHeight = requestedHeight;
        request.maximumSourceFps = maximumSourceFps;
        if(timing)
        {
            request.offsetSeconds = timing->offsetSeconds;
            request.playbackRate = timing->playbackRate;
            request.fitToSong = timing->fitToSong;
            request.blackDuringLeadIn = timing->blackDuringLeadIn;
        }
        return request;
    }
}
