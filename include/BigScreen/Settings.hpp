// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <chrono>

namespace BigScreen {
    /// One complete screen geometry preset. Keeping geometry together prevents
    /// a profile switch from briefly mixing values from two layouts.
    struct ScreenLayoutProfile {
        bool advancedControls = false;
        // Controls only the unused canvas around a letterboxed picture. The
        // video itself has an independent opacity so users can remove black
        // bars without unintentionally fading the picture.
        bool letterboxTransparency = false;
        float videoOpacity = 1.0f;
        float distanceOffset = 0.0f;
        float horizontalOffset = 0.0f;
        float verticalOffset = 0.0f;
        float tiltOffset = 0.0f;
        float scale = 1.0f;
        bool curved = false;
        float curvature = 0.35f;
        bool maintainAspectRatio = false;
        float screenRoll = 0.0f;
        float videoRotation = 0.0f;
        float videoZoom = 1.0f;
        float videoOffsetX = 0.0f;
        float videoOffsetY = 0.0f;
        float videoTilt = 0.0f;
        bool stretchVideoToFit = false;
        bool undocked = false;
        bool undockedConfigured = false;
        float undockedPositionX = 0.0f;
        float undockedPositionY = 1.8f;
        float undockedPositionZ = 3.2f;
        float undockedRotationX = 0.0f;
        float undockedRotationY = 0.0f;
        float undockedRotationZ = 0.0f;
        float undockedWidth = 2.4f;
        float undockedHeight = 1.35f;
    };

    /// Persistent, user-facing behavior shared by the main-menu page, map
    /// selection, decoding, and gameplay hooks.
    ///
    /// Map metadata remains the authoritative baseline for placement and
    /// timing. These values are deliberately expressed as global adjustments
    /// so users can tune the experience for their headset without editing
    /// every custom song.
    class Settings final {
    public:
        static Settings& Instance();

        void Load();
        void Reset();
        /// Restores only the selected screen profile. Global options and the
        /// other four saved screen layouts are deliberately left unchanged.
        void ResetActiveScreenLayout();
        /// Persists a pending group of rapid UI changes after its debounce
        /// period. This is called from the existing main-thread update hook.
        void TickPersistence();
        /// Forces pending settings to disk before focus loss or gameplay.
        void Flush();

        /// Starts a non-persistent Screen-tab editing session. Screen layout
        /// setters continue to update the live preview, but no JSON is written
        /// until CommitScreenEditTransaction is called. Cancelling restores
        /// every layout and the active-layout selection captured here.
        void BeginScreenEditTransaction();
        void CommitScreenEditTransaction();
        void CancelScreenEditTransaction();
        bool ScreenEditTransactionActive() const { return screenEditActive_; }

