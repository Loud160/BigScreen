# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Cross-file release invariants that are easy to regress during upgrades."""

from __future__ import annotations

import json
import pathlib

if not __debug__:
    raise RuntimeError(
        "RepositoryInvariantTests must run without Python -O; optimized mode disables assertions")
import re
import sys


root = pathlib.Path(sys.argv[1]).resolve()
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build_script = (root / "scripts/build.ps1").read_text(encoding="utf-8")
ffmpeg_build = (root / "scripts/build-ffmpeg-lgpl.sh").read_text(encoding="utf-8")
ffmpeg_elf_audit = (root / "scripts/validate-ffmpeg-elf.ps1").read_text(
    encoding="utf-8")
runtime_fetch = (root / "scripts/fetch-downloader-runtime.ps1").read_text(encoding="utf-8")
copy_script = (root / "scripts/copy.ps1").read_text(encoding="utf-8")
download_manager_header = (
    root / "include/BigScreen/DownloadManager.hpp"
).read_text(encoding="utf-8")
download_manager_source = (root / "src/DownloadManager.cpp").read_text(
    encoding="utf-8")
frame_decoder_source = (root / "src/FrameDecoder.cpp").read_text(
    encoding="utf-8")
frame_decoder_facade = (root / "src/FrameDecoderFacade.cpp").read_text(
    encoding="utf-8")
error_manager_source = (root / "src/ErrorManager.cpp").read_text(
    encoding="utf-8")
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
chroma_detector_source = (root / "src/ChromaMapDetector.cpp").read_text(
    encoding="utf-8")
video_library_header = (
    root / "include/BigScreen/VideoLibrary.hpp"
).read_text(encoding="utf-8")
video_library_source = (root / "src/VideoLibrary.cpp").read_text(
    encoding="utf-8"
)
showcase_source = (root / "src/ShowcaseLauncher.cpp").read_text(
    encoding="utf-8"
)
showcase_menu_source = (root / "src/ShowcaseMenu.cpp").read_text(
    encoding="utf-8"
)
showcase_timeline_source = (root / "src/UpDownShowcaseTimeline.cpp").read_text(
    encoding="utf-8"
)
screen_surface_source = (root / "src/ScreenSurface.cpp").read_text(
    encoding="utf-8"
)
qpm = json.loads((root / "qpm.json").read_text(encoding="utf-8"))
qpm_shared = json.loads((root / "qpm.shared.json").read_text(encoding="utf-8"))

# Big Screen's outbound license, section 7 terms, inbound contribution grant,
# and DCO certification are deliberately separate mechanisms. Keep the release
# repository and the QMOD notice staging aligned so a documentation edit cannot
# silently turn a binary release into a source/license compliance failure.
license_text = (root / "LICENSE").read_text(encoding="utf-8")
additional_terms = (root / "LICENSE-ADDITIONAL-TERMS.md").read_text(
    encoding="utf-8")
notice_text = (root / "NOTICE").read_text(encoding="utf-8")
contributing = (root / "CONTRIBUTING.md").read_text(encoding="utf-8")
inbound_license = (root / "INBOUND_LICENSE.md").read_text(encoding="utf-8")
dco = (root / "DCO.txt").read_text(encoding="utf-8")
third_party_notices = (root / "THIRD_PARTY_NOTICES.md").read_text(
    encoding="utf-8")
readme = (root / "README.md").read_text(encoding="utf-8")
mod_template = json.loads((root / "mod.template.json").read_text(encoding="utf-8"))
stage_notices = (root / "scripts/stage-runtime-notices.ps1").read_text(
    encoding="utf-8")
create_qmod = (root / "scripts/createqmod.ps1").read_text(encoding="utf-8")
validate_mod_json = (root / "scripts/validate-modjson.ps1").read_text(
    encoding="utf-8")

assert "GNU GENERAL PUBLIC LICENSE" in license_text
assert "Version 3, 29 June 2007" in license_text
assert "END OF TERMS AND CONDITIONS" in license_text
assert "MIT License" not in license_text
for marker in (
    "GPLv3 section 7(b)",
    "GPLv3 section 7(c)",
    "Loud160 (AKA Whisp)",
    "interoperability permission",
    "Beat Saber",
    "Unity",
):
    assert marker in additional_terms
