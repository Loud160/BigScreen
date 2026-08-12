"""Unit tests for the platform-neutral downloader source packager."""

from __future__ import annotations

import importlib.util
import pathlib
import sys
import tempfile
import zipfile


module_path = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("bigscreen_runtime_builder", module_path)
assert spec and spec.loader
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def put(path: pathlib.Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    yt_source = root / "yt"
    ejs_source = root / "ejs"

    put(yt_source / "yt_dlp/version.py", "__version__ = 'test'\n")
    put(yt_source / "yt_dlp/__init__.py", "")
    put(yt_source / "yt_dlp/__main__.py", "print('test')\n")
    put(yt_source / "yt_dlp/extractor/example.py", "VALUE = 42\n")
    put(yt_source / "yt_dlp/data/example.js", "const value = 42;\n")
    put(yt_source / "yt_dlp/__pyinstaller/hook-yt_dlp.py", "UNWANTED = True\n")

    for relative in builder.EJS_PACKAGE_FILES:
        put(ejs_source / "yt_dlp_ejs" / relative, "")
    put(ejs_source / "dist/yt.solver.core.min.js", "core();\n")
    put(ejs_source / "dist/yt.solver.lib.min.js", "library();\n")

    payload = builder.collect_payload(yt_source, ejs_source, "0.8.0")
    assert "__main__.py" in payload
    assert "yt_dlp/__main__.py" not in payload
    assert not any(name.startswith("yt_dlp/__pyinstaller/") for name in payload)
    assert b"version = '0.8.0'" in payload["yt_dlp_ejs/_version.py"]

    built = root / "yt-dlp-source-built"
    builder.write_zipimport(built, payload)
    assert built.read_bytes().startswith(b"#!/usr/bin/env python3\nPK")
    with zipfile.ZipFile(built) as archive:
        assert set(archive.namelist()) == set(payload)
        assert archive.read("yt_dlp_ejs/yt/solver/core.min.js") == payload[
            "yt_dlp_ejs/yt/solver/core.min.js"
        ]

    # The generated runtime itself can serve as a reference because zipfile
    # correctly accounts for yt-dlp's executable shebang before the ZIP data.
    builder.compare_reference(built, payload)

print("Downloader source packager tests passed.")
