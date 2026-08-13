"""Cross-file release invariants that are easy to regress during upgrades."""

from __future__ import annotations

import json
import pathlib
import re
import sys


root = pathlib.Path(sys.argv[1]).resolve()
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
ffmpeg_build = (root / "scripts/build-ffmpeg-lgpl.sh").read_text(encoding="utf-8")
runtime_fetch = (root / "scripts/fetch-downloader-runtime.ps1").read_text(encoding="utf-8")
copy_script = (root / "scripts/copy.ps1").read_text(encoding="utf-8")
settings_header = (root / "include/BigScreen/Settings.hpp").read_text(encoding="utf-8")
settings_source = (root / "src/Settings.cpp").read_text(encoding="utf-8")
settings_menu_source = (root / "src/SettingsMenu.cpp").read_text(encoding="utf-8")
library_menu_header = (
    root / "include/BigScreen/VideoLibraryMenu.hpp"
).read_text(encoding="utf-8")
library_menu_source = (root / "src/VideoLibraryMenu.cpp").read_text(
    encoding="utf-8"
)
frame_decoder_header = (root / "include/BigScreen/FrameDecoder.hpp").read_text(
    encoding="utf-8"
)
playback_header = (root / "include/BigScreen/PlaybackSession.hpp").read_text(
    encoding="utf-8"
)
playback_source = (root / "src/PlaybackSession.cpp").read_text(encoding="utf-8")
core_logic = (root / "include/BigScreen/CoreLogic.hpp").read_text(encoding="utf-8")
performance_panel_source = (root / "src/PerformancePanel.cpp").read_text(
    encoding="utf-8"
)
main_source = (root / "src/main.cpp").read_text(encoding="utf-8")
menu_flow_source = (root / "src/MenuFlowCoordinator.cpp").read_text(
    encoding="utf-8"
)
qpm = json.loads((root / "qpm.json").read_text(encoding="utf-8"))
qpm_shared = json.loads((root / "qpm.shared.json").read_text(encoding="utf-8"))

# One pinned Android toolchain must drive QPM metadata, CI, and FFmpeg.
ndk_revision = "27.3.13750724"
assert qpm["workspace"]["ndk"] == f"^{ndk_revision}"
assert qpm_shared["config"]["workspace"]["ndk"] == f"^{ndk_revision}"
workflow = (root / ".github/workflows/build-ndk.yml").read_text(encoding="utf-8")
assert ndk_revision in workflow
assert "android-ndk-r27d" in ffmpeg_build

# The release configuration must remain LGPL-only and keep both benchmarked
# versions reproducible until on-device evidence selects the replacement.
for forbidden in ("--enable-gpl", "--enable-version3", "--enable-nonfree"):
    assert forbidden not in re.sub(r"#[^\n]*", "", ffmpeg_build)
for version in ("4.4.8", "9.0.1"):
    assert version in ffmpeg_build
    assert version in cmake
assert "CONFIG_VERSION3" in ffmpeg_build
for warning in ("-Wall", "-Wextra", "-Wpedantic"):
    assert warning in cmake

# CPython headers, linked SONAME, staged archive, and runtime manifest must all
# describe the same minor/patch release.
assert '$pythonVersion = "3.14.7"' in runtime_fetch
assert "python-3.14.7/prefix" in cmake
assert "libpython3.14.so" in cmake
for python_library in (
    "libpython3.14.so",
    "libssl_python.so",
    "libcrypto_python.so",
    "libsqlite3_python.so",
):
    assert f"${{BIGSCREEN_PYTHON_PREFIX}}/lib/${{BIGSCREEN_PYTHON_LIBRARY}}" in cmake
    assert python_library in cmake
assert "No authoritative source was found for required runtime library" in copy_script
assert '$packagedDependency = Join-Path "extern/libs" $fileName' in copy_script
assert '$buildLibraryStage = Join-Path $repositoryRoot "build"' in runtime_fetch
assert "-Destination $buildLibraryStage" in runtime_fetch

# A stale copy in the opposite Scotland2 phase loads Big Screen twice and
# initializes CPython twice, which aborts Beat Saber during startup.
assert 'Modloader/mods/$fileName' in copy_script
assert 'Modloader/early_mods/$fileName' in copy_script
assert copy_script.count("adb shell rm -f --") == 2

# Every per-screen field must be both loaded and written. Reset deliberately
# reconstructs Settings{} so new member initializers remain the defaults list.
screen_suffixes = (
    "AdvancedControls", "Transparency", "Distance", "Horizontal", "Vertical",
    "Tilt", "Scale", "Curved", "Curvature", "MaintainAspect", "ScreenRoll",
    "VideoRotation", "VideoZoom", "VideoOffsetX", "VideoOffsetY", "VideoTilt",
    "StretchVideoToFit", "Undocked", "UndockedConfigured", "UndockedPositionX",
    "UndockedPositionY", "UndockedPositionZ", "UndockedRotationX",
    "UndockedRotationY", "UndockedRotationZ", "UndockedWidth", "UndockedHeight",
)
for suffix in screen_suffixes:
    assert f'prefix + "{suffix}"' in settings_source, suffix
