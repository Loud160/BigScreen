#include "main.hpp"

#include "BigScreen/PlaybackSession.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/EnvironmentInfoSO.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};
static bool showMenuPreview = true;

namespace {
    Configuration& GetConfiguration()
    {
        static Configuration configuration(modInfo);
        return configuration;
    }

    void LoadSettings()
    {
        auto& configuration = GetConfiguration();
        configuration.Load();

        // Configuration::Load guarantees an object document for a valid file.
        // Be defensive around malformed user edits and restore only our own
        // field, leaving unrelated future settings untouched.
        auto& document = configuration.config;
        if(!document.IsObject())
            document.SetObject();

        const auto existing = document.FindMember("showMenuPreview");
        if(existing != document.MemberEnd() && existing->value.IsBool())
        {
            showMenuPreview = existing->value.GetBool();
            return;
        }

        document.RemoveMember("showMenuPreview");
        document.AddMember("showMenuPreview", true, document.GetAllocator());
        configuration.Write();
        showMenuPreview = true;
    }

    void HandleLevelWasSelected(
        SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& eventArgs)
    {
        auto& session = BigScreen::PlaybackSession::Instance();

        // A built-in song, a level without video metadata, or a disabled
        // preference must immediately remove any preview left by the previous
        // selection. Gameplay preparation will create its own fresh session.
        if(!showMenuPreview || !eventArgs.isCustom || !eventArgs.customBeatmapLevel)
        {
            session.Stop();
            return;
        }

        session.Prepare(eventArgs.customBeatmapLevel);
        if(session.HasPreparedVideo())
            session.Start(BigScreen::PlaybackContext::MenuPreview);
    }
}

bool IsMenuPreviewEnabled()
{
    return showMenuPreview;
}

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
        BigScreen::PlaybackSession::Instance().Start(BigScreen::PlaybackContext::Gameplay);
    }

    MAKE_HOOK_MATCH(
        SongPreviewPlayer_Update,
        &GlobalNamespace::SongPreviewPlayer::Update,
        void,
        GlobalNamespace::SongPreviewPlayer* self)
    {
        SongPreviewPlayer_Update(self);

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
    LoadSettings();
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
    INSTALL_HOOK(PaperLogger, SongPreviewPlayer_Update);
    INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_Finish);

    // SongCore publishes selections after its custom-level details are ready,
    // including WIP songs. A plain native callback keeps this path independent
    // of Beat Saber's private view-controller field layout.
    SongCore::API::LevelSelect::GetLevelWasSelectedEvent().addCallback(HandleLevelWasSelected);
    PaperLogger.info("Big Screen hooks installed");
}
