#include "main.hpp"

#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/PlaybackSession.hpp"
#include "BigScreen/SelectionVideoToggle.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/SettingsMenu.hpp"
#include "BigScreen/VideoLibrary.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BasicBeatmapEventData.hpp"
#include "GlobalNamespace/EnvironmentInfoSO.hpp"
#include "GlobalNamespace/EnvironmentEffectsFilterPreset.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/Rotate.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "GlobalNamespace/StandardLevelDetailView.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/TrackLaneRingsPositionStepEffectSpawner.hpp"
#include "GlobalNamespace/TrackLaneRingsRotationEffect.hpp"
#include "GlobalNamespace/TrackLaneRingsRotationEffectSpawner.hpp"
#include "System/Nullable_1.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/Object.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "custom-types/shared/register.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace {
    void HandleLevelWasSelected(
        SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& eventArgs)
    {
        if(!BigScreen::Settings::Instance().ModEnabled())
            return;
        BigScreen::SelectionVideoToggle::Instance().LevelSelected(
            eventArgs.levelID,
            eventArgs.beatmapLevel);
    }
}

bool IsMenuPreviewEnabled()
{
    const auto& settings = BigScreen::Settings::Instance();
    return settings.ModEnabled() && settings.MenuPreviewEnabled();
}

namespace {
    template<typename T>
    int DisableLoadedComponents()
    {
        int disabled = 0;
        for(auto* component : UnityEngine::Object::FindObjectsOfType<T*>(false))
        {
            if(component && component->get_enabled())
            {
                component->set_enabled(false);
                ++disabled;
            }
        }
        return disabled;
    }

    void DisableEnvironmentMotion()
    {
        // These are scenery-only components used by Beat Saber's tunnel rings
        // and continuously rotating background props. Gameplay spawn rotation,
        // note movement, sabers, and cameras use different component types and
        // are deliberately left untouched.
        int disabled = 0;
        disabled += DisableLoadedComponents<GlobalNamespace::TrackLaneRingsRotationEffect>();
        disabled += DisableLoadedComponents<GlobalNamespace::Rotate>();
        PaperLogger.info("Disabled {} rotating or moving environment components", disabled);
    }

    bool ShouldSuppressEnvironmentMotion()
    {
        return BigScreen::Settings::Instance().ModEnabled() &&
               BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
               !BigScreen::Settings::Instance().EnvironmentMotionEnabled();
    }

    MAKE_HOOK_MATCH(
        TrackLaneRingsRotationEffectSpawner_HandleBeatmapEvent,
        &GlobalNamespace::TrackLaneRingsRotationEffectSpawner::HandleBeatmapEvent,
        void,
        GlobalNamespace::TrackLaneRingsRotationEffectSpawner* self,
        GlobalNamespace::BasicBeatmapEventData* eventData)
    {
        // Beat map callbacks are ordinary delegates and can invoke a disabled
        // MonoBehaviour. Suppress the callback itself so no new ring rotation
        // effects accumulate while the visual effect component is frozen.
        if(ShouldSuppressEnvironmentMotion())
            return;
        TrackLaneRingsRotationEffectSpawner_HandleBeatmapEvent(self, eventData);
    }

    MAKE_HOOK_MATCH(
        TrackLaneRingsPositionStepEffectSpawner_HandleBeatmapEvent,
        &GlobalNamespace::TrackLaneRingsPositionStepEffectSpawner::HandleBeatmapEvent,
        void,
        GlobalNamespace::TrackLaneRingsPositionStepEffectSpawner* self,
        GlobalNamespace::BasicBeatmapEventData* eventData)
    {
        // Position-step callbacks move the same tunnel scenery directly, so
        // component.enabled alone would not stop them once they subscribed.
        if(ShouldSuppressEnvironmentMotion())
            return;
        TrackLaneRingsPositionStepEffectSpawner_HandleBeatmapEvent(self, eventData);
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_Awake,
        &GlobalNamespace::StandardLevelDetailView::Awake,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        StandardLevelDetailView_Awake(self);
        BigScreen::SelectionVideoToggle::Instance().CreateUi(self);
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_OnEnable,
        &GlobalNamespace::StandardLevelDetailView::OnEnable,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        // Let Beat Saber restore its audio/selection subscriptions first, then
        // resume the retained preview only if the user's global settings allow
        // it. Initial activation is harmless because no map is prepared yet.
        StandardLevelDetailView_OnEnable(self);
        BigScreen::SelectionVideoToggle::Instance().SongSelectionShown();
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_OnDisable,
        &GlobalNamespace::StandardLevelDetailView::OnDisable,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        // OnDisable is the reliable boundary for leaving song selection. The
        // view is commonly kept alive while Beat Saber shows its home screen
        // or a mod flow, so waiting for OnDestroy can leave the world-space
        // video surface and decoder running outside the song browser.
        BigScreen::SelectionVideoToggle::Instance().SongSelectionHidden();
        StandardLevelDetailView_OnDisable(self);
    }