assert "splash screen" in additional_terms
assert "Loud160 (AKA Whisp)" in notice_text
assert "canonical repository" in notice_text
assert "https://github.com/Loud160/BigScreen" in notice_text
assert "https://github.com/Loud160/BigScreen" in additional_terms
assert mod_template["author"] == "Loud160 (AKA Whisp)"
assert qpm["info"]["url"] == "https://github.com/Loud160/BigScreen"
assert mod_template["packageVersion"] == "1.40.8_7379"
assert qpm["version"] == mod_template["version"]
assert qpm_shared["config"]["version"] == mod_template["version"]
resolved_dependencies = {
    entry["dependency"]["id"]: entry["version"]
    for entry in qpm_shared["restoredDependencies"]
}
for dependency_id, dependency_version in {
    "beatsaber-hook": "6.4.2",
    "paper2_scotland2": "4.8.0",
    "bs-cordl": "4008.0.0",
    "songcore": "1.1.26",
    "bsml": "0.4.55",
    "custom-types": "0.18.4",
}.items():
    assert resolved_dependencies[dependency_id] == dependency_version
for packaging_guard in (
    "Programs/QPM/qpm.exe",
    '"packageVersion"',
    "mod.json dependencies do not match qpm.shared.json",
    "Run 'qpm qmod manifest'",
):
    assert packaging_guard in validate_mod_json
assert "source%20license-GPL--3.0--only-blue" in readme
for marker in (
    "GPL-3.0-only",
    "inbound MIT",
    "separate inbound MIT grant",
    "Signed-off-by:",
    "Developer Certificate of Origin 1.1",
    "does **not** create or grant the inbound MIT license",
):
    assert marker in contributing
assert "Copyright (c) the respective contributors to Big Screen" in inbound_license
assert "Permission is hereby granted, free of charge" in inbound_license
assert "Developer's Certificate of Origin 1.1" in dco
assert "changing it is not allowed" in dco

for runtime_notice in (
    "BIGSCREEN-LICENSE.txt",
    "BIGSCREEN-ADDITIONAL-TERMS.md",
    "BIGSCREEN-NOTICE.txt",
    "OPENSSL-APACHE-2.0.txt",
    "SQLITE-PUBLIC-DOMAIN.txt",
):
    assert runtime_notice in stage_notices
    assert runtime_notice in create_qmod
for preserved_third_party_term in (
    "MIT License",
    "Apache License 2.0",
    "Mozilla Public License 2.0",
    "GNU Lesser General Public License 2.1 or later",
    "Unlicense",
    "zlib License",
):
    assert preserved_third_party_term in third_party_notices

# Every first-party source/build file receives the base SPDX identifier plus a
# human-readable pointer to Big Screen's section 7 terms. JSON and Markdown do
# not permit or need source comments. Vendored and generated dependencies are
# intentionally outside these roots and must never receive Big Screen headers.
first_party_patterns = {
    root / ".github/workflows": ("*.yml", "*.yaml"),
    root / "cmake": ("*.cmake",),
    root / "include": ("*.h", "*.hpp"),
    root / "src": ("*.c", "*.cpp", "*.h", "*.hpp"),
    root / "python": ("*.py",),
    root / "scripts": ("*.ps1", "*.sh", "*.py"),
    root / "tests": ("CMakeLists.txt", "*.c", "*.cpp", "*.h", "*.hpp", "*.py"),
    root / "tools": ("*.c", "*.cpp", "*.h", "*.hpp", "*.py", "*.ps1", "*.sh"),
}
first_party_files: set[pathlib.Path] = {
    root / "CMakeLists.txt",
    root / "Build-And-Deploy.bat",
}
for directory, patterns in first_party_patterns.items():
    for pattern in patterns:
        first_party_files.update(directory.rglob(pattern))
assert first_party_files
for source_file in sorted(first_party_files):
    source_text = source_file.read_text(encoding="utf-8")
    source_preamble = "\n".join(source_text.splitlines()[:12])
    assert source_preamble.count("SPDX-License-Identifier: GPL-3.0-only") == 1, source_file
    assert "LICENSE-ADDITIONAL-TERMS.md" in source_preamble, source_file

for vendored_sample in (
    root / "extern/quickjs-ng/source/quickjs.c",
    root / "extern/ffmpeg-lgpl/include/libavcodec/avcodec.h",
    root / "extern/ffmpeg-lgpl-9.0.1/include/libavcodec/avcodec.h",
):
    if vendored_sample.exists():
        assert "Loud160 (AKA Whisp)" not in vendored_sample.read_text(
            encoding="utf-8", errors="ignore")

