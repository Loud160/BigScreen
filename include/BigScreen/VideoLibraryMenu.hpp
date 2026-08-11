#pragma once

#include <string>
#include <vector>

namespace BSML {
    class CustomListTableData;
    class IncrementSetting;
}
namespace GlobalNamespace { class BeatmapLevel; }
namespace HMUI { class InputFieldView; class ViewController; }
namespace TMPro { class TextMeshProUGUI; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    /// Right-panel browser/editor for durable video assignments. The catalog
    /// is sourced from Beat Saber's loaded repository, so OST, DLC, custom,
    /// and WIP levels all use the same user-override workflow.
    class VideoLibraryMenu final {
    public:
        static VideoLibraryMenu& Instance();
        void CreateUi(HMUI::ViewController* controller);
        void Refresh();
        void Tick();
        void Deactivate();

    private:
        VideoLibraryMenu() = default;
        void RebuildCatalog();
        void RebuildVisibleRows();
        void SelectRow(int row);
        void StartOrCancelDownload();
        void RemoveOverride();
        void FitToSong();
        void RefreshDetails();
        void StartSelectedPreview();

        HMUI::ViewController* controller_ = nullptr;
        BSML::CustomListTableData* list_ = nullptr;
        HMUI::InputFieldView* searchInput_ = nullptr;
        HMUI::InputFieldView* urlInput_ = nullptr;
        BSML::IncrementSetting* offsetSetting_ = nullptr;
        BSML::IncrementSetting* rateSetting_ = nullptr;
        TMPro::TextMeshProUGUI* storageText_ = nullptr;
        TMPro::TextMeshProUGUI* detailText_ = nullptr;
        UnityEngine::UI::Button* downloadButton_ = nullptr;
        UnityEngine::UI::Button* removeButton_ = nullptr;
        std::vector<GlobalNamespace::BeatmapLevel*> catalog_;
        std::vector<GlobalNamespace::BeatmapLevel*> visible_;
        GlobalNamespace::BeatmapLevel* selected_ = nullptr;
        std::string search_;
        std::string url_;
        double offset_ = 0.0;
        double rate_ = 1.0;
        float previewStartedAt_ = 0.0f;
        bool active_ = false;
        int tickCounter_ = 0;
    };
}