assert "*this = Settings{};" in settings_source
assert "std::array<ScreenLayoutProfile, 5>" in settings_header
assert "int automaticPerformanceThreshold_ = 5;" in settings_header
assert "float automaticPerformanceResponseSeconds_ = 5.0f;" in settings_header
assert 'ReadInt(document, "automaticPerformanceThreshold", 5), 1, 15' in settings_source
assert 'ReadFloat(document, "automaticPerformanceResponseSeconds", 5.0f)' in settings_source
assert '"automaticPerformanceResponseSeconds"' in settings_source
assert '"Frame Rate Loss Trigger"' in settings_menu_source
assert '"Scaling Response Time"' in settings_menu_source
assert "Settings::Instance().AutomaticPerformanceResponseSeconds()" in playback_source

# Menu previews keep references across asynchronous audio loading, video
# decoder restarts, Quest focus/recording transitions, and song crossfades.
# Raw pointers or truth tests on their underlying addresses can dereference a
# Unity object after m_CachedPtr has been cleared and eject the whole mod menu.
for declaration in (
    "UnityW<GlobalNamespace::SongPreviewPlayer> songPreviewPlayer_",
    "UnityW<UnityEngine::AudioClip> previewAudioClip_",
    "UnityW<UnityEngine::AudioSource> previewAudioSource_",
):
    assert declaration in library_menu_header
for unsafe_condition in (
    "if(previewAudioClip_)",
    "if(!previewAudioClip_",
    "if(previewAudioSource_",
    "if(!previewAudioSource_",
):
    assert unsafe_condition not in library_menu_source, unsafe_condition
assert "RecoverInvalidPreviewAudio(\"menu update\")" in library_menu_source
assert "UnityW<UnityEngine::AudioClip>::isAlive" in library_menu_source

# Missed-video-frame statistics must use gaps between timestamps that actually
# reached Unity. Worker deadline sampling mistakes harmless thread scheduling
# jitter for visible loss, while raw upload totals mistake source-frame reuse.
assert "PresentedFrameIntervals" in core_logic
assert "lastUploadedPresentationSeconds_" in playback_header
assert "deliveredPresentedFrames_" in playback_header
assert "missedPresentedFrames_" in playback_header
assert "PresentedFrameIntervals(" in playback_source
assert "Missed Frames \" << missedFrames" in playback_source
assert "std::setprecision(2) << missedPercent" in playback_source
assert "PeakDecodeMilliseconds" in frame_decoder_header
assert "ResetPeakDecodeMilliseconds" in playback_source
assert "AutomaticPerformanceHistory automaticPerformanceHistory_" in playback_header
assert "ApplyAutomaticPerformanceRecovery(mediaTime)" in playback_source
assert "automaticPerformanceHistory_.RecordReduction" in playback_source
assert "automaticPerformanceHistory_.CommitRecovery" in playback_source
assert "<color=#AEBAC8>Missed Frames</color>" not in performance_panel_source
assert "<color=#AEBAC8>Frames Skipped</color>" in performance_panel_source
assert "<color=#AEBAC8>Video FPS Average</color>" in performance_panel_source
assert "<color=#AEBAC8>Frame Rate Loss</color>" in performance_panel_source
assert "self->____levelBar->get_transform()" in main_source
assert "UnityEngine::Vector2{0.0f, -36.0f}" not in main_source
assert "<color=#AEBAC8>Decode Peak</color>" in performance_panel_source
assert "SetGameplayLastNoteTime" in playback_header
assert "ShouldSampleGameplayFrame" in playback_source
assert "songTimeSeconds >= 10.0" in core_logic
assert "gameplayFrameSamplingFinished_" in playback_source
assert "BeatmapObjectSpawnController_Start" in main_source
assert "CreateResultsPerformancePanel" in main_source
for obsolete_counter in (
    "expectedFrameAccumulator_",
    "expectedPresentedFrames_",
    "diagnosticsWindowPresentedFrames_",
    "recentMissedFramePercent_",
    "expectedFrameDeadlines_",
    "pendingDiagnosticsRequestVersion_",
    "LatestHandledRequestVersion",
):
    assert obsolete_counter not in playback_header
    assert obsolete_counter not in playback_source

# BSML 0.4.43's Full floating-screen handle is not reliably draggable on the
# Quest. Keep the proven Top handler, expand its hit volume over the panel, and
# leave status text wrapping enabled so startup messages cannot be clipped.
assert "set_HandleSide(BSML::Side::Top)" in performance_panel_source
assert "set_HandleSide(BSML::Side::Full)" not in performance_panel_source
assert "ExpandNativeHandleAcrossPanel(screen_)" in performance_panel_source
assert "void PerformancePanel::TickInteraction() noexcept" in performance_panel_source
assert "anchor->get_rotation(), handle->_grabRot" in performance_panel_source
assert main_source.count("PerformancePanel::Instance().TickInteraction()") == 2
assert performance_panel_source.count("set_enableWordWrapping(true)") >= 2

# Beat Saber clears a retained flow's top controller during gameplay. The
# generated getter throws rather than returning null during that transition,
# so menu re-entry must restore the center through the direct setter.
assert "get_topViewController()" not in menu_flow_source
assert menu_flow_source.count("SetTopScreenViewController(") >= 2

# The intentionally deferred large-file split must remain visible in both the
# backlog and release review until it is completed and these checks are updated.
future = (root / "docs/FUTURE_WORK.md").read_text(encoding="utf-8")
checklist = (root / "docs/RELEASE_CHECKLIST.md").read_text(encoding="utf-8")
assert "TODO: Split oversized source files" in future
assert "large-file refactor" in checklist.lower()

print("Repository toolchain, licensing, persistence, and deferred-work invariants passed.")
