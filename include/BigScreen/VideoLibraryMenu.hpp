#pragma once

#include <functional>
#include <string>
#include <vector>

#include "beatsaber-hook/shared/utils/typedefs.h"

namespace BSML {
    class ClickableText;
    class CustomListTableData;
    class IncrementSetting;
    class ModalView;
    class SliderSetting;
    class ToggleSetting;
}
namespace GlobalNamespace {
    class BeatmapLevel;
    class IPreviewMediaData;
    class SongPreviewPlayer;
}
namespace HMUI { class ImageView; class InputFieldView; class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace System::Threading::Tasks { template<class TResult> class Task_1; }
namespace UnityEngine { class AudioClip; class AudioSource; class GameObject; class Sprite; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    enum class VideoOrigin;
    enum class SongLibraryGroup { Custom, Wip, Ost, Dlc };
    enum class SongLibraryFilter { All, Custom, Wip, Ost, Dlc, Video };

    struct SongLibraryItem {
        GlobalNamespace::BeatmapLevel* level = nullptr;
        SongLibraryGroup group = SongLibraryGroup::Ost;
    };

    /// Owns the contents of two independent right-panel view controllers: a
    /// song browser and a focused editor. The parent flow coordinator swaps
    /// controllers during navigation, so hidden controls can never overlap or
    /// intercept input from the page currently visible to the player.
    class VideoLibraryMenu final {
    public:
        static VideoLibraryMenu& Instance();
        void CreateUi(
            HMUI::ViewController* browserController,
            HMUI::ViewController* editorController,
            std::function<void(bool showEditor)> navigate);
        void Refresh();
        void Tick(GlobalNamespace::SongPreviewPlayer* songPreviewPlayer);
        void Deactivate();

    private:
        VideoLibraryMenu() = default;
        void RebuildCatalog();
        void RebuildVisibleRows();
        void SelectRow(int row);
        void ShowBrowser();
        void ShowEditor();
        void ChangeFilter(int direction);
        void BeginUrlProbe();
        void StartOrCancelDownload();
        void PasteUrlFromClipboard();
        void RemoveOverride();
        bool ApplyFitToSong(bool reportStatus);
        bool SaveTiming();
        void RefreshDetails();
        void ClearThumbnail();
        void RefreshVisibleVideoThumbnails();
        void StartSelectedPreview();
        void RequestSelectedAudio();
        void TogglePreviewPlayback();
        void SeekPreview(float songTimeSeconds);
        void StartPreviewAudio();
        void StopPreviewAudio(bool returnToMenuMusic);
        void RefreshPlaybackControls();
        void JumpToLetter(char letter);
        VideoOrigin SelectedVideoOrigin() const;

        HMUI::ViewController* browserController_ = nullptr;
        HMUI::ViewController* editorController_ = nullptr;
        std::function<void(bool)> navigate_;
        BSML::CustomListTableData* list_ = nullptr;
        HMUI::InputFieldView* searchInput_ = nullptr;
        HMUI::InputFieldView* urlInput_ = nullptr;
        BSML::IncrementSetting* offsetSetting_ = nullptr;
        BSML::IncrementSetting* rateSetting_ = nullptr;
        BSML::SliderSetting* playbackScrubber_ = nullptr;
        BSML::ToggleSetting* fitToggle_ = nullptr;
        BSML::ToggleSetting* blackLeadInToggle_ = nullptr;
        TMPro::TextMeshProUGUI* browserTitle_ = nullptr;
        TMPro::TextMeshProUGUI* browserStorage_ = nullptr;
        TMPro::TextMeshProUGUI* filterText_ = nullptr;
        TMPro::TextMeshProUGUI* detailTitle_ = nullptr;
        TMPro::TextMeshProUGUI* detailText_ = nullptr;
        TMPro::TextMeshProUGUI* detailMapStorage_ = nullptr;
        TMPro::TextMeshProUGUI* detailLibraryStorage_ = nullptr;
        TMPro::TextMeshProUGUI* detailFreeStorage_ = nullptr;
        TMPro::TextMeshProUGUI* playbackTimeText_ = nullptr;
        TMPro::TextMeshProUGUI* pasteUrlButtonText_ = nullptr;
        TMPro::TextMeshProUGUI* downloadButtonText_ = nullptr;
        HMUI::ImageView* downloadProgressTrack_ = nullptr;
        HMUI::ImageView* downloadProgressFill_ = nullptr;
        HMUI::ImageView* playbackScrubberFill_ = nullptr;
        HMUI::ImageView* urlThumbnail_ = nullptr;
        BSML::ModalView* removeConfirmModal_ = nullptr;
        std::vector<UnityEngine::GameObject*> videoOnlyRows_;
        UnityEngine::UI::Button* filterPreviousButton_ = nullptr;
        UnityEngine::UI::Button* filterNextButton_ = nullptr;
        UnityEngine::UI::Button* backToListButton_ = nullptr;
        UnityEngine::UI::Button* pasteUrlButton_ = nullptr;
        UnityEngine::UI::Button* downloadButton_ = nullptr;
        UnityEngine::UI::Button* playPauseButton_ = nullptr;
        UnityEngine::UI::Button* removeButton_ = nullptr;
        std::vector<BSML::ClickableText*> alphabetButtons_;
        std::vector<SongLibraryItem> catalog_;
        std::vector<SongLibraryItem*> visible_;
        GlobalNamespace::BeatmapLevel* selected_ = nullptr;
        GlobalNamespace::IPreviewMediaData* previewMediaData_ = nullptr;
        GlobalNamespace::SongPreviewPlayer* songPreviewPlayer_ = nullptr;
        System::Threading::Tasks::Task_1<UnityW<UnityEngine::AudioClip>>* audioLoadTask_ = nullptr;
        UnityEngine::AudioClip* previewAudioClip_ = nullptr;
        UnityEngine::AudioSource* previewAudioSource_ = nullptr;
        SongLibraryFilter filter_ = SongLibraryFilter::All;
        std::string search_;
        std::string url_;
        std::string transientStatus_;
        std::string loadedThumbnailPath_;
        std::string completedVideoThumbnailIdentity_;
        std::string audioLoadLevelId_;
        std::string autoPlayedDownloadIdentity_;
        UnityEngine::Sprite* loadedThumbnailSprite_ = nullptr;
        double offset_ = 0.0;
        double rate_ = 1.0;
        double previewSongTime_ = 0.0;
        bool fitToSong_ = false;
        bool blackDuringLeadIn_ = false;
        bool active_ = false;
        bool editorVisible_ = false;
        bool previewPlaying_ = false;
        bool playWhenAudioReady_ = false;
        bool suppressScrubberCallback_ = false;
        float scrubberFollowResumeTime_ = 0.0f;
        bool suppressTimingCallbacks_ = false;
        bool suppressUrlCallback_ = false;
        int tickCounter_ = 0;
    };
}