# This file is primarily assert-based by design, so optimized Python must not
# be able to turn the release audit into a silent pass.
assert __debug__

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
assert 'runtime_tag="44"' in ffmpeg_build
assert 'runtime_tag="9"' in ffmpeg_build
assert 'build_suffix="-bigscreen${runtime_tag}"' in ffmpeg_build
assert 'symbol_namespace="BIGSCREEN${runtime_tag}"' in ffmpeg_build
for required_media_option in (
    "--enable-jni",
    "--enable-mediacodec",
    "--enable-decoder=h264",
    "--enable-decoder=h264_mediacodec",
    "--enable-decoder=hevc_mediacodec",
    "--enable-decoder=vp8",
    "--enable-decoder=vp8_mediacodec",
    "--enable-decoder=vp9",
    "--enable-decoder=vp9_mediacodec",
    "--enable-demuxer=matroska",
):
    assert required_media_option in ffmpeg_build
for required_config_gate in (
    "CONFIG_JNI",
    "CONFIG_MEDIACODEC",
    "CONFIG_H264_DECODER",
    "CONFIG_H264_MEDIACODEC_DECODER",
):
    assert required_config_gate in ffmpeg_build
for runtime_tag in ("44", "9"):
    assert f"libavformat-bigscreen{runtime_tag}.so" in cmake
    assert f"libavcodec-bigscreen{runtime_tag}.so" in cmake
    assert f"libavutil-bigscreen{runtime_tag}.so" in cmake
    assert f"libswscale-bigscreen{runtime_tag}.so" in cmake
    assert f"bigscreen_ffmpeg{runtime_tag}_backend SHARED" in cmake
    assert f"libbigscreen-ffmpeg{runtime_tag}-backend.so" in (
        root / "scripts/createqmod.ps1").read_text(encoding="utf-8")
assert "TARGET_OBJECTS:bigscreen_ffmpeg" not in cmake
assert '"validate-ffmpeg-elf.ps1"' in build_script
assert "BIGSCREEN44_LIB" in ffmpeg_elf_audit
assert "BIGSCREEN9_LIB" in ffmpeg_elf_audit
assert "OtherNamespace" in ffmpeg_elf_audit
assert "CreateFrameDecoder44Backend" in ffmpeg_elf_audit
assert "CreateFrameDecoder9Backend" in ffmpeg_elf_audit
assert "CreateFrameDecoder44Backend" in frame_decoder_facade
assert "CreateFrameDecoder9Backend" in frame_decoder_facade
assert "Settings::Instance().UseFfmpeg9()" in frame_decoder_facade
assert "BIGSCREEN_FFMPEG_BACKEND_EXPORT" in frame_decoder_header
assert 'ReadBool(document, "useFfmpeg9", false)' in settings_source
assert 'Replace(document, "useFfmpeg9", useFfmpeg9_)' in settings_source
assert '"Use FFmpeg 9"' in settings_menu_source
assert 'ReadBool(\n            document, "hardwareDecodingEnabled", false)' in settings_source
assert 'Replace(document, "hardwareDecodingEnabled", hardwareDecodingEnabled_)' in settings_source
assert '"Hardware Video Decoding"' in settings_menu_source
assert '"Experimental: uses the Quest' in settings_menu_source
for panel_transform_key in (
    "performancePanelPositionX",
    "performancePanelPositionY",
    "performancePanelPositionZ",
    "performancePanelRotationX",
    "performancePanelRotationY",
    "performancePanelRotationZ",
):
    assert panel_transform_key in settings_source
assert "SetPerformancePanelPlacement" in performance_panel_source
assert "ResetPerformancePanelPlacement" in performance_panel_source
assert 'performanceParent, "↻"' not in settings_menu_source
assert 'diagnosticsParent, "↻"' in settings_menu_source
assert "ApplyDisplaySettingsAndRefreshPreview();" in settings_menu_source
assert workflow.count("BIGSCREEN_FFMPEG_VERSION=") == 2
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
for required_file in (
    "lib/libpython3.14.so",
    "lib/libssl_python.so",
    "lib/libcrypto_python.so",
    "lib/libsqlite3_python.so",
    "lib/python3.14/os.py",
    "include/python3.14/Python.h",
):
    assert f'"{required_file}"' in runtime_fetch
