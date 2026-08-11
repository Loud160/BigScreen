#pragma once

#include <functional>
#include <string>
#include <vector>

namespace BSML {
    class ClickableText;
    class CustomListTableData;
    class IncrementSetting;
}
namespace GlobalNamespace { class BeatmapLevel; }
namespace HMUI { class InputFieldView; class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
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
        void Tick();
        void Deactivate();

    private:
        VideoLibraryMenu() = default;
        void RebuildCatalog();
        void RebuildVisibleRows();
        void SelectRow(int row);
        void ShowBrowser();
        void ShowEditor();
        void ChangeFilter(int direction);
        void StartOrCancelDownload();
        void RemoveOverride();
        void FitToSong();
        void RefreshDetails();
        void StartSelectedPreview();
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
        TMPro::TextMeshProUGUI* browserTitle_ = nullptr;
        TMPro::TextMeshProUGUI* browserStorage_ = nullptr;
        TMPro::TextMeshProUGUI* filterText_ = nullptr;
        TMPro::TextMeshProUGUI* detailTitle_ = nullptr;
        TMPro::TextMeshProUGUI* detailText_ = nullptr;
        TMPro::TextMeshProUGUI* detailStorage_ = nullptr;
        UnityEngine::UI::Button* filterPreviousButton_ = nullptr;
        UnityEngine::UI::Button* filterNextButton_ = nullptr;
        UnityEngine::UI::Button* backToListButton_ = nullptr;
        UnityEngine::UI::Button* downloadButton_ = nullptr;
        UnityEngine::UI::Button* fitButton_ = nullptr;
        UnityEngine::UI::Button* removeButton_ = nullptr;
        std::vector<BSML::ClickableText*> alphabetButtons_;
        std::vector<SongLibraryItem> catalog_;
        std::vector<SongLibraryItem*> visible_;
        GlobalNamespace::BeatmapLevel* selected_ = nullptr;
        SongLibraryFilter filter_ = SongLibraryFilter::All;
        std::string search_;
        std::string url_;
        double offset_ = 0.0;
        double rate_ = 1.0;
        float previewStartedAt_ = 0.0f;
        bool active_ = false;
        bool editorVisible_ = false;
        int tickCounter_ = 0;
    };
}
