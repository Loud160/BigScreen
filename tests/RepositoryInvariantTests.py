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
settings_header = (root / "include/BigScreen/Settings.hpp").read_text(encoding="utf-8")
settings_source = (root / "src/Settings.cpp").read_text(encoding="utf-8")
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

# The intentionally deferred large-file split must remain visible in both the
# backlog and release review until it is completed and these checks are updated.
future = (root / "docs/FUTURE_WORK.md").read_text(encoding="utf-8")
checklist = (root / "docs/RELEASE_CHECKLIST.md").read_text(encoding="utf-8")
assert "TODO: Split oversized source files" in future
assert "large-file refactor" in checklist.lower()

print("Repository toolchain, licensing, persistence, and deferred-work invariants passed.")
