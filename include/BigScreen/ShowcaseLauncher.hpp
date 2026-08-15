// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <future>
#include <string>

namespace BigScreen {
    enum class ShowcaseLaunchState {
        Idle,
        DownloadingMap,
        RefreshingSongs,
        DownloadingVideo,
        DismissingBigScreen,
        PresentingSolo,
        WaitingForSelection
    };

    struct ShowcaseReadiness {
        bool chromaActive = false;
        bool noodleActive = false;
        bool mapReady = false;
        bool mapFilesPresent = false;
        bool videoReady = false;
        bool downloaderReady = false;
        std::string downloaderMessage;

        bool ReadyToPlay() const {
            return chromaActive && noodleActive && mapReady && videoReady;
        }
    };

    struct ShowcaseLaunchSnapshot {
        ShowcaseLaunchState state = ShowcaseLaunchState::Idle;
        std::string message;

        bool Active() const { return state != ShowcaseLaunchState::Idle; }
    };

    /// Installs and opens Big Screen's optional Up & Down demonstration.
    /// Every method is called on Unity's main thread; only DownloadManager and
    /// SongCore perform their own background filesystem/loading work.
    class ShowcaseLauncher final {
    public:
        static ShowcaseLauncher& Instance();

        /// Checks runtime capabilities, not merely filenames on storage. A mod
        /// that failed to load must be reported just like a missing mod.
        bool CheckRequirements(std::string& explanation) const;
        /// Reports readiness without starting downloads or changing menus.
        ShowcaseReadiness Readiness() const;
        /// Asset actions are deliberately separate. Opening the showcase page
        /// must never start a network operation without another user click.
        bool DownloadMap(std::string& error);
        bool RecheckMap(std::string& error);
        bool DownloadVideo(std::string& error);
        /// Starts gameplay only after every required asset and capability has
        /// already been verified by the readiness page.
        bool Play(std::string& error);
        void Tick();
        ShowcaseLaunchSnapshot Snapshot() const;

        /// Set immediately before the managed Lawless chart starts. It is
        /// cleared as soon as gameplay finishes; Beat Saber's ordinary result
        /// and navigation controls remain completely untouched.
        bool ShowcaseGameplayActive() const { return showcaseGameplayActive_; }
        void OnGameplayFinished() noexcept;

    private:
        ShowcaseLauncher() = default;

        void BeginMenuDismissal();
        void PresentSoloFlow();
        void TryStartSelectedLevel();
        void Fail(std::string title, std::string detail);
        void SetState(ShowcaseLaunchState state, std::string message);

        ShowcaseLaunchState state_ = ShowcaseLaunchState::Idle;
        std::string message_;
        std::shared_future<void> songRefresh_;
        int transitionFrames_ = 0;
        int selectionWaitFrames_ = 0;
        bool levelSelectionRequested_ = false;
        bool showcaseGameplayActive_ = false;
    };
}
