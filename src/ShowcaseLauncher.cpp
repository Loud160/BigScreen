// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/ShowcaseLauncher.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/ShowcaseMenu.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/LevelCollectionNavigationController.hpp"
#include "GlobalNamespace/LevelSelectionFlowCoordinator.hpp"
#include "GlobalNamespace/LevelSelectionNavigationController.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/MainMenuViewController.hpp"
#include "GlobalNamespace/ResultsViewController.hpp"
#include "GlobalNamespace/SelectLevelCategoryViewController.hpp"
#include "GlobalNamespace/SoloFreePlayFlowCoordinator.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "GlobalNamespace/StandardLevelDetailViewController.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "songcore/shared/SongCore.hpp"
#include "UnityEngine/GameObject.hpp"
#include "System/Nullable_1.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr std::string_view ShowcaseMapKey = "11cf8";
        constexpr std::string_view ShowcaseHash =
            "2aa85aad10e124eb674d18d49251bc94ee1a4283";
        constexpr std::string_view ShowcaseFolder =
            "11cf8 (Up & Down - The Good Boi)";
        constexpr std::string_view ShowcaseVideoUrl =
            "https://youtu.be/oJa7Kr7_9dw";

        GlobalNamespace::BeatmapLevel* FindShowcaseLevel()
        {
            return SongCore::API::Loading::GetLevelByHash(ShowcaseHash);
        }

        std::filesystem::path ShowcaseRoot()
        {
            return VideoLibrary::Instance().RootPath() / "DemoLevels";
        }

        std::filesystem::path ShowcaseDirectory()
        {
            return ShowcaseRoot() / ShowcaseFolder;
        }

        bool CapabilityAvailable(std::string_view name)
        {
            return SongCore::API::Capabilities::IsCapabilityRegistered(name);
        }
    }

    ShowcaseLauncher& ShowcaseLauncher::Instance()
    {
        static ShowcaseLauncher launcher;
        return launcher;
    }

    bool ShowcaseLauncher::CheckRequirements(std::string& explanation) const
    {
        const bool chroma = CapabilityAvailable("Chroma");
        // Current Noodle Extensions registers the spaced capability used by
        // map requirements. Accept the legacy compact spelling as well so a
        // compatible older Quest build is not falsely reported as missing.
        const bool noodle =
            CapabilityAvailable("Noodle Extensions") ||
            CapabilityAvailable("NoodleExtensions");
        if(chroma && noodle)
            return true;

        std::string missing;
        if(!chroma) missing = "Chroma";
        if(!noodle)
        {
            if(!missing.empty()) missing += " and ";
            missing += "Noodle Extensions";
        }
        explanation =
            missing +
            " must be installed and active before the Big Screen showcase can run. "
            "Install or enable the missing mod, restart Beat Saber, and try again.";
        return false;
    }

    ShowcaseReadiness ShowcaseLauncher::Readiness() const
    {
        ShowcaseReadiness result;
        result.chromaActive = CapabilityAvailable("Chroma");
        result.noodleActive =
            CapabilityAvailable("Noodle Extensions") ||
            CapabilityAvailable("NoodleExtensions");
        std::error_code fileError;
        result.mapFilesPresent =
            std::filesystem::exists(ShowcaseDirectory(), fileError) &&
            !fileError;
        if(auto* level = FindShowcaseLevel())
        {
            result.mapReady = true;
            result.videoReady =
                VideoLibrary::Instance().Describe(level).CanPlay();
        }
        result.downloaderReady = DownloadManager::Instance().IsReady();
        if(!result.downloaderReady)
            result.downloaderMessage =
                DownloadManager::Instance().UnavailableMessage();
        return result;
    }

    bool ShowcaseLauncher::DownloadMap(std::string& error)
    {
        if(state_ != ShowcaseLaunchState::Idle)
        {
            error = "The Big Screen showcase is already being prepared.";
            return false;
        }
        VideoLibraryMenu::Instance().StopActivePreview();
        if(FindShowcaseLevel())
        {
            error = "The showcase map is already installed and loaded.";
            return false;
        }

        if(!DownloadManager::Instance().IsReady())
        {
            error = DownloadManager::Instance().UnavailableMessage();
            return false;
        }

        std::error_code fileError;
        std::filesystem::create_directories(ShowcaseRoot(), fileError);
        if(fileError)
        {
            error = "Big Screen could not create its managed showcase map folder: " +
                fileError.message();
            return false;
        }
        // Register the root before refreshing so this and later sessions use
        // the same SongCore-supported custom-level discovery path.
        SongCore::API::Loading::AddLevelPath(ShowcaseRoot(), false);
        MapPackageRequest request;
        request.mapKey = std::string(ShowcaseMapKey);
        request.expectedHash = std::string(ShowcaseHash);
        request.destinationDirectory = ShowcaseDirectory();
        if(!DownloadManager::Instance().StartMapPackage(
               std::move(request), error))
            return false;
        SetState(
            ShowcaseLaunchState::DownloadingMap,
            "Downloading showcase map");
        return true;
    }

    bool ShowcaseLauncher::RecheckMap(std::string& error)
    {
        if(state_ != ShowcaseLaunchState::Idle)
        {
            error = "The Big Screen showcase is already being prepared.";
            return false;
        }
        if(FindShowcaseLevel())
            return true;
        std::error_code fileError;
        if(!std::filesystem::exists(ShowcaseDirectory(), fileError) || fileError)
        {
            error = "The showcase map files are not present. Use Download Map first.";
            return false;
        }

        SongCore::API::Loading::AddLevelPath(ShowcaseRoot(), false);
        songRefresh_ = SongCore::API::Loading::RefreshSongs(false);
        if(!songRefresh_.valid())
        {
            error =
                "SongCore was not ready to recheck the showcase map. Restart Beat Saber and try again.";
            return false;
        }
        SetState(
            ShowcaseLaunchState::RefreshingSongs,
            "Rechecking showcase map");
        return true;
    }

    bool ShowcaseLauncher::DownloadVideo(std::string& error)
    {
        if(state_ != ShowcaseLaunchState::Idle)
        {
            error = "The Big Screen showcase is already being prepared.";
            return false;
        }
        auto* level = FindShowcaseLevel();
        if(!level)
        {
            error = "Install and load the showcase map before downloading its video.";
            return false;
        }
        if(VideoLibrary::Instance().Describe(level).CanPlay())
        {
            error = "The showcase video is already downloaded and playable.";
            return false;
        }
        if(!DownloadManager::Instance().IsReady())
        {
            error = DownloadManager::Instance().UnavailableMessage();
            return false;
        }

        VideoLibraryMenu::Instance().StopActivePreview();
        DownloadRequest request;
        request.levelId = std::string(level->levelID);
        request.songName = level->songName
            ? std::string(level->songName)
            : "Up & Down";
        request.songAuthor = level->songAuthorName
            ? std::string(level->songAuthorName)
            : "Marnik";
        request.sourceUrl = std::string(ShowcaseVideoUrl);
        request.origin = VideoOrigin::Mapper;
        request.explicitContentAllowed = false;
        request.offsetSeconds = -2.5;
        request.playbackRate = 1.0;
        request.fitToSong = false;
        request.blackDuringLeadIn = true;
        request.requestedHeight = 1080;
        request.maximumSourceFps = Settings::Instance().PlaybackFpsLimit();
        if(!DownloadManager::Instance().Start(std::move(request), error))
            return false;
        SetState(
            ShowcaseLaunchState::DownloadingVideo,
            "Downloading showcase video");
        return true;
    }

    bool ShowcaseLauncher::Play(std::string& error)
    {
        if(state_ != ShowcaseLaunchState::Idle)
        {
            error = "Wait for showcase preparation to finish before playing.";
            return false;
        }
        const auto readiness = Readiness();
        if(!readiness.chromaActive || !readiness.noodleActive)
            return CheckRequirements(error);
        if(!readiness.mapReady)
        {
            error = "The showcase map is not ready. Download or recheck it first.";
            return false;
        }
        if(!readiness.videoReady)
        {
            error = "The showcase video is not ready. Download it first.";
            return false;
        }
        BeginMenuDismissal();
        return state_ != ShowcaseLaunchState::Idle;
    }

    void ShowcaseLauncher::Tick()
    {
        switch(state_)
        {
            case ShowcaseLaunchState::Idle:
                return;
            case ShowcaseLaunchState::DownloadingMap:
            {
                const auto download = DownloadManager::Instance().Snapshot();
                if(download.levelId != "__showcase_map__")
                    return;
                message_ = download.message;
                if(download.Active())
                    return;
                if(download.state != DownloadState::Completed)
                {
                    Fail(
                        "Showcase map download failed",
                        download.message.empty()
                            ? "Big Screen could not install the showcase map."
                            : download.message);
                    return;
                }
                SongCore::API::Loading::AddLevelPath(ShowcaseRoot(), false);
                songRefresh_ = SongCore::API::Loading::RefreshSongs(false);
                if(!songRefresh_.valid())
                {
                    Fail(
                        "Showcase map unavailable",
                        "SongCore was not ready to load the installed showcase map. Restart Beat Saber and try again.");
                    return;
                }
                SetState(
                    ShowcaseLaunchState::RefreshingSongs,
                    "Adding showcase map to the song library");
                return;
            }
            case ShowcaseLaunchState::RefreshingSongs:
                if(songRefresh_.wait_for(std::chrono::seconds(0)) !=
                   std::future_status::ready)
                    return;
                try
                {
                    songRefresh_.get();
                }
                catch(const std::exception& exception)
                {
                    Fail(
                        "Showcase map unavailable",
                        std::string("SongCore could not load the showcase map: ") +
                            exception.what());
                    return;
                }
                if(!FindShowcaseLevel())
                {
                    Fail(
                        "Showcase map unavailable",
                        "SongCore loaded songs but did not recognize the exact showcase revision. See Big Screen's error log for details.");
                    return;
                }
                SetState(
                    ShowcaseLaunchState::Idle,
                    "Showcase map ready");
                return;
            case ShowcaseLaunchState::DownloadingVideo:
            {
                auto* level = FindShowcaseLevel();
                if(!level)
                {
                    Fail(
                        "Showcase map unavailable",
                        "The showcase map left SongCore's library while its video was downloading.");
                    return;
                }
                const auto download = DownloadManager::Instance().Snapshot();
                if(download.levelId != std::string(level->levelID))
                    return;
                message_ = download.message;
                if(download.Active())
                    return;
                if(download.state != DownloadState::Completed ||
                   !VideoLibrary::Instance().Describe(level).CanPlay())
                {
                    Fail(
                        "Showcase video download failed",
                        download.message.empty()
                            ? "The map is installed, but its video could not be prepared."
                            : download.message);
                    return;
                }
                SetState(
                    ShowcaseLaunchState::Idle,
                    "Showcase video ready");
                return;
            }
            case ShowcaseLaunchState::DismissingBigScreen:
                if(IsBigScreenMenuActive())
                {
                    transitionFrames_ = 0;
                    return;
                }
                // A dismissed child flow disappears before MainFlowCoordinator
                // and MainMenuViewController finish their own transition. The
                // previous four-frame delay raced that handoff and caused the
                // first launch to be queued until the player manually opened
                // Solo. Require the same stable parent conditions used by the
                // menu re-entry guard before presenting another child flow.
                if(auto* mainFlow = BSML::Helpers::GetMainFlowCoordinator())
                {
                    auto* mainMenu = mainFlow
                        ->__cordl_internal_get__mainMenuViewController().ptr();
                    const bool stable = mainMenu &&
                        mainFlow->get_isActivated() &&
                        !mainFlow->get_isInTransition() &&
                        mainMenu->get_isActivated() &&
                        !mainMenu->get_isInTransition() &&
                        mainMenu->get_gameObject() &&
                        mainMenu->get_gameObject()->get_activeInHierarchy();
                    transitionFrames_ = stable ? transitionFrames_ + 1 : 0;
                }
                else
                    transitionFrames_ = 0;
                if(transitionFrames_ < 12)
                    return;
                PresentSoloFlow();
                return;
            case ShowcaseLaunchState::PresentingSolo:
                // Presentation begins synchronously; one additional frame is
                // enough to let HMUI attach the solo coordinator before the
                // selection readiness checks below begin.
                if(++transitionFrames_ < 2)
                    return;
                SetState(
                    ShowcaseLaunchState::WaitingForSelection,
                    "Opening Lawless Expert+");
                return;
            case ShowcaseLaunchState::WaitingForSelection:
                TryStartSelectedLevel();
                return;
        }
    }

    ShowcaseLaunchSnapshot ShowcaseLauncher::Snapshot() const
    {
        return {state_, message_};
    }

    void ShowcaseLauncher::BeginMenuDismissal()
    {
        VideoLibraryMenu::Instance().StopActivePreview();
        ShowcaseMenu::Instance().DismissTransientUi();
        if(!ExitBigScreenMenuForShowcase())
        {
            Fail(
                "Could not start showcase",
                "Big Screen could not safely close its menu before gameplay.");
            return;
        }
        transitionFrames_ = 0;
        SetState(
            ShowcaseLaunchState::DismissingBigScreen,
            "Opening showcase");
    }

    void ShowcaseLauncher::PresentSoloFlow()
    {
        auto* level = FindShowcaseLevel();
        auto* pack = SongCore::API::Loading::GetCustomLevelPack();
        auto* mainFlow = BSML::Helpers::GetMainFlowCoordinator();
        auto* solo = mainFlow
            ? mainFlow->__cordl_internal_get__soloFreePlayFlowCoordinator().ptr()
            : nullptr;
        if(!level || !pack || !mainFlow || !solo)
        {
            Fail(
                "Could not start showcase",
                "Beat Saber's Solo menu or the SongCore custom-level pack was unavailable.");
            return;
        }

        auto* lawless =
            SongCore::API::Characteristics::GetCharacteristicBySerializedName(
                "Lawless");
        if(!lawless || !level->GetDifficultyBeatmapData(
               lawless, GlobalNamespace::BeatmapDifficulty::ExpertPlus))
        {
            Fail(
                "Showcase difficulty unavailable",
                "The installed map does not expose its required Lawless Expert+ difficulty.");
            return;
        }

        // The two-argument start state selected only the map. Beat Saber then
        // waited for a later song-list selection callback before its detail
        // controller received a BeatmapKey, which is why the first Play click
        // merely opened Solo and clicking the track unexpectedly launched it.
        // Supply the complete Lawless Expert+ key in the start state so the
        // flow owns all information required for a direct transition.
        GlobalNamespace::BeatmapKey key;
        key._ctor(
            level->levelID,
            lawless,
            GlobalNamespace::BeatmapDifficulty::ExpertPlus);
        System::Nullable_1<
            GlobalNamespace::SelectLevelCategoryViewController_LevelCategory>
            noCategory{};
        solo->Setup(
            GlobalNamespace::LevelSelectionFlowCoordinator_State::New_ctor(
                noCategory,
                pack,
                byref(key),
                level));
        mainFlow->PresentFlowCoordinatorOrAskForTutorial(solo);
        transitionFrames_ = 0;
        selectionWaitFrames_ = 0;
        levelSelectionRequested_ = false;
        SetState(
            ShowcaseLaunchState::PresentingSolo,
            "Selecting Lawless Expert+");
    }

    void ShowcaseLauncher::TryStartSelectedLevel()
    {
        auto* level = FindShowcaseLevel();
        auto* mainFlow = BSML::Helpers::GetMainFlowCoordinator();
        auto* solo = mainFlow
            ? mainFlow->__cordl_internal_get__soloFreePlayFlowCoordinator().ptr()
            : nullptr;
        if(!level || !solo)
        {
            Fail(
                "Could not start showcase",
                "The selected showcase level or Solo menu became unavailable.");
            return;
        }
        if(++selectionWaitFrames_ > 3600)
        {
            Fail(
                "Could not start showcase",
                "Beat Saber's Solo menu did not finish preparing the showcase within one minute.");
            return;
        }
        if(!solo->get_isActivated() || solo->get_isInTransition())
            return;

        auto* lawless =
            SongCore::API::Characteristics::GetCharacteristicBySerializedName(
                "Lawless");
        if(!lawless || !level->GetDifficultyBeatmapData(
               lawless, GlobalNamespace::BeatmapDifficulty::ExpertPlus))
        {
            Fail(
                "Showcase difficulty unavailable",
                "The installed map does not expose its required Lawless Expert+ difficulty.");
            return;
        }

        auto* navigation =
            solo->__cordl_internal_get_levelSelectionNavigationController().ptr();
        if(!navigation)
            return;
        auto* collection = navigation
            ->__cordl_internal_get__levelCollectionNavigationController().ptr();
        if(!collection)
            return;
        if(navigation->get_beatmapLevel() != level)
        {
            auto* collectionView = collection
                ->__cordl_internal_get__levelCollectionViewController().ptr();
            const bool collectionReady = collectionView &&
                collection->get_isActivated() &&
                !collection->get_isInTransition() &&
                !collection->__cordl_internal_get__loading();

            // LevelCollectionNavigationController::SelectLevel is a
            // pre-presentation request and is ignored after the collection is
            // already visible. That left the showcase waiting until the user
            // physically clicked the track. Once the native collection is
            // stable, invoke the exact callback that a real row selection
            // sends to the navigation controller. Retry at a restrained
            // interval because SongCore may replace its table data during the
            // first few presentation frames.
            if(collectionReady &&
               (!levelSelectionRequested_ || selectionWaitFrames_ % 30 == 0))
            {
                levelSelectionRequested_ = true;
                collection->HandleLevelCollectionViewControllerDidSelectLevel(
                    collectionView,
                    level);
                PaperLogger.info(
                    "Applied native showcase row selection while Solo finished presenting");
            }
            return;
        }
        auto* detailController = collection
            ? collection->__cordl_internal_get__levelDetailViewController().ptr()
            : nullptr;
        auto* detailView = detailController
            ? detailController->__cordl_internal_get__standardLevelDetailView().ptr()
            : nullptr;
        if(!detailView)
            return;

        GlobalNamespace::BeatmapKey key;
        key._ctor(
            level->levelID,
            lawless,
            GlobalNamespace::BeatmapDifficulty::ExpertPlus);
        detailView->set_beatmapKey(key);
        PaperLogger.info(
            "Starting managed Up & Down showcase on Lawless Expert+");

        // Return to idle before changing scenes. If StartLevel throws, the
        // guarded caller records the exception and the button is available for
        // a retry instead of remaining permanently stuck in an active state.
        showcaseGameplayActive_ = true;
        SetState(ShowcaseLaunchState::Idle, {});
        solo->StartLevel(nullptr, false);
    }

    void ShowcaseLauncher::OnGameplayFinished() noexcept
    {
        // The showcase launcher owns only the transition into gameplay. Once
        // the map ends or is quit, Beat Saber owns navigation again. Clearing
        // both fields here prevents a later visit to the readiness page from
        // inheriting a stale "returning" state.
        showcaseGameplayActive_ = false;
        transitionFrames_ = 0;
        selectionWaitFrames_ = 0;
        levelSelectionRequested_ = false;
        SetState(ShowcaseLaunchState::Idle, {});
    }

    void ShowcaseLauncher::Fail(std::string title, std::string detail)
    {
        songRefresh_ = {};
        transitionFrames_ = 0;
        selectionWaitFrames_ = 0;
        levelSelectionRequested_ = false;
        SetState(ShowcaseLaunchState::Idle, {});
        // ReportUserVisible records the handled failure as well as queueing a
        // single dialog. Do not record it a second time here: content/network
        // failures are expected and must never resemble repeated internal
        // crashes in the diagnostic history.
        ErrorManager::Instance().ReportUserVisible(
            std::move(title), std::move(detail));
        // While Big Screen is still visible, consume the queued message into
        // its retained modal immediately. Once dismissal has started, leave it
        // queued for ErrorManager's normal main-menu dialog instead.
        if(IsBigScreenMenuActive())
            SettingsMenu::Instance().RefreshDownloaderStatus();
    }

    void ShowcaseLauncher::SetState(
        ShowcaseLaunchState state,
        std::string message)
    {
        state_ = state;
        message_ = std::move(message);
    }
}
