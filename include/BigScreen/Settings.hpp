#pragma once

#include <array>

namespace BigScreen {
    /// One complete screen geometry preset. Keeping geometry together prevents
    /// a profile switch from briefly mixing values from two layouts.
    struct ScreenLayoutProfile {
        float distanceOffset = 0.0f;
        float horizontalOffset = 0.0f;
        float verticalOffset = 0.0f;
        float tiltOffset = 0.0f;
        float scale = 1.0f;
        bool curved = false;
        float curvature = 0.35f;
        bool maintainAspectRatio = false;
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

        bool ModEnabled() const { return modEnabled_; }
        bool DistractionFreeMenu() const { return distractionFreeMenu_; }
        bool VideoEnabled() const { return videoEnabled_; }
        bool MenuPreviewEnabled() const { return menuPreviewEnabled_; }
        int ActiveScreenLayout() const { return activeScreenLayout_; }
        const ScreenLayoutProfile& ActiveLayout() const {
            return screenLayouts_[activeScreenLayout_];
        }
        float ScreenDistanceOffset() const { return ActiveLayout().distanceOffset; }
        float ScreenHorizontalOffset() const { return ActiveLayout().horizontalOffset; }
        float ScreenVerticalOffset() const { return ActiveLayout().verticalOffset; }
        float ScreenTiltOffset() const { return ActiveLayout().tiltOffset; }
        float ScreenScale() const { return ActiveLayout().scale; }
        bool CurvedScreenEnabled() const { return ActiveLayout().curved; }
        float ScreenCurvature() const { return ActiveLayout().curvature; }
        bool MaintainCurveAspectRatio() const { return ActiveLayout().maintainAspectRatio; }
        bool TransparencyEnabled() const { return transparencyEnabled_; }
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
        int ResolutionHeight() const { return resolutionHeight_; }
        bool AutomaticPerformanceEnabled() const { return automaticPerformanceEnabled_; }
        int AutomaticPerformanceThreshold() const { return automaticPerformanceThreshold_; }
        bool PerformanceDiagnosticsEnabled() const { return performanceDiagnosticsEnabled_; }
        bool NightlyDownloaderUpdates() const { return nightlyDownloaderUpdates_; }

        void SetModEnabled(bool value);
        void SetDistractionFreeMenu(bool value);
        void SetVideoEnabled(bool value);
        void SetMenuPreviewEnabled(bool value);
        void SetActiveScreenLayout(int value);
        void SetScreenDistanceOffset(float value);
        void SetScreenHorizontalOffset(float value);
        void SetScreenVerticalOffset(float value);
        void SetScreenTiltOffset(float value);
        void SetScreenScale(float value);
        void SetCurvedScreenEnabled(bool value);
        void SetScreenCurvature(float value);
        void SetMaintainCurveAspectRatio(bool value);
        void SetTransparencyEnabled(bool value);
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
        void SetResolutionHeight(int value);
        void SetAutomaticPerformanceEnabled(bool value);
        void SetAutomaticPerformanceThreshold(int value);
        void SetPerformanceDiagnosticsEnabled(bool value);
        void SetNightlyDownloaderUpdates(bool value);

    private:
        Settings() = default;

        void Save();

        bool modEnabled_ = true;
        // The placement preview is easiest to judge against an uncluttered
        // world. This affects only Big Screen's own menu lifetime and restores
        // every stock or optional-mod object when the player leaves.
        bool distractionFreeMenu_ = true;
        bool videoEnabled_ = true;
        bool menuPreviewEnabled_ = true;
        std::array<ScreenLayoutProfile, 3> screenLayouts_{};
        int activeScreenLayout_ = 0;
        bool transparencyEnabled_ = false;
        bool mapLightShowEnabled_ = true;
        bool hideBackWallLights_ = true;
        bool hideRingLights_ = true;
        bool hideSideLaserLights_ = true;
        bool environmentOverrideEnabled_ = true;
        bool glassDesertOverrideEnabled_ = false;
        bool disableEnvironmentMotion_ = false;
        bool hideTrackRings_ = true;
        bool hideSideBars_ = true;
        bool hideSpectrogramBars_ = true;
        int playbackFpsLimit_ = 30;
        int resolutionHeight_ = 720;
        bool automaticPerformanceEnabled_ = false;
        int automaticPerformanceThreshold_ = 10;
        bool performanceDiagnosticsEnabled_ = false;
        bool nightlyDownloaderUpdates_ = false;
    };
}
