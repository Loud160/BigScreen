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
import urllib.request


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
release_check_script = extract(source, "YtDlpReleaseScript")

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
    ("YtDlpReleaseScript", release_check_script),
):
    compile(script, f"<{name}>", "exec")


def run_release_check(job, stable_version, nightly_version):
    """Execute the exact embedded policy without contacting GitHub."""
    requested_urls = []

    class FakeResponse:
        def __init__(self, version):
            self.payload = json.dumps({"tag_name": version}).encode("utf-8")

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def read(self, _limit=-1):
            return self.payload

    original_urlopen = urllib.request.urlopen

    def fake_urlopen(request, timeout=0):
        del timeout
        url = request.full_url
        requested_urls.append(url)
        return FakeResponse(
            nightly_version if "nightly-builds" in url else stable_version
        )

    try:
        urllib.request.urlopen = fake_urlopen
        namespace = {"BIGSCREEN_JOB": json.dumps(job)}
        exec(
            compile(
                release_check_script,
                "<YtDlpReleaseScript-policy>",
                "exec",
            ),
            namespace,
        )
        return json.loads(namespace["BIGSCREEN_YTDLP_RELEASE_RESULT"]), requested_urls
    finally:
        urllib.request.urlopen = original_urlopen


# Stable users never contact or auto-promote to nightly.
release, urls = run_release_check(
    {
        "currentVersion": "2026.08.19",
        "currentChannel": "stable",
        "requestedNightly": False,
        "automatic": True,
    },
    "2026.08.19",
    "2026.08.20.010101",
)
assert release["state"] == "up_to_date"
assert len(urls) == 1 and "nightly-builds" not in urls[0]

# A newer-dated stable release is offered before querying nightly.
release, urls = run_release_check(
    {
        "currentVersion": "2026.08.18.122307",
        "currentChannel": "nightly",
        "requestedNightly": True,
        "automatic": True,
    },
    "2026.08.19",
    "2026.08.20.010101",
)
assert release["state"] == "update_available"
assert release["checkedChannel"] == "stable"
assert release["stableReturn"] is True
assert release["stableCaughtUp"] is True
assert len(urls) == 1

# A stable release cut on the same date as the installed nightly counts as
# caught up even though the nightly tag carries an additional build suffix.
release, urls = run_release_check(
    {
        "currentVersion": "2026.08.18.122307",
        "currentChannel": "nightly",
        "requestedNightly": True,
        "automatic": True,
    },
    "2026.08.18",
    "2026.08.20.010101",
)
assert release["checkedChannel"] == "stable"
assert release["stableCaughtUp"] is True
assert len(urls) == 1

# Only when stable has not caught up does an installed nightly query nightly.
release, urls = run_release_check(
    {
        "currentVersion": "2026.08.18.122307",
        "currentChannel": "nightly",
        "requestedNightly": True,
        "automatic": True,
    },
    "2026.08.17",
    "2026.08.19.010101",
)
assert release["state"] == "update_available"
assert release["checkedChannel"] == "nightly"
assert len(urls) == 2

# A manual stable check always leaves the return path available. When stable
# is older than the installed nightly, the result must carry that distinction
# so the confirmation can clearly describe the switch as a downgrade.
release, urls = run_release_check(
    {
        "currentVersion": "2026.08.18.122307",
        "currentChannel": "nightly",
        "requestedNightly": False,
        "automatic": False,
    },
    "2026.07.04",
    "2026.08.19.010101",
)
assert release["state"] == "update_available"
assert release["checkedChannel"] == "stable"
assert release["stableReturn"] is True
assert release["stableCaughtUp"] is False
assert "has not caught up" in release["message"]
assert len(urls) == 1

# All three foreground worker types share the cancellation marker. Probe and
# updater checks are deliberately placed between bounded network operations;
# the updater additionally checks every streamed package chunk.
for script in (download_script, probe_script, updater_script):
    assert "job['cancelPath']" in script
    assert "def cancelled():" in script
assert "raise RuntimeError('BIGSCREEN_CANCELLED')" in download_script
assert "raise KeyboardInterrupt('Video URL check cancelled')" in probe_script
assert "raise KeyboardInterrupt('yt-dlp update cancelled')" in updater_script
assert "def version_key(value):" in updater_script
assert "latest_key <= current_key" in updater_script
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

# Android VR media URLs began requiring a Google Video Server PO token in
# August 2026. Both the probe and transfer must use yt-dlp's current default
# client selection while explicitly excluding that broken client. A remaining
# mid-transfer 403 on a resumable `.part` file still receives one clean retry.
client_policy = "'player_client': ['default', '-android_vr']"
assert download_script.count(client_policy) == 1
assert probe_script.count(client_policy) == 1
assert "'player_client': ['android_vr']" not in download_script
assert "'player_client': ['android_vr']" not in probe_script
assert "part_path = job['finalPath'] + '.part'" in download_script
assert "partial_bytes <= 0" in download_script
assert "os.remove(part_path)" in download_script
assert "continuedl=False" in download_script
assert "BS-DL-HTTP-403" in download_script
assert "stream_summary(chosen)" in download_script