for runtime_only_library in (
    "libssl_python.so", "libcrypto_python.so", "libsqlite3_python.so"):
    assert f'Join-Path $nativeLibraryStage $runtimeOnlyLibrary' in runtime_fetch
assert "wslpath -a" in build_script
assert "SHA256SUMS" in build_script
assert "-DBIGSCREEN_UP_DOWN_SHOWCASE=ON" in build_script

# Decoder shutdown changes the wait predicate under the waiter's mutex, and
# ordinary FFmpeg EOF/EAGAIN remains distinct from a hard worker failure.
for function_name in ("FrameDecoder::Close()", "FrameDecoder::SetWorkerError"):
    function = frame_decoder_source.split(function_name, 1)[1]
    function = function.split("\n    }", 1)[0]
    assert "std::scoped_lock lock(requestMutex_)" in function
    assert function.index("lock(requestMutex_)") < function.index("stopWorker_ = true")
assert "AVERROR(EAGAIN)" in frame_decoder_source
assert "av_strerror" in frame_decoder_source
assert 'CodecPolicy{"H.264", "h264", "h264_mediacodec", CoreLogic::VideoCodecKind::H264}' in frame_decoder_source
assert 'CodecPolicy{"H.265/HEVC", nullptr, "hevc_mediacodec", CoreLogic::VideoCodecKind::Hevc}' in frame_decoder_source
assert 'CodecPolicy{"VP8", "vp8", "vp8_mediacodec", CoreLogic::VideoCodecKind::Vp8}' in frame_decoder_source
assert 'CodecPolicy{"VP9", "vp9", "vp9_mediacodec", CoreLogic::VideoCodecKind::Vp9}' in frame_decoder_source
assert "std::once_flag registration" in frame_decoder_source
assert "RegisterJavaVmForThisRuntime(javaVm)" in frame_decoder_source
assert "const auto applyUserVideoControls" in playback_source
assert playback_source.count("applyUserVideoControls();") >= 2
assert "config_->letterboxTransparent =" in playback_source
assert "bool PlaybackSession::MapperScreenPresentationActive() const" in playback_source
assert "chromaMapDetected_ &&" in playback_source
assert "baseConfig_->hasMapperScreenGeometry" in playback_source
assert "bool PlaybackSession::MapperEnvironmentPresentationActive() const" in playback_source
assert "static_cast<AVPixelFormat>(decoded_->format)" in frame_decoder_source
assert "AV_FRAME_CROP_UNALIGNED" in frame_decoder_source
assert "sws_setColorspaceDetails" in frame_decoder_source
assert '#include "main.hpp"' in frame_decoder_facade
assert "return modloader_jvm;" in frame_decoder_facade
assert 'dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs")' not in frame_decoder_facade
assert "ReopenWithSoftwareAfterHardwareFailure" in frame_decoder_facade
assert 'return UsingHardwareDecoder() ? "hardware" : "software";' in frame_decoder_facade
close_and_retain = frame_decoder_facade.split(
    "void FrameDecoder::CloseAndRetainBackendMetrics()", 1
)[1].split("bool FrameDecoder::ReopenWithSoftwareAfterHardwareFailure", 1)[0]
assert close_and_retain.index("backend_->Close();") < close_and_retain.index(
    "accumulatedWorkerCpuMilliseconds_ += backend_->WorkerCpuMilliseconds();"
)
assert close_and_retain.index(
    "accumulatedWorkerCpuMilliseconds_ += backend_->WorkerCpuMilliseconds();"
) < close_and_retain.index("backend_.reset();")
assert "retainedPeakDecodeMilliseconds_" in close_and_retain
assert "accumulatedBufferAllocations_ += backend_->BufferAllocations();" in close_and_retain
assert 'if(!backend_)\n            return "none";' in frame_decoder_facade

# Downloader state transitions must be serialized and a C++ terminal failure
# must not be overwritten by a stale on-disk active state.
assert "std::mutex startMutex_" in download_manager_header
assert download_manager_source.count(
    "std::scoped_lock startLock(startMutex_)") >= 3
set_failure = download_manager_source.split(
    "void DownloadManager::SetFailure(std::string message)", 1
)[1].split("\n    }", 1)[0]
assert "std::filesystem::remove(statusPath_, removeError)" in set_failure
assert set_failure.index("remove(statusPath_, removeError)") < set_failure.index(
    "snapshot_.state = DownloadState::Failed")