        bool ModEnabled() const { return modEnabled_; }
        bool DistractionFreeMenu() const { return distractionFreeMenu_; }
        bool ShowMenuEnvironment() const { return showMenuEnvironment_; }
        // Scenery, lighting, and floor visibility share one user preference.
        // Keep this accessor so the focused floor-discovery component remains
        // decoupled from the settings representation.
        bool ShowMenuFloor() const { return showMenuEnvironment_; }
        bool ShowLaneGuidesEnabled() const { return showLaneGuidesEnabled_; }
        bool VideoEnabled() const { return videoEnabled_; }
        bool MenuPreviewEnabled() const { return menuPreviewEnabled_; }
        bool AdvancedOptionsEnabled() const { return ActiveLayout().advancedControls; }
        int ActiveScreenLayout() const { return activeScreenLayout_; }
        const ScreenLayoutProfile& ActiveLayout() const {
            return screenLayouts_[activeScreenLayout_];
        }
        float ScreenDistanceOffset() const { return ActiveLayout().distanceOffset; }
        float ScreenHorizontalOffset() const { return ActiveLayout().horizontalOffset; }
        float ScreenVerticalOffset() const { return ActiveLayout().verticalOffset; }
        float ScreenTiltOffset() const { return ActiveLayout().tiltOffset; }
        float ScreenScale() const { return ActiveLayout().scale; }
        float MaximumScreenScale() const;
        bool CurvedScreenEnabled() const { return ActiveLayout().curved; }
        float ScreenCurvature() const { return ActiveLayout().curvature; }
        bool MaintainCurveAspectRatio() const { return ActiveLayout().maintainAspectRatio; }
        float ScreenRoll() const { return ActiveLayout().screenRoll; }
        float VideoRotation() const { return ActiveLayout().videoRotation; }
        float VideoZoom() const { return ActiveLayout().videoZoom; }
        float VideoOffsetX() const { return ActiveLayout().videoOffsetX; }
        float VideoOffsetY() const { return ActiveLayout().videoOffsetY; }
        float VideoTilt() const { return ActiveLayout().videoTilt; }
        bool StretchVideoToFit() const { return ActiveLayout().stretchVideoToFit; }
        bool UndockedScreenEnabled() const { return ActiveLayout().undocked; }
        bool RespectMapperSettings() const { return respectMapperSettings_; }
        bool AllowChromaOverride() const { return allowChromaOverride_; }
        bool LetterboxTransparencyEnabled() const {
            return ActiveLayout().letterboxTransparency;
        }
        float VideoOpacity() const { return ActiveLayout().videoOpacity; }
        bool MapLightShowEnabled() const { return mapLightShowEnabled_; }
        bool HideBackWallLights() const { return hideBackWallLights_; }
        bool HideRingLights() const { return hideRingLights_; }
        bool HideSideLaserLights() const { return hideSideLaserLights_; }
        bool EnvironmentOverrideEnabled() const { return environmentOverrideEnabled_; }
        bool GlassDesertOverrideEnabled() const { return glassDesertOverrideEnabled_; }
        bool DisableEnvironmentMotion() const { return disableEnvironmentMotion_; }
        bool HideTrackRings() const { return hideTrackRings_; }
        bool HideSideBars() const { return hideSideBars_; }
        bool HideSpectrogramBars() const { return hideSpectrogramBars_; }
        int PlaybackFpsLimit() const { return playbackFpsLimit_; }
        bool UseFfmpeg9() const { return useFfmpeg9_; }
        bool EmbeddedVideoShaderEnabled() const {
            return embeddedVideoShaderEnabled_;
        }
        float NativeBloomLevel() const { return nativeBloomLevel_; }
        float CinemaBloomLevel() const { return cinemaBloomLevel_; }
        bool HardwareDecodingEnabled() const { return hardwareDecodingEnabled_; }
        bool AutomaticPerformanceEnabled() const { return automaticPerformanceEnabled_; }
        int AutomaticPerformanceThreshold() const { return automaticPerformanceThreshold_; }
        float AutomaticPerformanceAttackSeconds() const {
            return automaticPerformanceAttackSeconds_;
        }
        float AutomaticPerformanceReleaseSeconds() const {
            return automaticPerformanceReleaseSeconds_;
        }
        int AutomaticPerformanceFpsStep() const {
            return automaticPerformanceFpsStep_;
        }
        bool AutomaticPerformanceOscillationPreventionEnabled() const {
            return automaticPerformanceOscillationPreventionEnabled_;
        }
        int AutomaticPerformanceOscillationLimit() const {
            return automaticPerformanceOscillationLimit_;
        }
        bool PerformanceDiagnosticsEnabled() const { return performanceDiagnosticsEnabled_; }
        float PerformancePanelPositionX() const { return performancePanelPositionX_; }
        float PerformancePanelPositionY() const { return performancePanelPositionY_; }
        float PerformancePanelPositionZ() const { return performancePanelPositionZ_; }
        float PerformancePanelRotationX() const { return performancePanelRotationX_; }
        float PerformancePanelRotationY() const { return performancePanelRotationY_; }
        float PerformancePanelRotationZ() const { return performancePanelRotationZ_; }
        bool PowerBenchmarkEnabled() const { return powerBenchmarkEnabled_; }
        bool NightlyDownloaderUpdates() const { return nightlyDownloaderUpdates_; }

