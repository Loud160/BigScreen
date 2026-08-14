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
selection_toggle_source = (root / "src/SelectionVideoToggle.cpp").read_text(
    encoding="utf-8"
)
local_browser_header = (
    root / "include/BigScreen/LocalVideoBrowserMenu.hpp"
).read_text(encoding="utf-8")
local_browser_source = (
    root / "src/LocalVideoBrowserMenu.cpp"
).read_text(encoding="utf-8")
video_library_header = (
    root / "include/BigScreen/VideoLibrary.hpp"
).read_text(encoding="utf-8")
video_library_source = (root / "src/VideoLibrary.cpp").read_text(
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
    "AdvancedControls", "LetterboxTransparency", "VideoOpacity", "Distance", "Horizontal", "Vertical",
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
assert "bool powerBenchmarkEnabled_ = false;" in settings_header
assert 'document, "powerBenchmarkEnabled", false' in settings_source
assert 'Replace(document, "powerBenchmarkEnabled", powerBenchmarkEnabled_)' in settings_source
assert "if(!started_)\n            return {};" in playback_source
assert '"Record Power Benchmark"' in settings_menu_source
power_benchmark_source = (root / "src/PowerBenchmark.cpp").read_text(
    encoding="utf-8"
)
assert "il2cpp_utils::Box(&boxedProperty)" in power_benchmark_source
assert "Quest battery probe succeeded" in power_benchmark_source
assert "ProbeBatteryAccessOnce();" in main_source
assert '"getIntProperty"' in power_benchmark_source
assert '"getLongProperty"' in power_benchmark_source
assert "power-benchmark-summary.csv" in power_benchmark_source
assert "power-benchmark-samples.csv" in power_benchmark_source
benchmark_tick = power_benchmark_source.split(
    "void PowerBenchmark::Tick", 1
)[1].split("void PowerBenchmark::Finish", 1)[0]
assert "ofstream" not in benchmark_tick
assert "create_directories" not in benchmark_tick
assert 'ReadInt(document, "automaticPerformanceThreshold", 5), 1, 15' in settings_source
assert 'ReadFloat(document, "automaticPerformanceResponseSeconds", 5.0f)' in settings_source
assert '"automaticPerformanceResponseSeconds"' in settings_source
assert '"Frame Rate Loss Trigger"' in settings_menu_source
assert '"Scaling Response Time"' in settings_menu_source
assert "Settings::Instance().AutomaticPerformanceResponseSeconds()" in playback_source
assert "context_ == PlaybackContext::LibraryPreview" in playback_source
assert "bool FirstFrameUploaded() const" in playback_header
assert "BeginLibraryPreviewMeasurement" in playback_header
assert "maySamplePlaybackFrame" in playback_source
assert "context_ == PlaybackContext::Gameplay\n                        ? gameplayLastNoteTime_\n                        : std::nullopt" in playback_source
assert "playWhenVideoReady_" in library_menu_source
assert "AdvanceSmoothedPreviewClock" in library_menu_source
assert "playbackControlsTickCounter_ >= 6" in library_menu_source
assert "downloadActive || periodicDownloadWasActive_" in library_menu_source
assert "IsLibraryPreviewActive()) return;" in selection_toggle_source
assert '"Preparing synchronized video preview..."' in library_menu_source
preview_audio_start = library_menu_source.split(
    "void VideoLibraryMenu::StartPreviewAudio()", 1
)[1].split("void VideoLibraryMenu::LoopPreviewPlayback()", 1)[0]
assert "playback.Tick(previewSongTime_)" in preview_audio_start
assert "if(!playback.FirstFrameUploaded())" in preview_audio_start
assert "CrossfadeTo(" in preview_audio_start
assert preview_audio_start.index("if(!playback.FirstFrameUploaded())") < \
    preview_audio_start.index("CrossfadeTo(")
assert preview_audio_start.index("BeginLibraryPreviewMeasurement") < \
    preview_audio_start.index("CrossfadeTo(")
assert "StartSelectedPreview();" not in preview_audio_start.split("CrossfadeTo(", 1)[1]
assert 'document.RemoveMember((prefix + "Transparency").c_str())' in settings_source

# User layout geometry must start from Big Screen's neutral canvas when Chroma
# ownership is disabled. The settings reset cannot repair a mapper position if
# playback keeps using that mapper position as its baseline.
assert "ResetPresentationToDefaults" in playback_source
assert "ResetPresentationToDefaults" in (
    root / "src/ScreenPreview.cpp"
).read_text(encoding="utf-8")

# The local-file picker is a center-screen browser. Potentially slow directory
# enumeration and FFmpeg compatibility probes stay off Unity's UI thread, and
# arbitrary shared-storage references remain explicitly user-owned.
assert "std::thread worker_" in local_browser_header
assert "ScanWorker" in local_browser_source
assert "InspectLocalVideo" in local_browser_source
assert '"Back One Folder"' in local_browser_source
assert "RebuildBreadcrumbs" in local_browser_source
assert "CreateClickableText" in local_browser_source
assert '"<color=#33FF5C>"' in local_browser_source
assert '"<color=#FF4040>"' in local_browser_source
assert "BrowserListWidth" in local_browser_source
assert "0.0f, 0.0f, 0.0f, 0.76f" in local_browser_source
assert '"Show File Browser"' in library_menu_source
assert "bool externalFile = false;" in video_library_header
assert 'sourceType == "externalFile"' in video_library_source
assert "!IsUserOwnedFile(*previous)" in video_library_source

# An animated dismissal from an already inactive HMUI controller leaves Beat
# Saber's parent flow with no responsive center screen. Both normal and error
# exits deliberately use the immediate dismissal overload.
assert menu_flow_source.count(
    "HMUI::ViewController::AnimationDirection::Horizontal,\n                nullptr,\n                true"
) >= 2
assert "BeginMenuReentryGuard" in menu_flow_source
assert "stableMainMenuFrames < 12" in menu_flow_source
assert "mainMenu->get_isActivated()" in menu_flow_source
assert "TickMenuReentryGuard();" in main_source

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
# Rows are individual TMP elements fed through one shared label/value
# template; the labels themselves are plain strings passed to that template.
assert '"Missed Frames"' not in performance_panel_source
assert '<color=#AEBAC8>{}</color>  <b>{}</b>' in performance_panel_source
assert '"Frames Skipped"' in performance_panel_source
assert '"Video FPS Average"' in performance_panel_source
assert '"Frame Rate Loss"' in performance_panel_source
assert "self->____levelBar->get_transform()" in main_source
assert "UnityEngine::Vector2{0.0f, -36.0f}" not in main_source
assert '"Decode Peak"' in performance_panel_source
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
# The panel is fixed-size: rows are height-locked LayoutElements and text
# auto-sizes down to fit instead of wrapping or growing the box. The count
# is title + subtitle + footer + the shared row factory (all rows flow
# through one createRow lambda).
assert performance_panel_source.count("set_enableAutoSizing(true)") >= 4
assert "set_childForceExpandHeight(false)" in performance_panel_source
assert "titleBorders_" in performance_panel_source
assert "instructionBorders_" in performance_panel_source
assert "BodyRowHeight = 7.0f" in performance_panel_source
assert "HeaderHeight + BodyHeight + FooterHeight" in performance_panel_source

# Song-screen global controls live on their own floating canvas below the
# center song panel, never over song metadata or OST pack buttons. Their
# calculated roots remain one centered visual group and labels stay on one line.
assert "ControlsScreenWidth = 150.0f" in selection_toggle_source
assert "ControlsScreenHeight = 12.0f" in selection_toggle_source
assert "BSML::FloatingScreen::CreateFloatingScreen" in selection_toggle_source
assert "SongHeaderControlY = 0.0f" in selection_toggle_source
assert "SongHeaderLabelControlGap = 2.5f" in selection_toggle_source
assert "SongHeaderGroupGap = 3.0f" in selection_toggle_source
assert "LayoutSelectorWidth = 50.0f" in selection_toggle_source
assert "ToggleRootWidth = 44.0f" in selection_toggle_source
assert "SongHeaderGroupSpan" in selection_toggle_source
assert "setting->text->set_enableWordWrapping(false)" in selection_toggle_source
assert "PlaceTopBarLayoutSelector(layoutUi_)" in selection_toggle_source
assert 'root->Find("NameText")' in selection_toggle_source
assert "TextAlignmentOptions::MidlineRight" in selection_toggle_source
assert "labelRect->set_sizeDelta({21.5f, 8.0f})" in selection_toggle_source
assert "pickerRect->set_sizeDelta({26.0f, 8.0f})" in selection_toggle_source
assert "setting->decButton" not in selection_toggle_source
assert "setting->incButton" not in selection_toggle_source
assert "void SelectionVideoToggle::BringHeaderControlsToFront()" in selection_toggle_source
assert selection_toggle_source.count("SetAsLastSibling()") >= 3
assert "void SelectionVideoToggle::LayoutSelectorChanged(float value)" in selection_toggle_source
assert selection_toggle_source.count("SettingsMenu::Instance().RefreshControls()") >= 3
assert "SelectionVideoToggle::Instance().ScreenLayoutPreferenceChanged()" in settings_menu_source

# The playback transport keeps its established edge padding while the visible
# gray rail is thin and the thumb occupies 80 percent of that rail.
assert "RectOffset::New_ctor(1, 1, 0, 0)" in library_menu_source
assert "scrubberTrackHeight = 2.5f" in library_menu_source
assert "scrubberTrackHeight * 0.80f" in library_menu_source

# Beat Saber can clear a retained flow's current main-stack controller during
# gameplay, and the generated getter throws rather than returning null during
# that transition. Re-entry must therefore leave HMUI's provided main stack
# alone. SetTopScreenViewController controls the physically separate top panel;
# assigning the center controller there corrupts ownership and can prevent Beat
# Saber's MainMenuViewController from returning after Big Screen is dismissed.
assert "get_topViewController()" not in menu_flow_source
assert "SetTopScreenViewController(" not in menu_flow_source
assert "HMUI retains the main-view stack" in menu_flow_source

# The intentionally deferred large-file split must remain visible in both the
# backlog and release review until it is completed and these checks are updated.
future = (root / "docs/FUTURE_WORK.md").read_text(encoding="utf-8")
checklist = (root / "docs/RELEASE_CHECKLIST.md").read_text(encoding="utf-8")
assert "TODO: Split oversized source files" in future
assert "large-file refactor" in checklist.lower()

print("Repository toolchain, licensing, persistence, and deferred-work invariants passed.")
