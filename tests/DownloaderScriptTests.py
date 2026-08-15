# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Syntax and user-facing HTTP error tests for embedded downloader scripts.

The production scripts live inside C++ raw strings so the Quest never depends
on loose executable source files. Extracting them here validates the exact text
that CPython receives, including mappings that are otherwise difficult to test
without deliberately provoking YouTube failures.
"""

import json
import pathlib
import sys
import tempfile
import types


def extract(source: str, name: str) -> str:
    markers = (
        f'constexpr const char* {name} = R"PY(',
        f'constexpr std::string_view {name} = R"PY(',
    )
    marker = next(candidate for candidate in markers if candidate in source)
    start = source.index(marker) + len(marker)
    end = source.index('\n)PY";', start)
    return source[start:end]


def definitions(script: str, first_action: str) -> dict:
    prefix = script[: script.index(first_action)]
    namespace = {
        "BIGSCREEN_JOB": json.dumps(
            {
                "statusPath": "unused-status.json",
                "cancelPath": "unused-cancel",
            }
        )
    }
    exec(compile(prefix, "<embedded-downloader-definitions>", "exec"), namespace)
    return namespace


source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
provider_source = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
media_helpers = extract(source, "MediaScriptHelpers")
download_script = media_helpers + extract(source, "DownloaderScript")
map_package_script = extract(source, "MapPackageScript")
probe_script = media_helpers + extract(source, "ProbeScript")
updater_script = extract(source, "UpdaterScript")

# A candidate that failed the on-device import test must not be offered every
# startup. This is intentionally a source-level invariant because the complete
# updater normally contacts GitHub and writes real package files.
assert "rejected = job.get('rejectedVersion', '')" in updater_script
assert "version == rejected" in updater_script
assert "will wait for a newer release" in updater_script
assert "maximum_package_bytes = 32 * 1024 * 1024" in updater_script
for required_entry in (
    "yt_dlp_ejs/__init__.py",
    "yt_dlp_ejs/yt/solver/__init__.py",
    "yt_dlp_ejs/yt/solver/core.min.js",
    "yt_dlp_ejs/yt/solver/lib.min.js",
):
    assert required_entry in updater_script

# Rollback must evict both separately named packages. Otherwise Python can
# retain solver code from a rejected update while loading yt-dlp from the
# restored package, creating a mixed runtime that was never validated.
assert "k == 'yt_dlp_ejs' or k.startswith('yt_dlp_ejs.')" in source
assert "ejs_solver.lib(), ejs_solver.core()" in source

# Compile every complete raw string so a typo cannot ship as a runtime-only
# failure on the headset.
for name, script in (
    ("DownloaderScript", download_script),
    ("MapPackageScript", map_package_script),
    ("ProbeScript", probe_script),
    ("UpdaterScript", updater_script),
):
    compile(script, f"<{name}>", "exec")

# All three foreground worker types share the cancellation marker. Probe and
# updater checks are deliberately placed between bounded network operations;
# the updater additionally checks every streamed package chunk.
for script in (download_script, probe_script, updater_script):
    assert "job['cancelPath']" in script
    assert "def cancelled():" in script
assert "raise RuntimeError('BIGSCREEN_CANCELLED')" in download_script
assert "raise KeyboardInterrupt('Video URL check cancelled')" in probe_script
assert "raise KeyboardInterrupt('yt-dlp update cancelled')" in updater_script
assert updater_script.count("cancelled()") >= 4
assert "raise RuntimeError('BIGSCREEN_CANCELLED')" in map_package_script
assert map_package_script.count("cancelled()") >= 4

# Managed BeatSaver extraction is intentionally narrower than a generic ZIP
# installer: redirects are host-pinned, paths and symlinks are rejected, and
# compressed/expanded work is bounded before the atomic final rename.
for safeguard in (
    "safe_https_url",
    "maximumArchiveBytes",
    "maximumExpandedBytes",
    "maximumEntries",
    "os.path.commonpath",
    "stat.S_IFLNK",
    "os.replace(publish_root, final_path)",
):
    assert safeguard in map_package_script

# The Quest provider must remain an in-process bridge. Reintroducing the
# upstream subprocess provider would appear to work on desktop while failing
# against Android's writable-directory execution restrictions.
compile(provider_source, "<bigscreen_jsc_provider>", "exec")
assert "bigscreen_quickjs.execute(source)" in provider_source
assert "subprocess" not in provider_source
assert "import bigscreen_jsc_provider" in download_script
assert "import bigscreen_jsc_provider" in probe_script
assert "import yt_dlp_ejs" in source

# Do not trust the arbitrary thumbnail URL returned in metadata. The validated
# YouTube ID is the only variable segment, and a failed refresh preserves the
# previously displayed image by deleting only its temporary file.
assert "video_id = str(result.get('id') or info.get('id') or '')" in download_script
assert "https://i.ytimg.com/vi/" in download_script
assert "info.get('thumbnail')" not in download_script
assert "os.remove(thumbnail_path + '.tmp')" in download_script
assert "os.remove(job['thumbnailPath'])" not in download_script
assert "Thumbnail warning:" in download_script

# Progress callbacks can be extremely frequent. Verify that transient updates
# neither fsync flash nor exceed the UI's 10 Hz polling cadence, while terminal
# state still uses the durable default.
assert "def publish(state, message='', durable=True" in download_script
assert "if durable:" in download_script
assert "now - last_progress_publish < 0.125" in download_script
assert "durable=False" in download_script

# Every network operation used by the cancellable download worker must have a
# finite wait. These settings keep shutdown bounded when Wi-Fi disappears.
for option in (
    "'socket_timeout': 15",
    "'retries': 3",
    "'fragment_retries': 3",
    "'extractor_retries': 3",
):
    assert option in download_script

# Quest downloads use a deterministic client that does not currently require
# a Google Video Server PO token. A mid-transfer 403 on a resumable `.part`
# file gets exactly one clean retry rather than leaving every future attempt
# stuck on the same rejected byte range.
assert "'player_client': ['android_vr']" in download_script
assert "part_path = job['finalPath'] + '.part'" in download_script
assert "partial_bytes <= 0" in download_script
assert "os.remove(part_path)" in download_script
assert "continuedl=False" in download_script
assert "BS-DL-HTTP-403" in download_script
assert "stream_summary(chosen)" in download_script

# Execute the production script against a small fake yt-dlp implementation.
# This exercises the recovery branch rather than merely checking its spelling:
# metadata succeeds, the first transfer leaves a `.part` and receives 403, and
# the second transfer must start clean and publish a completed result.
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    final_path = root / "video.mp4"
    status_path = root / "status.json"
    thumbnail_path = root / "thumbnail.jpg"
    calls = []
    candidate = {
        "format_id": "137",
        "ext": "mp4",
        "vcodec": "avc1.640028",
        "acodec": "none",
        "width": 1920,
        "height": 1080,
        "filesize": 32,
        "protocol": "https",
        "url": "https://example.invalid/video?c=ANDROID_VR",
    }
    info = {
        "title": "Recovery test",
        "duration": 10,
        "age_limit": 0,
        "formats": [candidate],
    }

    class FakeYoutubeDL:
        def __init__(self, options):
            self.options = options

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def extract_info(self, _url, download=False):
            if not download:
                return dict(info)
            calls.append(self.options.get("continuedl"))
            if len(calls) == 1:
                pathlib.Path(str(final_path) + ".part").write_bytes(b"partial")
                raise RuntimeError("HTTP Error 403: Forbidden")
            assert not pathlib.Path(str(final_path) + ".part").exists()
            final_path.write_bytes(b"complete-video")
            return dict(info)

    old_modules = {
        name: sys.modules.get(name)
        for name in ("bigscreen_jsc_provider", "yt_dlp")
    }
    sys.modules["bigscreen_jsc_provider"] = types.ModuleType(
        "bigscreen_jsc_provider"
    )
    fake_yt_dlp = types.ModuleType("yt_dlp")
    fake_yt_dlp.YoutubeDL = FakeYoutubeDL
    sys.modules["yt_dlp"] = fake_yt_dlp
    try:
        namespace = {
            "BIGSCREEN_JOB": json.dumps(
                {
                    "sourceUrl": "https://youtu.be/recovery",
                    "finalPath": str(final_path),
                    "thumbnailPath": str(thumbnail_path),
                    "statusPath": str(status_path),
                    "cancelPath": str(root / "cancel"),
                    "explicitContentAllowed": True,
                    "reserveBytes": 0,
                    "unknownRequiredBytes": 0,
                }
            )
        }
        exec(compile(download_script, "<download-recovery-test>", "exec"), namespace)
    finally:
        for name, module in old_modules.items():
            if module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module

    status = json.loads(status_path.read_text(encoding="utf-8"))
    assert status["state"] == "completed", status
    assert calls == [True, False], calls
    assert final_path.read_bytes() == b"complete-video"

# Resolution tiers are orientation-independent (short edge), downloads are
# exact rather than silently substituting a different tier, and only the new
# 1440 tier changes from H.264/MP4 to VP9/WebM.
assert "return min(width, height)" in download_script
assert "requested_height > 1440" in download_script
assert "if requested_height == 1440:" in download_script
assert "candidate.get('ext') == 'webm'" in download_script
assert "codec.startswith('vp9')" in download_script
assert "candidate.get('ext') == 'mp4'" in download_script
assert "codec.startswith('avc1')" in download_script
assert "tier(candidate) == requested_height" in download_script
assert "within_fps_limit or formats" in download_script
assert "available_heights = [height for height in (480, 720, 1080)" in probe_script
assert "if 1440 in vp9_heights:" in probe_script

download = definitions(download_script, "\ntry:\n    publish('preparing'")
probe = definitions(probe_script, "\ntry:\n    publish('probing'")

http_cases = {
    "HTTP Error 400: Bad Request": "Bad Request",
    "HTTP Error 401: Unauthorized": "Unauthorized",
    "HTTP Error 403: Forbidden": "refused access",
    "HTTP Error 404: Not Found": "Not Found",
    "HTTP Error 410: Gone": "Gone",
    "HTTP Error 429: Too Many Requests": "rate-limiting",
    "HTTP Error 503: Service Unavailable": "server error",
}
for raw_error, expected in http_cases.items():
    download_message = download["classify"](raw_error)[1]
    probe_message = probe["classify"](raw_error)
    assert expected.lower() in download_message.lower(), (raw_error, download_message)
    assert expected.lower() in probe_message.lower(), (raw_error, probe_message)

print("Embedded downloader scripts and HTTP explanations passed.")