    MAKE_HOOK_MATCH(
        StandardLevelDetailView_OnDestroy,
        &GlobalNamespace::StandardLevelDetailView::OnDestroy,
        void,
        GlobalNamespace::StandardLevelDetailView* self)
    {
        // OnDisable normally runs first, but repeat the context-guarded cleanup
        // here as a defensive fallback for scene teardown ordering changes.
        BigScreen::SelectionVideoToggle::Instance().SongSelectionHidden();
        BigScreen::SelectionVideoToggle::Instance().ForgetUi();
        StandardLevelDetailView_OnDestroy(self);
    }

    MAKE_HOOK_MATCH(
        StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo,
        &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::InitEnvironmentInfo,
        void,
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::OverrideEnvironmentSettings* overrideEnvironmentSettings,
        GlobalNamespace::EnvironmentsListModel* environmentsListModel)
    {
        if(!BigScreen::Settings::Instance().ModEnabled())
        {
            StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo(
                self,
                overrideEnvironmentSettings,
                environmentsListModel);
            return;
        }

        // Init has already stored the chosen BeatmapLevel by the time it calls
        // this helper. Preparing here is early enough to influence environment
        // selection but late enough to avoid hooking both overloaded Init APIs.
        auto& playback = BigScreen::PlaybackSession::Instance();
        if(BigScreen::SelectionVideoToggle::Instance().IsEnabledForSelectedLevel())
            playback.Prepare(self->get_beatmapLevel());
        else
            playback.Prepare(nullptr);
        StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo(
            self,
            overrideEnvironmentSettings,
            environmentsListModel);

        const auto& requested = playback.RequestedEnvironment();
        if(!BigScreen::Settings::Instance().EnvironmentOverrideEnabled())
        {
            if(requested)
                PaperLogger.info("Map environment override suppressed by user setting");
            return;
        }

        if(!requested || !environmentsListModel)
            return;

        // The safe lookup cannot throw for an unknown mapper-authored name. It
        // may return a fallback, so verify the serialized name before applying
        // it; a typo must never silently replace the player's environment.
        auto environment = environmentsListModel->GetEnvironmentInfoBySerializedNameSafe(*requested);
        if(environment && std::string(environment->get_serializedName()) == *requested)
        {
            self->set_environmentInfo(environment);
            self->set_usingOverrideEnvironment(true);
            PaperLogger.info("Using map-requested environment '{}'", *requested);
        }
        else
        {
            PaperLogger.warn("Map requested unavailable environment '{}'", *requested);
        }
    }