# Prefer a direct HTTPS H.264 MP4 over a higher-bitrate HLS representation at
# the same tier. HLS remains eligible when it is the only usable transport,
# but selecting it unnecessarily forces a second on-device preparation pass.
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    final_path = root / "video.mp4"
    status_path = root / "status.json"
    chosen_formats = []
    direct_candidate = {
        "format_id": "direct-1080",
        "ext": "mp4",
        "vcodec": "avc1.640028",
        "acodec": "none",
        "width": 1920,
        "height": 1080,
        "fps": 30,
        "tbr": 2500,
        "filesize": 32,
        "protocol": "https",
    }
    hls_candidate = dict(
        direct_candidate,
        format_id="hls-1080",
        protocol="m3u8_native",
        tbr=5000,
    )
    info = {
        "title": "Transport preference test",
        "duration": 10,
        "age_limit": 0,
        "formats": [hls_candidate, direct_candidate],
    }

    class TransportYoutubeDL:
        def __init__(self, options):
            self.options = options

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def extract_info(self, _url, download=False):
            if not download:
                return dict(info)
            chosen_formats.append(self.options["format"])
            final_path.write_bytes(b"direct-video")
            return dict(info)

    old_modules = {
        name: sys.modules.get(name)
        for name in ("bigscreen_jsc_provider", "yt_dlp")
    }
    sys.modules["bigscreen_jsc_provider"] = types.ModuleType(
        "bigscreen_jsc_provider"
    )
    fake_yt_dlp = types.ModuleType("yt_dlp")
    fake_yt_dlp.YoutubeDL = TransportYoutubeDL
    sys.modules["yt_dlp"] = fake_yt_dlp
    try:
        namespace = {
            "BIGSCREEN_JOB": json.dumps(
                {
                    "sourceUrl": "https://youtu.be/transport",
                    "finalPath": str(final_path),
                    "thumbnailPath": str(root / "thumbnail.jpg"),
                    "statusPath": str(status_path),
                    "cancelPath": str(root / "cancel"),
                    "explicitContentAllowed": True,
                    "requestedHeight": 1080,
                    "maximumSourceFps": 30,
                    "reserveBytes": 0,
                    "unknownRequiredBytes": 0,
                }
            )
        }
        exec(compile(download_script, "<transport-preference-test>", "exec"), namespace)
    finally:
        for name, module in old_modules.items():
            if module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module

    status = json.loads(status_path.read_text(encoding="utf-8"))
    assert status["state"] == "completed", status
    assert chosen_formats == ["direct-1080"], chosen_formats

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

# Execute cancellation through yt-dlp's real production progress-hook path.
# The worker must publish a terminal cancelled state and retain the .part file
# so the user's explicit Resume action can continue it later.
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    final_path = root / "video.mp4"
    part_path = pathlib.Path(str(final_path) + ".part")
    status_path = root / "status.json"
    cancel_path = root / "cancel"
    candidate = {
        "format_id": "137",
        "ext": "mp4",
        "vcodec": "avc1.640028",
        "acodec": "none",
        "width": 1920,
        "height": 1080,
        "fps": 30,
        "filesize": 32,
        "protocol": "https",
        "url": "https://example.invalid/video?c=VISIONOS",
    }
    info = {
        "id": "abcdefghijk",
        "title": "Cancellation test",
        "duration": 10,
        "age_limit": 0,
        "formats": [candidate],
    }

    class CancellingYoutubeDL:
        def __init__(self, options):
            self.options = options

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def extract_info(self, _url, download=False):
            if not download:
                return dict(info)
            part_path.write_bytes(b"resumable-partial-video")
            cancel_path.write_text("cancel", encoding="utf-8")
            for hook in self.options["progress_hooks"]:
                hook({"status": "downloading", "downloaded_bytes": 23})
            raise AssertionError("The cancellation hook must abort the transfer")

    old_modules = {
        name: sys.modules.get(name)
        for name in ("bigscreen_jsc_provider", "yt_dlp")
    }
    sys.modules["bigscreen_jsc_provider"] = types.ModuleType(
        "bigscreen_jsc_provider"
    )
    fake_yt_dlp = types.ModuleType("yt_dlp")
    fake_yt_dlp.YoutubeDL = CancellingYoutubeDL
    sys.modules["yt_dlp"] = fake_yt_dlp
    try:
        namespace = {
            "BIGSCREEN_JOB": json.dumps(
                {
                    "sourceUrl": "https://youtu.be/cancelled",
                    "finalPath": str(final_path),
                    "thumbnailPath": str(root / "thumbnail.jpg"),
                    "statusPath": str(status_path),
                    "cancelPath": str(cancel_path),
                    "explicitContentAllowed": True,
                    "requestedHeight": 1080,
                    "maximumSourceFps": 60,
                    "reserveBytes": 0,
                    "unknownRequiredBytes": 0,
                }
            )
        }
        exec(
            compile(download_script, "<download-cancellation-test>", "exec"),
            namespace,
        )
    finally:
        for name, module in old_modules.items():
            if module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module

    status = json.loads(status_path.read_text(encoding="utf-8"))
    assert status["state"] == "cancelled", status
    assert status["errorCode"] == "BS-DL-CANCELLED", status
    assert not final_path.exists()
    assert part_path.read_bytes() == b"resumable-partial-video"

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

# YouTube prefixes policy failures with the generic words "Video unavailable".
# The specific restriction must win or the headset tells the player that the
# video disappeared when the real cause is an account/network policy.
workspace_restriction = (
    "ERROR: [youtube] abc: Video unavailable. This video is restricted. "
    "Please check the Google Workspace administrator and/or the network "
    "administrator restrictions."
)
assert "administrator" in download["classify"](workspace_restriction)[1].lower()
assert "administrator" in probe["classify"](workspace_restriction).lower()
assert download["diagnostic_code"](workspace_restriction) == \
    "BS-DL-ACCESS-RESTRICTED"
assert probe["diagnostic_code"](workspace_restriction) == \
    "BS-DL-ACCESS-RESTRICTED"

generic_unavailable = "ERROR: [youtube] abc: Video unavailable"
assert download["diagnostic_code"](generic_unavailable) == \
    "BS-DL-VIDEO-UNAVAILABLE"
assert probe["diagnostic_code"](generic_unavailable) == \
    "BS-DL-VIDEO-UNAVAILABLE"

print("Embedded downloader scripts and HTTP explanations passed.")
