#pragma once

namespace BigScreen {
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
        bool VideoEnabled() const { return videoEnabled_; }
        bool MenuPreviewEnabled() const { return menuPreviewEnabled_; }
        float ScreenDistanceOffset() const { return screenDistanceOffset_; }
        float ScreenHorizontalOffset() const { return screenHorizontalOffset_; }
        float ScreenVerticalOffset() const { return screenVerticalOffset_; }
        float ScreenTiltOffset() const { return screenTiltOffset_; }
        float ScreenScale() const { return screenScale_; }
        bool CurvedScreenEnabled() const { return curvedScreenEnabled_; }
        float ScreenCurvature() const { return screenCurvature_; }
        bool TransparencyEnabled() const { return transparencyEnabled_; }
        bool MapLightShowEnabled() const { return mapLightShowEnabled_; }
        bool HideBackWallLights() const { return hideBackWallLights_; }
        bool EnvironmentOverrideEnabled() const { return environmentOverrideEnabled_; }
        bool GlassDesertOverrideEnabled() const { return glassDesertOverrideEnabled_; }
        bool EnvironmentMotionEnabled() const { return environmentMotionEnabled_; }
        bool HideTrackRings() const { return hideTrackRings_; }
        bool HideSideBars() const { return hideSideBars_; }
        bool HideSpectrogramBars() const { return hideSpectrogramBars_; }
        int PlaybackFpsLimit() const { return playbackFpsLimit_; }
        int ResolutionHeight() const { return resolutionHeight_; }
        bool NightlyDownloaderUpdates() const { return nightlyDownloaderUpdates_; }

        void SetModEnabled(bool value);
        void SetVideoEnabled(bool value);
        void SetMenuPreviewEnabled(bool value);
        void SetScreenDistanceOffset(float value);
        void SetScreenHorizontalOffset(float value);
        void SetScreenVerticalOffset(float value);
        void SetScreenTiltOffset(float value);
        void SetScreenScale(float value);
        void SetCurvedScreenEnabled(bool value);
        void SetScreenCurvature(float value);
        void SetTransparencyEnabled(bool value);
        void SetMapLightShowEnabled(bool value);
        void SetHideBackWallLights(bool value);
        void SetEnvironmentOverrideEnabled(bool value);
        void SetGlassDesertOverrideEnabled(bool value);
        void SetEnvironmentMotionEnabled(bool value);
        void SetHideTrackRings(bool value);
        void SetHideSideBars(bool value);
        void SetHideSpectrogramBars(bool value);
        void SetPlaybackFpsLimit(int value);
        void SetResolutionHeight(int value);
        void SetNightlyDownloaderUpdates(bool value);

    private:
        Settings() = default;

        void Save();

        bool modEnabled_ = true;
        bool videoEnabled_ = true;
        bool menuPreviewEnabled_ = true;
        float screenDistanceOffset_ = 0.0f;
        float screenHorizontalOffset_ = 0.0f;
        float screenVerticalOffset_ = 0.0f;
        float screenTiltOffset_ = 0.0f;
        float screenScale_ = 1.0f;
        bool curvedScreenEnabled_ = false;
        float screenCurvature_ = 0.35f;
        bool transparencyEnabled_ = false;
        bool mapLightShowEnabled_ = true;
        bool hideBackWallLights_ = true;
        bool environmentOverrideEnabled_ = true;
        bool glassDesertOverrideEnabled_ = false;
        bool environmentMotionEnabled_ = true;
        bool hideTrackRings_ = true;
        bool hideSideBars_ = true;
        bool hideSpectrogramBars_ = true;
        int playbackFpsLimit_ = 30;
        int resolutionHeight_ = 720;
        bool nightlyDownloaderUpdates_ = false;
    };
}