    MAKE_HOOK_MATCH(
        StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes,
        &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::InitAndSetupScenes,
        void,
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::PlayerSpecificSettings* playerSpecificSettings,
        StringW backButtonText,
        bool startPaused)
    {
        if(!BigScreen::Settings::Instance().ModEnabled())
        {
            StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes(
                self,
                playerSpecificSettings,
                backButtonText,
                startPaused);
            return;
        }

        auto* effectiveSettings = playerSpecificSettings;
        if(BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
           !BigScreen::Settings::Instance().MapLightShowEnabled() &&
           playerSpecificSettings)
        {
            // CopyWith preserves every player preference while replacing only
            // the two difficulty-dependent environment filters. Passing a copy
            // avoids mutating Beat Saber's saved setting or affecting non-video
            // songs after this scene transition.
            using OptionalEffects =
                System::Nullable_1<GlobalNamespace::EnvironmentEffectsFilterPreset>;
            const OptionalEffects noEffects{
                true,
                GlobalNamespace::EnvironmentEffectsFilterPreset::NoEffects
            };
            effectiveSettings = playerSpecificSettings->CopyWith(
                {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
                {}, {}, {}, {}, {}, {}, {}, noEffects, noEffects, {});
            PaperLogger.info("Map light show disabled for this video level");
        }

        StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes(
            self,
            effectiveSettings,
            backButtonText,
            startPaused);
    }

    MAKE_HOOK_MATCH(
        AudioTimeSyncController_StartSong,
        &GlobalNamespace::AudioTimeSyncController::StartSong,
        void,
        GlobalNamespace::AudioTimeSyncController* self,
        float startTimeOffset)
    {
        AudioTimeSyncController_StartSong(self, startTimeOffset);

        if(!BigScreen::Settings::Instance().ModEnabled())
            return;

        // StartSong runs after the gameplay scene and environment have loaded,
        // so Unity objects created here belong to the correct scene. The screen
        // remains hidden until the worker publishes its first decoded frame.
        if(BigScreen::PlaybackSession::Instance().HasPreparedVideo() &&
           !BigScreen::Settings::Instance().EnvironmentMotionEnabled())
        {
            DisableEnvironmentMotion();
        }
        BigScreen::PlaybackSession::Instance().Start(BigScreen::PlaybackContext::Gameplay);
    }

    MAKE_HOOK_MATCH(
        SongPreviewPlayer_Update,
        &GlobalNamespace::SongPreviewPlayer::Update,
        void,
        GlobalNamespace::SongPreviewPlayer* self)
    {
        SongPreviewPlayer_Update(self);

        if(!BigScreen::Settings::Instance().ModEnabled())
            return;

        BigScreen::SelectionVideoToggle::Instance().TickDownloadUi();

        auto& session = BigScreen::PlaybackSession::Instance();
        if(!session.IsMenuPreviewActive())
            return;

        // SongPreviewPlayer crossfades between a small bank of AudioSources.
        // Reading its active source after the original Update gives Big Screen
        // the exact clip position heard by the user, including preview start,
        // pause, resume, and crossfade channel changes.
        const int activeChannel = self->__cordl_internal_get__activeChannel();
        auto controllers = self->__cordl_internal_get__audioSourceControllers();
        if(!controllers || activeChannel < 0 || activeChannel >= controllers.size())
            return;

        auto* controller = controllers[activeChannel];
        if(!controller)
            return;
        auto audioSource = controller->__cordl_internal_get_audioSource();
        if(audioSource)
            session.Tick(audioSource->get_time());
    }

    MAKE_HOOK_MATCH(
        AudioTimeSyncController_Update,
        &GlobalNamespace::AudioTimeSyncController::Update,
        void,
        GlobalNamespace::AudioTimeSyncController* self)
    {
        AudioTimeSyncController_Update(self);

        if(!BigScreen::Settings::Instance().ModEnabled())
            return;

        // Beat Saber's song time is the sole playback clock. It stops during a
        // pause, jumps on restart/scrub, incorporates practice speed, and is the
        // timeline Replay advances during playback and frame-by-frame capture.
        BigScreen::PlaybackSession::Instance().Tick(self->get_songTime());
    }

    MAKE_HOOK_MATCH(
        StandardLevelScenesTransitionSetupDataSO_Finish,
        &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::Finish,
        void,
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::LevelCompletionResults* levelCompletionResults)
    {
        // Release native decoder resources and Unity objects before the normal
        // transition tears down GameCore. This also guarantees that the next
        // selected map cannot inherit a stale frame or decoder worker.
        if(BigScreen::Settings::Instance().ModEnabled())
            BigScreen::PlaybackSession::Instance().Stop();
        StandardLevelScenesTransitionSetupDataSO_Finish(self, levelCompletionResults);
    }
}

MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept
{
    *info = modInfo.to_c();
    BigScreen::Settings::Instance().Load();
    Paper::Logger::RegisterFileContextId(PaperLogger.tag);
    PaperLogger.info("Big Screen {} initialized", VERSION);
}

MOD_EXTERN_FUNC void late_load() noexcept
{
    il2cpp_functions::Init();
    custom_types::Register::AutoRegister();
    BigScreen::VideoLibrary::Instance().Initialize();
    std::string downloaderError;
    if(!BigScreen::DownloadManager::Instance().Initialize(downloaderError))
        PaperLogger.error("Downloader unavailable: {}", downloaderError);

    // Hooks stay on public Beat Saber lifecycle and clock APIs: selection view
    // visibility owns menu preview lifetime, scene transition owns gameplay
    // setup, and Beat Saber's audio clocks remain authoritative for sync.
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo);
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_InitAndSetupScenes);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_Awake);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_OnEnable);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_OnDisable);
    INSTALL_HOOK(PaperLogger, StandardLevelDetailView_OnDestroy);
    INSTALL_HOOK(PaperLogger, TrackLaneRingsRotationEffectSpawner_HandleBeatmapEvent);
    INSTALL_HOOK(PaperLogger, TrackLaneRingsPositionStepEffectSpawner_HandleBeatmapEvent);
    INSTALL_HOOK(PaperLogger, AudioTimeSyncController_StartSong);
    INSTALL_HOOK(PaperLogger, AudioTimeSyncController_Update);
    INSTALL_HOOK(PaperLogger, SongPreviewPlayer_Update);
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_Finish);

    // SongCore publishes selections after its custom-level details are ready,
    // including WIP songs. A plain native callback keeps this path independent
    // of Beat Saber's private view-controller field layout.
    SongCore::API::LevelSelect::GetLevelWasSelectedEvent().addCallback(HandleLevelWasSelected);
    BigScreen::SettingsMenu::Instance().Register();
    PaperLogger.info("Big Screen hooks installed");
}
