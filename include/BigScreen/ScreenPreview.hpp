#pragma once

#include <optional>
#include <array>

#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"

namespace BSML { class FloatingScreen; }
namespace HMUI { class ImageView; }
namespace TMPro { class TextMeshProUGUI; }
namespace UnityEngine::UI { class Button; }

namespace BigScreen {
    /// Owns the optional full-size screen shown in Beat Saber's menu world.
    ///
    /// It deliberately uses ScreenSurface, the same geometry and material path
    /// as decoded song videos, so size, placement, curve, and transparency are
    /// represented at their real world scale instead of inside a UI thumbnail.
    class ScreenPreview final {
    public:
        static ScreenPreview& Instance();

        void ActivateCurrentState();
        void SetEnabled(bool enabled);
        void Refresh();
        void Suspend();
        void BeginUndockedEditing();
        void SaveUndockedEditing();
        void CancelUndockedEditing();
        void TickUndockedEditor();
        bool IsUndockedEditing() const { return editorScreen_ != nullptr; }

    private:
        ScreenPreview() = default;

        void CaptureBasePlacement();
        bool CreateWorldScreen();
        void DestroyEditorUi();
        bool CreateEditorUi();
        void UpdateEditorOverlayLayout();
        void PlaceResizeHandle();

        std::optional<MapVideoConfig> baseConfig_;
        std::optional<MapVideoConfig> editorConfig_;
        ScreenSurface surface_;
        BSML::FloatingScreen* editorScreen_ = nullptr;
        BSML::FloatingScreen* resizeHandleScreen_ = nullptr;
        std::array<HMUI::ImageView*, 4> editorBorders_{};
        TMPro::TextMeshProUGUI* editorInstructions_ = nullptr;
        TMPro::TextMeshProUGUI* editorAspectText_ = nullptr;
        UnityEngine::UI::Button* editorSaveButton_ = nullptr;
    };
}
