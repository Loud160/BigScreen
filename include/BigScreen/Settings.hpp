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

        bool VideoEnabledByDefault() const { return videoEnabledByDefault_; }
        bool MenuPreviewEnabled() const { return menuPreviewEnabled_; }
        bool MenuScreenPreviewEnabled() const { return menuScreenPreviewEnabled_; }
        float ScreenDistanceOffset() const { return screenDistanceOffset_; }
        float ScreenScale() const { return screenScale_; }
        bool CurvedScreenEnabled() const { return curvedScreenEnabled_; }
        float ScreenCurvature() const { return screenCurvature_; }
        bool TransparencyEnabled() const { return transparencyEnabled_; }
        bool MapLightShowEnabled() const { return mapLightShowEnabled_; }
        bool EnvironmentOverrideEnabled() const { return environmentOverrideEnabled_; }
        bool EnvironmentMotionEnabled() const { return environmentMotionEnabled_; }
        int ResolutionHeight() const { return resolutionHeight_; }

        void SetVideoEnabledByDefault(bool value);
        void SetMenuPreviewEnabled(bool value);
        void SetMenuScreenPreviewEnabled(bool value);
        void SetScreenDistanceOffset(float value);
        void SetScreenScale(float value);
        void SetCurvedScreenEnabled(bool value);
        void SetScreenCurvature(float value);
        void SetTransparencyEnabled(bool value);
        void SetMapLightShowEnabled(bool value);
        void SetEnvironmentOverrideEnabled(bool value);
        void SetEnvironmentMotionEnabled(bool value);
        void SetResolutionHeight(int value);

    private:
        Settings() = default;

        void Save();

        bool videoEnabledByDefault_ = true;
        bool menuPreviewEnabled_ = true;
        bool menuScreenPreviewEnabled_ = false;
        float screenDistanceOffset_ = 0.0f;
        float screenScale_ = 1.0f;
        bool curvedScreenEnabled_ = false;
        float screenCurvature_ = 0.35f;
        bool transparencyEnabled_ = false;
        bool mapLightShowEnabled_ = true;
        bool environmentOverrideEnabled_ = true;
        bool environmentMotionEnabled_ = true;
        int resolutionHeight_ = 720;
    };
}