        void SetModEnabled(bool value);
        void SetDistractionFreeMenu(bool value);
        void SetShowMenuEnvironment(bool value);
        void SetShowLaneGuidesEnabled(bool value);
        void SetVideoEnabled(bool value);
        void SetMenuPreviewEnabled(bool value);
        void SetAdvancedOptionsEnabled(bool value);
        void SetActiveScreenLayout(int value);
        void SetScreenDistanceOffset(float value);
        void SetScreenHorizontalOffset(float value);
        void SetScreenVerticalOffset(float value);
        void SetScreenTiltOffset(float value);
        void SetScreenScale(float value);
        void SetCurvedScreenEnabled(bool value);
        void SetScreenCurvature(float value);
        void SetMaintainCurveAspectRatio(bool value);
        void SetScreenRoll(float value);
        void SetVideoRotation(float value);
        void SetVideoZoom(float value);
        void SetVideoOffsetX(float value);
        void SetVideoOffsetY(float value);
        void SetVideoTilt(float value);
        void SetStretchVideoToFit(bool value);
        void SetUndockedScreenEnabled(bool value);
        void SaveUndockedScreen(
            float positionX, float positionY, float positionZ,
            float rotationX, float rotationY, float rotationZ,
            float width, float height);
        void SetRespectMapperSettings(bool value);
        void SetAllowChromaOverride(bool value);
        void SetLetterboxTransparencyEnabled(bool value);
        void SetVideoOpacity(float value);
        void SetMapLightShowEnabled(bool value);
        void SetHideBackWallLights(bool value);
        void SetHideRingLights(bool value);
        void SetHideSideLaserLights(bool value);
        void SetEnvironmentOverrideEnabled(bool value);
        void SetGlassDesertOverrideEnabled(bool value);
        void SetDisableEnvironmentMotion(bool value);
        void SetHideTrackRings(bool value);
        void SetHideSideBars(bool value);
        void SetHideSpectrogramBars(bool value);
        void SetPlaybackFpsLimit(int value);
        void SetUseFfmpeg9(bool value);
        void SetEmbeddedVideoShaderEnabled(bool value);
        void SetNativeBloomLevel(float value);
        void SetCinemaBloomLevel(float value);
        void SetHardwareDecodingEnabled(bool value);
        void SetAutomaticPerformanceEnabled(bool value);
        void SetAutomaticPerformanceThreshold(int value);
        void SetAutomaticPerformanceAttackSeconds(float value);
        void SetAutomaticPerformanceReleaseSeconds(float value);
        void SetAutomaticPerformanceFpsStep(int value);
        void SetAutomaticPerformanceOscillationPreventionEnabled(bool value);
        void SetAutomaticPerformanceOscillationLimit(int value);
        void SetPerformanceDiagnosticsEnabled(bool value);
        /// Stores the diagnostics panel's last safe six-degree-of-freedom
        /// placement. Callers commit this only at toggle/menu/gameplay
        /// boundaries, never during controller movement.
        void SetPerformancePanelPlacement(
            float positionX,
            float positionY,
            float positionZ,
            float rotationX,
            float rotationY,
            float rotationZ);
        void ResetPerformancePanelPlacement();
        void SetPowerBenchmarkEnabled(bool value);
        void SetNightlyDownloaderUpdates(bool value);

    private:
        Settings() = default;

        void Save();
        void WriteNow();

