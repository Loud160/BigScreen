#include "main.hpp"

#include "BigScreen/PlaybackSession.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/EnvironmentInfoSO.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace {
    MAKE_HOOK_MATCH(
        StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo,
        &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::InitEnvironmentInfo,
        void,
        GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
        GlobalNamespace::OverrideEnvironmentSettings* overrideEnvironmentSettings,
        GlobalNamespace::EnvironmentsListModel* environmentsListModel)
    {
        // Init has already stored the chosen BeatmapLevel by the time it calls
        // this helper. Preparing here is early enough to influence environment
        // selection but late enough to avoid hooking both overloaded Init APIs.
        BigScreen::PlaybackSession::Instance().Prepare(self->get_beatmapLevel());
        StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo(
            self,
            overrideEnvironmentSettings,
            environmentsListModel);

        const auto& requested = BigScreen::PlaybackSession::Instance().RequestedEnvironment();
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
        AudioTimeSyncController_StartSong,
        &GlobalNamespace::AudioTimeSyncController::StartSong,
        void,
        GlobalNamespace::AudioTimeSyncController* self,
        float startTimeOffset)
    {
        AudioTimeSyncController_StartSong(self, startTimeOffset);

        // StartSong runs after the gameplay scene and environment have loaded,
        // so Unity objects created here belong to the correct scene. The screen
        // remains hidden until the worker publishes its first decoded frame.
        BigScreen::PlaybackSession::Instance().Start();
    }

    MAKE_HOOK_MATCH(
        AudioTimeSyncController_Update,
        &GlobalNamespace::AudioTimeSyncController::Update,
        void,
        GlobalNamespace::AudioTimeSyncController* self)
    {
        AudioTimeSyncController_Update(self);

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
        BigScreen::PlaybackSession::Instance().Stop();
        StandardLevelScenesTransitionSetupDataSO_Finish(self, levelCompletionResults);
    }
}

MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept
{
    *info = modInfo.to_c();
    Paper::Logger::RegisterFileContextId(PaperLogger.tag);
    PaperLogger.info("Big Screen {} initialized", VERSION);
}

MOD_EXTERN_FUNC void late_load() noexcept
{
    il2cpp_functions::Init();

    // These four hooks form a deliberately small public-API boundary: prepare
    // and select the environment at transition, create on song start, follow
    // the authoritative song clock, and clean up when the level finishes.
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo);
    INSTALL_HOOK(PaperLogger, AudioTimeSyncController_StartSong);
    INSTALL_HOOK(PaperLogger, AudioTimeSyncController_Update);
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_Finish);
    PaperLogger.info("Big Screen hooks installed");
}
