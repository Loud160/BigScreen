// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <optional>
#include <array>

#include "BigScreen/MapVideoConfig.hpp"
#include "BigScreen/ScreenSurface.hpp"
#include "BigScreen/Settings.hpp"

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
        /// Restores the selected user layout on a neutral screen after leaving
        /// a mapper-positioned Video Library preview. This deliberately drops
        /// cached Chroma/Cinema geometry from the preview only; it does not
        /// alter the map metadata or the user's Respect Mapper Settings choice.
        void ActivateUserLayout();
        void SetEnabled(bool enabled);
        void Refresh();
        void Suspend();
        void BeginUndockedEditing();
        void SaveUndockedEditing();
        void CancelUndockedEditing();
        /// Applies current Screen-tab draft values without closing the editor.
        void RefreshUndockedEditingFromSettings();
        /// Captures controller movement before the layout selector changes.
        void StageCurrentUndockedPlacement();
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
        /// Keeps a live Video Library preview attached to the editor while
        /// placing its rendered pixels behind the editor's grab controls.
        bool ApplyLibraryPreviewEditorDisplay(bool rebuildGeometry);

        std::optional<MapVideoConfig> baseConfig_;
        // ActivateCurrentState follows PlaybackSession's already-resolved
        // display configuration. That keeps Respect Mapper Settings identical
        // between the retained menu-world preview and gameplay without
        // duplicating Cinema geometry rules here. ActivateUserLayout clears
        // this ownership explicitly when the song browser needs the player's
        // neutral layout instead of the last selected map.
        bool followPreparedDisplay_ = false;
        std::optional<MapVideoConfig> editorConfig_;
        ScreenSurface surface_;
        BSML::FloatingScreen* editorScreen_ = nullptr;
        BSML::FloatingScreen* resizeHandleScreen_ = nullptr;
        std::array<HMUI::ImageView*, 4> editorBorders_{};
        HMUI::ImageView* editorMoveBar_ = nullptr;
        TMPro::TextMeshProUGUI* editorInstructions_ = nullptr;
        TMPro::TextMeshProUGUI* editorMoveText_ = nullptr;
        TMPro::TextMeshProUGUI* editorAspectText_ = nullptr;
        UnityEngine::UI::Button* editorSaveButton_ = nullptr;
        std::optional<ScreenLayoutProfile> editorAppliedLayout_;
        int editorLayoutIndex_ = -1;
    };
}