assert "ignoreStatusFile_ = true;" in set_failure
refresh_snapshot = download_manager_source.split(
    "void DownloadManager::RefreshSnapshotFromDiskLocked()", 1
)[1].split("void DownloadManager::SetFailure", 1)[0]
assert "if(ignoreStatusFile_)" in refresh_snapshot
assert "PyErr_Print" not in download_manager_source
assert "PyEval_GetBuiltins" not in download_manager_source
assert "PyImport_ImportModule(\"builtins\")" in download_manager_source

# Both user-facing download entry points must consume the probe's exact tier
# list. Backend support alone is insufficient: otherwise the old single button
# silently sends DownloadRequest's default 1080p value for every source.
assert "std::vector<int> availableHeights" in download_manager_header
assert "request.requestedHeight = height" in library_menu_source
assert "request.requestedHeight = height" in selection_toggle_source
assert "request.maximumSourceFps = Settings::Instance().PlaybackFpsLimit()" in (
    library_menu_source
)
assert "request.maximumSourceFps = Settings::Instance().PlaybackFpsLimit()" in (
    selection_toggle_source
)
assert "download.availableHeights" in library_menu_source
assert "snapshot.availableHeights" in selection_toggle_source
assert "verifiedAvailableHeights" in download_manager_source
assert "validatedCompletedTransfer" in library_menu_source
# The side editor deliberately uses compact one-row labels beside the video
# thumbnail; the song-selection modal has enough width for the longer form.
assert 'std::to_string(height) + "p"' in library_menu_source
assert '"DOWNLOAD " + std::to_string(height) + "p"' in library_menu_source
assert '"DOWNLOAD " +' in selection_toggle_source
assert "1440p requires Hardware Video Decoding" in library_menu_source
assert "1440p requires Hardware Video Decoding" in selection_toggle_source
assert "Your local file will not be deleted" in library_menu_source
assert "Local files are never deleted by replacement" in selection_toggle_source
assert "class StagedFileReplacement final" in download_manager_source
assert "IncomingSibling(finalPath)" in download_manager_source
assert download_manager_source.index("videoReplacement.Promote()") < (
    download_manager_source.index("VideoLibrary::Instance().CommitDownload(")
)

# The optional demo is fetched rather than redistributed. Its map revision is
# immutable, extraction is bounded/contained, both required mods are checked by
# live SongCore capability, and launch remains on Beat Saber's normal Solo path.
assert 'ShowcaseMapKey = "11cf8"' in showcase_source
assert '"2aa85aad10e124eb674d18d49251bc94ee1a4283"' in showcase_source
assert '"https://youtu.be/oJa7Kr7_9dw"' in showcase_source
assert 'CapabilityAvailable("Chroma")' in showcase_source
assert 'CapabilityAvailable("Noodle Extensions")' in showcase_source
assert "StartMapPackage" in download_manager_header
assert "MapPackageScript" in download_manager_source
for map_install_guard in (
    "maximumArchiveBytes",
    "maximumExpandedBytes",
    "maximumEntries",
    "os.path.commonpath",
    "stat.S_IFLNK",
    "api.beatsaver.com",
    "r2cdn.beatsaver.com",
    "'lawless'",
    "'expertplus'",
):
    assert map_install_guard in download_manager_source
