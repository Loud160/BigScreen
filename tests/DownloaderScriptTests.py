"""Syntax and user-facing HTTP error tests for embedded downloader scripts.

The production scripts live inside C++ raw strings so the Quest never depends
on loose executable source files. Extracting them here validates the exact text
that CPython receives, including mappings that are otherwise difficult to test
without deliberately provoking YouTube failures.
"""

import json
import pathlib
import sys


def extract(source: str, name: str) -> str:
    marker = f'constexpr const char* {name} = R"PY('
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
download_script = extract(source, "DownloaderScript")
probe_script = extract(source, "ProbeScript")
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
    ("ProbeScript", probe_script),
    ("UpdaterScript", updater_script),
):
    compile(script, f"<{name}>", "exec")

# The Quest provider must remain an in-process bridge. Reintroducing the
# upstream subprocess provider would appear to work on desktop while failing
# against Android's writable-directory execution restrictions.
compile(provider_source, "<bigscreen_jsc_provider>", "exec")
assert "bigscreen_quickjs.execute(source)" in provider_source
assert "subprocess" not in provider_source
assert "import bigscreen_jsc_provider" in download_script
assert "import bigscreen_jsc_provider" in probe_script
assert "import yt_dlp_ejs" in source

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

# Full HD is a maximum pixel envelope, not a landscape-only height rule.
# Portrait 1080x1920 must be accepted while 1440p/4K stays excluded.
assert "max(width, height) <= 1920" in download_script
assert "min(width, height) <= 1080" in download_script
assert "int(f.get('width') or 0) * int(f.get('height') or 0)" in download_script

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