        bool modEnabled_ = true;
        // The placement preview is easiest to judge against an uncluttered
        // world. This affects only Big Screen's own menu lifetime and restores
        // every stock or optional-mod object when the player leaves.
        bool distractionFreeMenu_ = true;
        // This positive-logic switch describes what remains visible. The
        // environment implementation disables only visual/light components;
        // the menu hierarchy, input systems, and Big Screen surfaces stay live.
        bool showMenuEnvironment_ = true;
        // Lane guides remain independent from environment and floor visibility.
        bool showLaneGuidesEnabled_ = false;
        bool videoEnabled_ = true;
        bool menuPreviewEnabled_ = true;
        std::array<ScreenLayoutProfile, 5> screenLayouts_{};
        int activeScreenLayout_ = 0;
        // Mapper-authored Cinema presentation is honored by default. Turning
        // this off keeps the mapper's media identity and synchronization but
        // deliberately replaces its screen/effect presentation with the
        // player's selected Big Screen layout.
        bool respectMapperSettings_ = true;
        // Only maps that actually contain mapper-authored Cinema presentation
        // fields can take ownership; ordinary video maps remain on the selected
        // Big Screen layout even though compatibility defaults to enabled.
        bool allowChromaOverride_ = true;
        bool mapLightShowEnabled_ = true;
        bool hideBackWallLights_ = true;
        bool hideRingLights_ = true;
        bool hideSideLaserLights_ = true;
        bool environmentOverrideEnabled_ = true;
        bool glassDesertOverrideEnabled_ = false;
        // A fresh install suppresses rotating/moving environment geometry by
        // default so the video remains readable. The UI presents this as a
        // disable-style toggle, therefore true is the visible ON state.
        bool disableEnvironmentMotion_ = true;
        bool hideTrackRings_ = true;
        bool hideSideBars_ = true;
        bool hideSpectrogramBars_ = true;
        int playbackFpsLimit_ = 30;
        // New installations use the current bundled runtime. FFmpeg 4.4.8
        // remains available from the menu as a compatibility/A-B option.
        bool useFfmpeg9_ = true;
        // Selects how the visible video picture is drawn. OFF (default) uses
        // Unity's UI/Default shader and an invisible embedded alpha-only guard.
        // ON uses Big Screen's embedded full-featured shader directly. Both
        // paths feed mapper bloom through the same mono-safe capture material;
        // if an embedded resource cannot load, the documented fallback ladder
        // keeps the UI path usable and records the failure for diagnosis.
        bool embeddedVideoShaderEnabled_ = false;
        // Diagnostic controls keep Beat Saber's framebuffer-alpha bloom and
        // Big Screen's Cinema/Kawase contribution independently adjustable.
        float nativeBloomLevel_ = 1.0f;
        float cinemaBloomLevel_ = 1.0f;
        // Compatible videos use the Quest's MediaCodec decoder by default.
        // Formats that permit software decoding still fall back safely when
        // MediaCodec cannot open the file.
        bool hardwareDecodingEnabled_ = true;
        bool automaticPerformanceEnabled_ = false;
        int automaticPerformanceThreshold_ = 5;
        float automaticPerformanceAttackSeconds_ = 5.0f;
        float automaticPerformanceReleaseSeconds_ = 5.0f;
        int automaticPerformanceFpsStep_ = 5;
        bool automaticPerformanceOscillationPreventionEnabled_ = true;
        int automaticPerformanceOscillationLimit_ = 3;
        bool performanceDiagnosticsEnabled_ = false;
        // Both menu and gameplay recreate the movable diagnostics panel from
        // this shared transform. The default remains above the note lanes and
        // central menu, where it cannot cover important controls.
        float performancePanelPositionX_ = 0.0f;
        float performancePanelPositionY_ = 3.05f;
        float performancePanelPositionZ_ = 4.25f;
        float performancePanelRotationX_ = 0.0f;
        float performancePanelRotationY_ = 0.0f;
        float performancePanelRotationZ_ = 0.0f;
        bool powerBenchmarkEnabled_ = false;
        bool nightlyDownloaderUpdates_ = false;
        bool savePending_ = false;
        std::chrono::steady_clock::time_point saveDeadline_{};
        bool screenEditActive_ = false;
        bool screenEditDirty_ = false;
        std::array<ScreenLayoutProfile, 5> screenEditLayouts_{};
        int screenEditActiveLayout_ = 0;
        bool screenEditAllowChromaOverride_ = true;
        bool screenEditRespectMapperSettings_ = true;
    };
}