assert "SongCore::API::Loading::RefreshSongs(false)" in showcase_source
assert "PresentFlowCoordinatorOrAskForTutorial" in showcase_source
assert "LevelSelectionFlowCoordinator_State::New_ctor(\n                noCategory" in showcase_source
# Once Solo is already visible, SelectLevel only records a future selection
# and does not reproduce the row callback needed to present the level detail.
# The launcher must use Beat Saber's real row-selection path so the showcase
# starts without requiring the player to click the map manually.
assert "HandleLevelCollectionViewControllerDidSelectLevel" in showcase_source
assert "collection->SelectLevel(level)" not in showcase_source
assert "solo->StartLevel(nullptr, false)" in showcase_source
assert '"Play Big Screen Showcase"' in settings_menu_source
assert "ShowcaseReadiness ShowcaseLauncher::Readiness() const" in showcase_source
assert "bool ShowcaseLauncher::DownloadMap" in showcase_source
assert "bool ShowcaseLauncher::DownloadVideo" in showcase_source
assert "bool ShowcaseLauncher::Play" in showcase_source
assert '"Download Map"' in showcase_menu_source
assert '"Download Video"' in showcase_menu_source
assert '"Recheck Map"' in showcase_menu_source
assert '"Play Showcase"' in showcase_menu_source
assert "ShowcaseMenu::Show()" in showcase_menu_source
showcase_show = showcase_menu_source.split(
    "void ShowcaseMenu::Show()", 1
)[1].split("void ShowcaseMenu::Tick()", 1)[0]
assert ".DownloadMap(" not in showcase_show
assert ".DownloadVideo(" not in showcase_show
assert "DismissTransientUi()" in showcase_show
assert "warningModal_->Hide()" in showcase_menu_source
assert "object->SetActive(false)" in showcase_menu_source
assert "transitionFrames_ < 12" in showcase_source
assert '"Return to Big Screen"' not in main_source
assert "RequestReturnToBigScreen" not in main_source
assert "WaitingForResultsDismissal" not in showcase_source
assert "RestoreShowcasePageAfterGameplay" not in showcase_source

# Showcase glass remains deterministic, programmatic, and isolated from the
# normal video surface. Only the authored 2:03 damage sequence uses it. Its
# final break freezes the displayed GPU texture and spreads pieces forward as
# they fall; ordinary floating screens retain their established safe paths.
for fracture_symbol in (
    "SeededFractureRandom",
    "GenerateFracturePattern",
    "GenerateRadialCracks",
    "PartitionFractureRevealGroups",
    "MaximumFracturePieces",
):
    assert fracture_symbol in core_logic
assert "freezeOnShatter" in core_logic
assert "FractureShardTransform" in core_logic
assert "shardTransformCount" in core_logic
assert "fractureShardTranslations_" in screen_surface_source
assert "UnityEngine::Graphics::CopyTexture" in screen_surface_source
assert "CoreLogic::FracturePhase::Shattered" in showcase_timeline_source
assert "ImpactSequence" in showcase_timeline_source
assert "RevealedImpactCount" in showcase_timeline_source
assert "gravityDistance = 110.0f" in showcase_timeline_source
assert "forwardScatterDistance" in core_logic
assert "forwardScatterDistance = 42.0f" in showcase_timeline_source
assert "randomizedForwardAmount" in screen_surface_source

# Error timestamps are generated from thread-safe platform APIs because worker
# failures can reach the logger concurrently with game-thread failures.
assert "localtime_s" in error_manager_source
assert "localtime_r" in error_manager_source
assert "std::localtime" not in error_manager_source

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
assert power_benchmark_source.count("decode_method") >= 2
assert "Archived older power benchmark schema" in power_benchmark_source
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
assert "if(!playback.SynchronizedAudioReady(previewSongTime_))" in preview_audio_start
assert "CrossfadeTo(" in preview_audio_start
assert preview_audio_start.index("if(!playback.SynchronizedAudioReady(previewSongTime_))") < \
    preview_audio_start.index("CrossfadeTo(")
assert preview_audio_start.index("BeginLibraryPreviewMeasurement") < \
    preview_audio_start.index("CrossfadeTo(")
assert "StartSelectedPreview();" not in preview_audio_start.split("CrossfadeTo(", 1)[1]
assert "shouldDeleteMapperDownload" in library_menu_source
assert "library.DeleteMapperDownload(levelId)" in library_menu_source
assert "descriptor.hasUserOverride || descriptor.hasMapperDownload" in library_menu_source
assert "const MapVideoConfig* EditorTimingConfig(" in library_menu_source
assert "if(descriptor.mapperDefinition)" in library_menu_source
assert library_menu_source.count("const auto* timing = EditorTimingConfig(descriptor);") >= 2
assert "offset_ = timing ? timing->offsetSeconds : 0.0;" in library_menu_source
assert "std::vector<UnityEngine::GameObject*> timingRows_;" in library_menu_header
assert "mapperTimingWaitingForVideo" in library_menu_source
assert "This timing comes from the map author's Cinema configuration" in library_menu_source
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

