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