# A missing primary manifest after an interrupted replace is a recovery case
# when a rotating backup exists, not a first-run empty library. Managed deletes
# must apply the same leaf-name boundary as managed reads.
assert "const bool primaryExists" in video_library_source
assert "const bool anyBackup = std::any_of" in video_library_source
assert "genuine first run, not a recovery event" in video_library_source
assert "bool RemoveManagedFile(" in video_library_source
for direct_delete in (
    "videoPath_ / previous->fileName",
    "videoPath_ / found->second.user->fileName",
    "videoPath_ / found->second.mapper->fileName",
):
    assert direct_delete not in video_library_source
assert '".corrupt-" + std::to_string(stamp)' in settings_source
assert "Configuration::getConfigFilePath" in settings_source

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

# Native menu singletons outlive MenuCore's Unity hierarchy. Every retained
# menu must forget its old objects before replacement controllers are built,
# and no menu singleton may be refreshed when that UnityW-backed flow is gone.
for menu_name in (
    "SettingsMenu", "VideoLibraryMenu", "StorageMaintenanceMenu",
    "LocalVideoBrowserMenu",
):
    assert f"{menu_name}::Instance().ForgetUi();" in menu_flow_source
assert "UnityW<MenuFlowCoordinator> activeMenuFlow" in menu_flow_source
assert "auto* coordinator = activeMenuFlow.ptr();" in menu_flow_source
assert main_source.count("if(BigScreen::IsBigScreenMenuActive())") >= 2
assert "void SettingsMenu::ForgetUi()" in settings_menu_source
assert "errorModal_ = nullptr" in settings_menu_source
assert "errorModalText_ = nullptr" in settings_menu_source
assert "void VideoLibraryMenu::ForgetUi()" in library_menu_source
assert "void StorageMaintenanceMenu::ForgetUi()" in (
    root / "src/StorageMaintenanceMenu.cpp"
).read_text(encoding="utf-8")
assert "void LocalVideoBrowserMenu::ForgetUi()" in local_browser_source

# A stock song-screen close owns only the task it started. Video Library jobs
# continue when the stock detail view is hidden.
assert "ownedDownloadLevelId_" in selection_toggle_source
song_hidden = selection_toggle_source.split(
    "void SelectionVideoToggle::SongSelectionHidden()", 1
)[1].split("void SelectionVideoToggle::", 1)[0]
assert "download.levelId == ownedDownloadLevelId_" in song_hidden
forget_selection = selection_toggle_source.split(
    "void SelectionVideoToggle::ForgetUi()", 1
)[1].split("void SelectionVideoToggle::", 1)[0]
assert "selectedLevel_ = nullptr" in forget_selection

# Misc-tab visibility changes redraw its sliders immediately, and a queued
# error cannot be consumed before the settings modal has been constructed.
assert "else if(selectedTab_ == 4)" in settings_menu_source
refresh_status = settings_menu_source.split(
    "void SettingsMenu::RefreshDownloaderStatus()", 1
)[1]
assert refresh_status.index("if(errorModal_ && errorModalText_)") < \
    refresh_status.index("TakePendingDialog()")

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

# BSML's Full floating-screen handle is not reliably draggable on the
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
assert "BodyRowCount = 8.0f" in performance_panel_source
assert 'row(rightRows_[0], "Decoder"' in performance_panel_source
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

# Chroma detection parses untrusted beatmap JSON on the Unity thread. It uses
# an explicit value stack to avoid recursive native-stack exhaustion and
# caches unchanged map metadata so one selection is not repeatedly reparsed.
assert "std::vector<const rapidjson::Value*> pending" in chroma_detector_source
assert "pending.push_back" in chroma_detector_source
assert "std::unordered_map<std::string, CacheEntry> cache" in chroma_detector_source
assert "last_write_time" in chroma_detector_source
assert "is_regular_file(entryError)" in chroma_detector_source

# The intentionally deferred large-file split must remain visible in both the
# backlog and release review until it is completed and these checks are updated.
future = (root / "docs/FUTURE_WORK.md").read_text(encoding="utf-8")
checklist = (root / "docs/RELEASE_CHECKLIST.md").read_text(encoding="utf-8")
review_resolution = (
    root / "docs/CODE_REVIEW_RESOLUTION.md"
).read_text(encoding="utf-8")
assert "TODO: Split oversized source files" in future
assert "large-file refactor" in checklist.lower()
assert "Reviewed and intentionally not changed" in review_resolution
assert "Deferred with explicit release tracking" in review_resolution

print("Repository toolchain, licensing, persistence, and deferred-work invariants passed.")
