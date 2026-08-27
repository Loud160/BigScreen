#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Canonical host-independent preparation, validation, build, and packaging.

The supported source build runs this file with the Linux Python 3 interpreter,
whether Linux is the host directly or Windows is supplying that host through
WSL.  Keeping these operations here gives both entrypoints one implementation
and avoids requiring PowerShell, Visual Studio, Git, or a separately installed
archive program to compile a downloaded source tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import selectors
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import uuid
import zipfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / ".cache"
DEPENDENCIES = CACHE / "dependencies"
BUILD = ROOT / "build"
RUNTIME_STAGE = BUILD / "downloader"

QUICKJS_VERSION = "0.16.1"
QUICKJS_SHA256 = "153f1940c5f61a59ab62703a6d13cf71ba0b2d2ba597683fe5315f14a64ed782"
QUICKJS_URL = (
    "https://github.com/quickjs-ng/quickjs/releases/download/"
    f"v{QUICKJS_VERSION}/quickjs-amalgam.zip"
)

PYTHON_VERSION = "3.14.7"
PYTHON_SHA256 = "6d50cc3aa66e414a439594089bcdfb5f1264358155c70c1f00471c24cfb477fb"
PYTHON_URL = (
    f"https://www.python.org/ftp/python/{PYTHON_VERSION}/"
    f"python-{PYTHON_VERSION}-aarch64-linux-android.tar.gz"
)
YTDLP_VERSION = "2026.08.19"
YTDLP_SHA256 = "1fa6733c37ea6fb51c99ad8fe785e7b7e5f3246c9b980230329d4fb72ed8d4d6"
YTDLP_EJS_VERSION = "0.8.0"
YTDLP_URL = (
    f"https://github.com/yt-dlp/yt-dlp/releases/download/{YTDLP_VERSION}/yt-dlp"
)
CERTIFI_VERSION = "2026.7.22"
CERTIFI_SHA256 = "62f22742b58a1a33014a2b6b706588a8d7e2a88ae7bd1a6ebe8c992928483775"
CERTIFI_URL = (
    "https://files.pythonhosted.org/packages/0b/a7/"
    "71ac2cff56fec219ed242bb11b8efb69fcc4bec75db06fb7bfe35de520e6/"
    f"certifi-{CERTIFI_VERSION}-py3-none-any.whl"
)

SCHEMA_REVISION = "eadb8d8d21caa1f8586b61da3c950a2953ebd399"
SCHEMA_SHA256 = "2de429724eae87554700b9eee31380fdd38a27afe135db0c2a124d5268e4c2ec"
SCHEMA_URL = (
    "https://raw.githubusercontent.com/Lauriethefish/QuestPatcher.QMod/"
    f"{SCHEMA_REVISION}/QuestPatcher.QMod/Resources/qmod.schema.json"
)

REQUIRED_LIBRARIES = (
    "libbigscreen-ffmpeg44-backend.so",
    "libbigscreen-ffmpeg9-backend.so",
    "libavformat-bigscreen44.so",
    "libavcodec-bigscreen44.so",
    "libavutil-bigscreen44.so",
    "libswscale-bigscreen44.so",
    "libavformat-bigscreen9.so",
    "libavcodec-bigscreen9.so",
    "libavutil-bigscreen9.so",
    "libswscale-bigscreen9.so",
    "libbeatsaber-hook.so",
    "libpython3.14.so",
    "libssl_python.so",
    "libcrypto_python.so",
    "libsqlite3_python.so",
)

RUNTIME_FILES = (
    "python314.zip",
    "yt-dlp-shipped",
    "certifi.whl",
    "runtime-manifest.json",
    "CPYTHON-LICENSE.txt",
    "bigscreen_jsc_provider.py",
    "BIGSCREEN-LICENSE.txt",
    "BIGSCREEN-ADDITIONAL-TERMS.md",
    "BIGSCREEN-NOTICE.txt",
    "THIRD-PARTY-NOTICES.md",
    "FFMPEG-LGPL-2.1-OR-LATER.txt",
    "FFMPEG-4.4.8-BUILD-INFO.txt",
    "FFMPEG-4.4.8-CHANGES.diff",
    "FFMPEG-9.0.1-BUILD-INFO.txt",
    "FFMPEG-9.0.1-CHANGES.diff",
    "CERTIFI-MPL-2.0.txt",
    "MPL-2.0.txt",
    "YT-DLP-UNLICENSE.txt",
    "QUICKJS-NG-MIT.txt",
    "OPENSSL-APACHE-2.0.txt",
    "SQLITE-PUBLIC-DOMAIN.txt",
)


class BuildError(RuntimeError):
    """A user-facing build failure."""


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path: pathlib.Path, minimum_size: int = 1) -> pathlib.Path:
    if not path.is_file() or path.stat().st_size < minimum_size:
        raise BuildError(f"Required build input is missing or incomplete: {path}")
    return path


def remove_fixed_child(path: pathlib.Path, parent: pathlib.Path) -> None:
    resolved = path.resolve(strict=False)
    if resolved.parent != parent.resolve(strict=False):
        raise BuildError(f"Refusing to remove an unexpected path: {resolved}")
    if resolved.is_dir():
        shutil.rmtree(resolved)
    elif resolved.exists():
        resolved.unlink()


def migrate_dependency_cache() -> None:
    DEPENDENCIES.mkdir(parents=True, exist_ok=True)
    migrations = {
        "ffmpeg-lgpl": "ffmpeg-lgpl",
        "ffmpeg-lgpl-9.0.1": "ffmpeg-lgpl-9.0.1",
        "quickjs-ng": "quickjs-ng",
        "downloader": "downloader",
        "downloader-source": "downloader-source",
    }
    for legacy_name, current_name in migrations.items():
        legacy = ROOT / "extern" / legacy_name
        current = DEPENDENCIES / current_name
        if legacy.is_dir() and not current.exists():
            print(f"Moving the existing {current_name} cache outside QPM's extern directory.")
            shutil.move(str(legacy), str(current))
    print(f"Big Screen dependency cache: {DEPENDENCIES}")


def download(url: str, destination: pathlib.Path, expected_hash: str, label: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and sha256(destination) == expected_hash:
        print(f"Using cached {label}.")
        return
    if destination.exists():
        destination.unlink()
    temporary = destination.with_name(destination.name + f".download.{os.getpid()}")
    temporary.unlink(missing_ok=True)
    print(f"Downloading {label}.")
    print(f"Source: {url}")
    print("The download is cached and reused after its pinned SHA-256 is verified.")
    last_percent = -1

    def report(blocks: int, block_size: int, total: int) -> None:
        nonlocal last_percent
        if total <= 0:
            return
        percent = min(100, blocks * block_size * 100 // total)
        if percent >= last_percent + 5 or percent == 100:
            print(f"  {label}: {percent}%", flush=True)
            last_percent = percent

    try:
        urllib.request.urlretrieve(url, temporary, report)
        actual = sha256(temporary)
        if actual != expected_hash:
            raise BuildError(
                f"SHA-256 mismatch for {label}. Expected {expected_hash}, received {actual}."
            )
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def prepare_quickjs(force: bool = False) -> pathlib.Path:
    migrate_dependency_cache()
    root = DEPENDENCIES / "quickjs-ng"
    archive = root / f"quickjs-amalgam-{QUICKJS_VERSION}.zip"
    source = root / "source"
    if force:
        archive.unlink(missing_ok=True)
    download(QUICKJS_URL, archive, QUICKJS_SHA256, f"QuickJS-NG {QUICKJS_VERSION}")
    c_file = source / "quickjs-amalgam.c"
    h_file = source / "quickjs.h"
    if force or not c_file.is_file() or not h_file.is_file():
        if source.exists():
            remove_fixed_child(source, root)
        source.mkdir(parents=True)
        with zipfile.ZipFile(archive) as package:
            package.extractall(source)
    require_file(c_file, 1024)
    require_file(h_file, 1024)
    ready = {
        "version": QUICKJS_VERSION,
        "archiveUrl": QUICKJS_URL,
        "archiveSha256": QUICKJS_SHA256,
    }
    (root / f"quickjs-{QUICKJS_VERSION}.ready").write_text(
        json.dumps(ready, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Prepared QuickJS-NG {QUICKJS_VERSION} in {source}")
    return source


def deterministic_tool() -> pathlib.Path:
    source_root = ROOT / "tools" / "deterministic-zip"
    inputs = (
        "CMakeLists.txt",
        "main.cpp",
        "miniz_export.h",
        "vendor/miniz-3.1.2/LICENSE",
        "vendor/miniz-3.1.2/miniz.c",
        "vendor/miniz-3.1.2/miniz.h",
        "vendor/miniz-3.1.2/miniz_common.h",
        "vendor/miniz-3.1.2/miniz_tdef.c",
        "vendor/miniz-3.1.2/miniz_tdef.h",
        "vendor/miniz-3.1.2/miniz_tinfl.c",
        "vendor/miniz-3.1.2/miniz_tinfl.h",
        "vendor/miniz-3.1.2/miniz_zip.h",
    )
    records: list[str] = []
    for relative in inputs:
        path = require_file(source_root / relative)
        records.append(f"{relative}={sha256(path)}")
    source_hash = hashlib.sha256("\n".join(records).encode()).hexdigest()
    host = f"linux-{os.uname().machine}"
    host_root = CACHE / "build-tools" / "deterministic-zip" / host
    build_root = host_root / "build"
    executable = build_root / "bin" / "bigscreen-deterministic-zip"
    stamp = host_root / "source.sha256"
    cached = stamp.read_text(encoding="utf-8").strip() if stamp.is_file() else ""
    if cached != source_hash or not executable.is_file():
        expected_parent = (CACHE / "build-tools" / "deterministic-zip").resolve()
        if host_root.resolve(strict=False).parent != expected_parent:
            raise BuildError(f"Refusing to rebuild an unexpected tool path: {host_root}")
        if host_root.exists():
            shutil.rmtree(host_root)
        host_root.mkdir(parents=True)
        print("Building Big Screen's tracked deterministic ZIP compressor.")
        subprocess.run(
            ["cmake", "-S", str(source_root), "-B", str(build_root),
             "-DCMAKE_BUILD_TYPE=Release"], check=True
        )
        subprocess.run(["cmake", "--build", str(build_root), "--config", "Release"], check=True)
        require_file(executable)
        stamp.write_text(source_hash, encoding="utf-8")
    return executable


def write_deterministic_zip(
    destination: pathlib.Path,
    sources: list[pathlib.Path],
    entries: list[str],
) -> None:
    if len(sources) != len(entries):
        raise BuildError("ZIP source paths and entry names must have equal lengths.")
    if len(entries) > 65535:
        raise BuildError("Deterministic ZIP32 cannot contain more than 65535 entries.")
    normalized: list[str] = []
    seen: set[str] = set()
    for source, entry in zip(sources, entries):
        require_file(source, 0)
        name = entry.replace("\\", "/")
        parts = pathlib.PurePosixPath(name).parts
        if (
            not name
            or name.startswith("/")
            or name.endswith("/")
            or ".." in parts
            or ":" in name
        ):
            raise BuildError(f"Unsafe ZIP entry name: {name}")
        if name in seen:
            raise BuildError(f"Duplicate ZIP entry name: {name}")
        seen.add(name)
        normalized.append(name)

    destination.parent.mkdir(parents=True, exist_ok=True)
    work_parent = CACHE / "build-tools" / "deterministic-zip"
    work_parent.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="work-", dir=work_parent))
    try:
        stage = work / "stage"
        stage.mkdir()
        by_entry = dict(zip(normalized, sources))
        for name in sorted(normalized):
            target = stage / pathlib.PurePosixPath(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(by_entry[name], target)
        listing = work / "entries.txt"
        listing.write_text("\n".join(sorted(normalized)) + "\n", encoding="utf-8")
        subprocess.run(
            [str(deterministic_tool()), str(destination), str(stage), str(listing)],
            check=True,
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def prepare_downloader(force: bool = False) -> None:
    migrate_dependency_cache()
    download_root = DEPENDENCIES / "downloader"
    python_archive = download_root / f"python-{PYTHON_VERSION}-aarch64-linux-android.tar.gz"
    python_extract = download_root / f"python-{PYTHON_VERSION}"
    python_prefix = python_extract / "prefix"
    ytdlp = download_root / f"yt-dlp-{YTDLP_VERSION}"
    certifi = download_root / f"certifi-{CERTIFI_VERSION}-py3-none-any.whl"
    if force:
        for cached in (python_archive, ytdlp, certifi):
            cached.unlink(missing_ok=True)
    download(PYTHON_URL, python_archive, PYTHON_SHA256, f"CPython {PYTHON_VERSION} Android ARM64")
    required_python = (
        "lib/libpython3.14.so",
        "lib/libssl_python.so",
        "lib/libcrypto_python.so",
        "lib/libsqlite3_python.so",
        "lib/python3.14/os.py",
        "include/python3.14/Python.h",
    )
    extraction_complete = all(
        (python_prefix / relative).is_file()
        and (python_prefix / relative).stat().st_size > 0
        for relative in required_python
    )
    if force or not extraction_complete:
        if python_extract.exists():
            remove_fixed_child(python_extract, download_root)
        python_extract.mkdir(parents=True)
        print("Extracting the verified Android CPython runtime. This can take a little while.")
        # Use the required host tar rather than tarfile's Python 3.12-only
        # ``filter=`` argument. Ubuntu 22.04 ships Python 3.10, and it is a
        # supported build host. The archive bytes are already pinned and
        # SHA-256 verified immediately above.
        subprocess.run(
            ["tar", "-xzf", str(python_archive), "-C", str(python_extract)],
            check=True,
        )
    for relative in required_python:
        require_file(python_prefix / relative)

    download(YTDLP_URL, ytdlp, YTDLP_SHA256, f"stable yt-dlp {YTDLP_VERSION}")
    with zipfile.ZipFile(ytdlp) as archive:
        required_ejs = (
            "yt_dlp_ejs/__init__.py",
            "yt_dlp_ejs/_version.py",
            "yt_dlp_ejs/yt/solver/core.min.js",
            "yt_dlp_ejs/yt/solver/lib.min.js",
        )
        missing = [name for name in required_ejs if name not in archive.namelist()]
        if missing:
            raise BuildError(f"The verified yt-dlp package lacks bundled EJS files: {missing}")
        version_text = archive.read("yt_dlp_ejs/_version.py").decode("utf-8")
        if not re.search(
            rf"__version__\s*=\s*version\s*=\s*'{re.escape(YTDLP_EJS_VERSION)}'",
            version_text,
        ):
            raise BuildError(
                f"The verified yt-dlp package does not contain yt-dlp-ejs {YTDLP_EJS_VERSION}."
            )

    download(CERTIFI_URL, certifi, CERTIFI_SHA256, f"certifi {CERTIFI_VERSION}")
    RUNTIME_STAGE.mkdir(parents=True, exist_ok=True)
    certifi_stage = RUNTIME_STAGE / "certifi"
    if certifi_stage.exists():
        remove_fixed_child(certifi_stage, RUNTIME_STAGE)
    certifi_stage.mkdir()
    with zipfile.ZipFile(certifi) as archive:
        for name in ("__init__.py", "__main__.py", "core.py", "py.typed", "cacert.pem"):
            entry = f"certifi/{name}"
            if entry not in archive.namelist():
                raise BuildError(f"The verified certifi wheel lacks {entry}.")
            (certifi_stage / name).write_bytes(archive.read(entry))

    python_lib = python_prefix / "lib"
    (ROOT / "extern" / "libs").mkdir(parents=True, exist_ok=True)
    BUILD.mkdir(parents=True, exist_ok=True)
    native_libraries = (
        "libpython3.14.so",
        "libssl_python.so",
        "libcrypto_python.so",
        "libsqlite3_python.so",
    )
    for name in native_libraries:
        source = require_file(python_lib / name)
        shutil.copyfile(source, BUILD / name)
        extern_copy = ROOT / "extern" / "libs" / name
        if name == "libpython3.14.so":
            shutil.copyfile(source, extern_copy)
        else:
            extern_copy.unlink(missing_ok=True)

    stdlib = python_lib / "python3.14"
    stdlib_zip = RUNTIME_STAGE / "python314.zip"
    stdlib_stamp = RUNTIME_STAGE / ".python314-zip.ready"
    format_stamp = f"bigscreen-deterministic-deflate-miniz-3.1.2-v1|{PYTHON_SHA256}"
    rebuild_stdlib = (
        force
        or not stdlib_zip.is_file()
        or stdlib_zip.stat().st_size < 1024
        or not stdlib_stamp.is_file()
        or stdlib_stamp.read_text(encoding="utf-8").strip() != format_stamp
    )
    if rebuild_stdlib:
        excluded = ("ensurepip/", "idlelib/", "lib-dynload/", "site-packages/",
                    "test/", "tkinter/", "turtledemo/", "venv/")
        sources: list[pathlib.Path] = []
        entries: list[str] = []
        for source in sorted(path for path in stdlib.rglob("*") if path.is_file()):
            relative = source.relative_to(stdlib).as_posix()
            if "/__pycache__/" in f"/{relative}" or relative.endswith(".pyc"):
                continue
            if any(relative.startswith(prefix) for prefix in excluded):
                continue
            sources.append(source)
            entries.append(relative)
        temporary = stdlib_zip.with_suffix(".zip.building")
        temporary.unlink(missing_ok=True)
        print("Compressing the Android Python standard library. This can take a little while.")
        write_deterministic_zip(temporary, sources, entries)
        os.replace(temporary, stdlib_zip)
        stdlib_stamp.write_text(format_stamp, encoding="utf-8")

    shutil.copyfile(ytdlp, RUNTIME_STAGE / "yt-dlp-shipped")
    shutil.copyfile(certifi, RUNTIME_STAGE / "certifi.whl")
    shutil.copyfile(stdlib / "LICENSE.txt", RUNTIME_STAGE / "CPYTHON-LICENSE.txt")
    shutil.copyfile(ROOT / "python" / "bigscreen_jsc_provider.py",
                    RUNTIME_STAGE / "bigscreen_jsc_provider.py")

    dynamic_stage = RUNTIME_STAGE / "lib-dynload"
    if dynamic_stage.exists():
        remove_fixed_child(dynamic_stage, RUNTIME_STAGE)
    dynamic_stage.mkdir()
    production_extensions: list[str] = []
    for extension in sorted((stdlib / "lib-dynload").glob("*.so")):
        name = extension.name
        if (
            name.startswith("_test")
            or "_test." in name
            or name.startswith("xx")
            or name in {
                "_xxtestfuzz.cpython-314-aarch64-linux-android.so",
                "_remote_debugging.cpython-314-aarch64-linux-android.so",
            }
        ):
            continue
        shutil.copyfile(extension, dynamic_stage / name)
        production_extensions.append(name)

    manifest = {
        "pythonVersion": PYTHON_VERSION,
        "pythonSha256": PYTHON_SHA256,
        "ytDlpVersion": YTDLP_VERSION,
        "ytDlpSha256": YTDLP_SHA256,
        "ytDlpEjsVersion": YTDLP_EJS_VERSION,
        "certifiVersion": CERTIFI_VERSION,
        "certifiSha256": CERTIFI_SHA256,
        "quickJsVersion": QUICKJS_VERSION,
        "nativeExtensions": production_extensions,
    }
    (RUNTIME_STAGE / "runtime-manifest.json").write_text(
        json.dumps(manifest, separators=(",", ":")), encoding="utf-8"
    )
    print(f"Prepared CPython {PYTHON_VERSION} and yt-dlp {YTDLP_VERSION} in {RUNTIME_STAGE}")


def schema_cache() -> pathlib.Path:
    path = CACHE / f"qmod-schema-{SCHEMA_REVISION}.json"
    download(SCHEMA_URL, path, SCHEMA_SHA256, f"pinned QMOD schema {SCHEMA_REVISION}")
    # Parsing the verified file detects corruption that somehow retained the
    # expected bytes only in caller assumptions, and locks this validator to
    # the reviewed schema contract below.
    schema = json.loads(path.read_text(encoding="utf-8"))
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        raise BuildError("The pinned QMOD schema has an unexpected dialect.")
    return path


def validate_schema_contract(manifest: dict) -> None:
    """Validate every schema rule that applies to Big Screen's QMOD shape.

    The pinned upstream schema uses only required/type/enum/pattern, anyOf,
    uniqueItems, array items, and additionalProperties for fields emitted by
    this project.  Implementing those rules directly avoids downloading a
    Python package manager and its dependency graph merely to validate one
    small, immutable manifest.
    """

    required = ("_QPVersion", "name", "id", "author", "version")
    for name in required:
        if name not in manifest:
            raise BuildError(f"mod.json is missing schema-required field '{name}'.")
    if manifest.get("_QPVersion") not in {"0.1.0", "0.1.1", "0.1.2", "1.0.0", "1.1.0", "1.2.0"}:
        raise BuildError("mod.json contains an unsupported _QPVersion.")
    string_fields = (
        "_QPVersion", "name", "id", "author", "porter", "version", "packageId",
        "packageVersion", "description", "coverImage", "modloader",
    )
    for name in string_fields:
        if name in manifest and not isinstance(manifest[name], str):
            raise BuildError(f"mod.json field '{name}' must be text.")
    if not re.fullmatch(r"\S+", manifest["id"]):
        raise BuildError("mod.json id must not contain whitespace.")
    semver = re.compile(
        r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
        r"(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)"
        r"(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
        r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$"
    )
    if not semver.fullmatch(manifest["version"]):
        raise BuildError("mod.json version is not valid SemVer.")
    if "modloader" in manifest and manifest["modloader"] not in {"QuestLoader", "Scotland2"}:
        raise BuildError("mod.json modloader is not supported by the pinned schema.")
    arrays = ("modFiles", "lateModFiles", "libraryFiles")
    for name in arrays:
        if name not in manifest:
            continue
        values = manifest[name]
        if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
            raise BuildError(f"mod.json '{name}' must be an array of text entries.")
        if len(values) != len(set(values)):
            raise BuildError(f"mod.json '{name}' contains duplicate entries.")
    if not any(name in manifest for name in
               ("modFiles", "lateModFiles", "libraryFiles", "dependencies", "fileCopies")):
        raise BuildError("mod.json does not satisfy the schema's payload anyOf rule.")
    dependencies = manifest.get("dependencies", [])
    if not isinstance(dependencies, list):
        raise BuildError("mod.json dependencies must be an array.")
    for dependency in dependencies:
        if not isinstance(dependency, dict) or not {"id", "version"} <= dependency.keys():
            raise BuildError("mod.json contains an incomplete dependency.")
        if set(dependency) - {"id", "version", "downloadIfMissing", "required"}:
            raise BuildError("mod.json dependency contains a schema-unknown property.")
        if not isinstance(dependency["id"], str) or not re.fullmatch(r"\S+", dependency["id"]):
            raise BuildError("mod.json dependency id is invalid.")
        if not isinstance(dependency["version"], str):
            raise BuildError("mod.json dependency version must be text.")
        if "downloadIfMissing" in dependency and not re.match(r"^https?://", dependency["downloadIfMissing"]):
            raise BuildError("mod.json dependency downloadIfMissing is not an HTTP(S) URL.")
        if "required" in dependency and not isinstance(dependency["required"], bool):
            raise BuildError("mod.json dependency required flag must be boolean.")
    copies = manifest.get("fileCopies", [])
    if not isinstance(copies, list):
        raise BuildError("mod.json fileCopies must be an array.")
    for copy in copies:
        if not isinstance(copy, dict) or set(copy) != {"name", "destination"}:
            raise BuildError("mod.json fileCopies entry does not match the pinned schema.")
        if not all(isinstance(copy[name], str) for name in ("name", "destination")):
            raise BuildError("mod.json fileCopies values must be text.")


def expected_dependencies(shared: dict) -> list[dict]:
    restored = {
        item["dependency"]["id"]: item["dependency"]
        for item in shared.get("restoredDependencies", [])
        if isinstance(item, dict) and isinstance(item.get("dependency"), dict)
    }
    expected: list[dict] = []
    for configured in shared["config"].get("dependencies", []):
        if configured.get("additionalData", {}).get("includeQmod", True) is False:
            continue
        dependency = restored.get(configured["id"], {})
        link = dependency.get("additionalData", {}).get("modLink")
        if link:
            expected.append({
                "id": configured["id"],
                "version": configured["versionRange"],
                "downloadIfMissing": link,
            })
    return expected


def validate_manifest(manifest_path: pathlib.Path = ROOT / "mod.json") -> dict:
    raw = require_file(manifest_path).read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raise BuildError("mod.json must be UTF-8 without a byte-order mark for MBF.")
    try:
        manifest = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BuildError(f"mod.json is not valid UTF-8 JSON: {error}") from error
    if not isinstance(manifest, dict):
        raise BuildError("mod.json root must be an object.")
    schema_cache()
    validate_schema_contract(manifest)
    template = json.loads((ROOT / "mod.template.json").read_text(encoding="utf-8"))
    shared = json.loads((ROOT / "qpm.shared.json").read_text(encoding="utf-8"))
    substitutions = {
        "${mod_name}": shared["config"]["info"]["name"],
        "${mod_id}": shared["config"]["info"]["id"],
    }
    for name in ("name", "id", "author", "version", "packageId", "packageVersion"):
        expected = substitutions.get(template.get(name), template.get(name))
        if manifest.get(name) != expected:
            raise BuildError(
                f"mod.json field '{name}' is stale (expected {expected!r}, "
                f"found {manifest.get(name)!r}). Run 'qpm qmod manifest'."
            )
    expected = {item["id"]: item for item in expected_dependencies(shared)}
    actual = {item["id"]: item for item in manifest.get("dependencies", [])}
    if set(expected) != set(actual):
        raise BuildError("mod.json dependencies do not match qpm.shared.json.")
    for identifier, dependency in expected.items():
        for name in ("version", "downloadIfMissing"):
            if actual[identifier].get(name) != dependency[name]:
                raise BuildError(
                    f"mod.json dependency '{identifier}' has stale {name} metadata."
                )
    print("Pinned QMOD schema, identity, target, and dependency validation passed.")
    return manifest


def generate_manifest() -> dict:
    qpm = os.environ.get("BIGSCREEN_QPM", "qpm")
    print("Generating mod.json from tracked QPM metadata.")
    subprocess.run([qpm, "qmod", "manifest"], cwd=ROOT, check=True)
    return validate_manifest()


def stage_notices() -> None:
    ffmpeg44 = DEPENDENCIES / "ffmpeg-lgpl"
    ffmpeg9 = DEPENDENCIES / "ffmpeg-lgpl-9.0.1"
    sources = {
        "BIGSCREEN-LICENSE.txt": ROOT / "LICENSE",
        "BIGSCREEN-ADDITIONAL-TERMS.md": ROOT / "LICENSE-ADDITIONAL-TERMS.md",
        "BIGSCREEN-NOTICE.txt": ROOT / "NOTICE",
        "THIRD-PARTY-NOTICES.md": ROOT / "THIRD_PARTY_NOTICES.md",
        "FFMPEG-LGPL-2.1-OR-LATER.txt": ffmpeg44 / "COPYING.LGPLv2.1",
        "FFMPEG-4.4.8-BUILD-INFO.txt": ffmpeg44 / "BUILD-INFO.txt",
        "FFMPEG-4.4.8-CHANGES.diff": ffmpeg44 / "bigscreen-ffmpeg-changes.diff",
        "FFMPEG-9.0.1-BUILD-INFO.txt": ffmpeg9 / "BUILD-INFO.txt",
        "FFMPEG-9.0.1-CHANGES.diff": ffmpeg9 / "bigscreen-ffmpeg-changes.diff",
        "CERTIFI-MPL-2.0.txt": ROOT / "licenses" / "CERTIFI-MPL-2.0.txt",
        "MPL-2.0.txt": ROOT / "licenses" / "MPL-2.0.txt",
        "YT-DLP-UNLICENSE.txt": ROOT / "licenses" / "YT-DLP-UNLICENSE.txt",
        "QUICKJS-NG-MIT.txt": ROOT / "licenses" / "QUICKJS-NG-MIT.txt",
        "OPENSSL-APACHE-2.0.txt": ROOT / "licenses" / "OPENSSL-APACHE-2.0.txt",
        "SQLITE-PUBLIC-DOMAIN.txt": ROOT / "licenses" / "SQLITE-PUBLIC-DOMAIN.txt",
    }
    if not RUNTIME_STAGE.is_dir():
        raise BuildError(f"Downloader runtime must be staged before notices: {RUNTIME_STAGE}")
    for name, source in sources.items():
        shutil.copyfile(require_file(source, 0), RUNTIME_STAGE / name)


def sync_runtime_manifest(manifest: dict) -> tuple[dict, list[pathlib.Path]]:
    destination = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime/"
    copies: list[dict] = []
    sources: list[pathlib.Path] = []
    for name in RUNTIME_FILES:
        source = require_file(RUNTIME_STAGE / name, 0)
        copies.append({"name": name, "destination": destination + name})
        sources.append(source)
    for subdirectory in ("certifi", "lib-dynload"):
        directory = RUNTIME_STAGE / subdirectory
        if not directory.is_dir():
            raise BuildError(f"Required runtime directory is missing: {directory}")
        for source in sorted(path for path in directory.iterdir() if path.is_file()):
            if subdirectory == "lib-dynload" and source.suffix != ".so":
                continue
            copies.append({
                "name": source.name,
                "destination": destination + f"{subdirectory}/{source.name}",
            })
            sources.append(source)
    names = [copy["name"] for copy in copies]
    if len(names) != len(set(names)):
        raise BuildError("Runtime manifest contains duplicate flat QMOD entry names.")
    manifest["fileCopies"] = copies
    print(f"Synchronized {len(copies)} Big Screen runtime files with mod.json.")
    return manifest, sources


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(json.dumps(value, separators=(",", ":")), encoding="utf-8")


def readelf(build_directory: pathlib.Path, file: pathlib.Path, *arguments: str) -> str:
    cache = require_file(build_directory / "CMakeCache.txt").read_text(
        encoding="utf-8", errors="replace"
    )
    match = re.search(r"^CMAKE_READELF:FILEPATH=(.+)$", cache, re.MULTILINE)
    if not match:
        raise BuildError("The configured Android llvm-readelf tool was not recorded.")
    executable = require_file(pathlib.Path(match.group(1)))
    require_file(file)
    return subprocess.run(
        [str(executable), *arguments, str(file)], check=True, text=True,
        stdout=subprocess.PIPE,
    ).stdout


def validate_elf(build_directory: pathlib.Path = BUILD) -> None:
    main_dynamic = readelf(build_directory, build_directory / "libbigscreen.so", "-d")
    main_symbols = readelf(
        build_directory, build_directory / "libbigscreen.so",
        "--dyn-syms", "--wide",
    )
    if "libpaper2_scotland2.so" in main_dynamic:
        raise BuildError(
            "Big Screen still declares Paper2 in DT_NEEDED."
        )
    for paper_symbol in (
        "paper2_queue_log_bytes_ffi", "paper2_wait_for_flush",
        "__wrap_paper2_queue_log_bytes_ffi",
        "__wrap_paper2_wait_for_flush",
    ):
        if paper_symbol in main_symbols:
            raise BuildError(
                "Big Screen exposes a Paper compatibility symbol: "
                f"{paper_symbol}."
            )
    for backend in ("libbigscreen-ffmpeg44-backend.so", "libbigscreen-ffmpeg9-backend.so"):
        if backend not in main_dynamic:
            raise BuildError(f"libbigscreen.so does not require {backend}.")
    runtimes = (
        ("44", "9", "BIGSCREEN44_LIB", "BIGSCREEN9_LIB", "CreateFrameDecoder44Backend"),
        ("9", "44", "BIGSCREEN9_LIB", "BIGSCREEN44_LIB", "CreateFrameDecoder9Backend"),
    )
    for tag, other, namespace, other_namespace, factory in runtimes:
        name = f"libbigscreen-ffmpeg{tag}-backend.so"
        path = build_directory / name
        dynamic = readelf(build_directory, path, "-d")
        versions = readelf(build_directory, path, "--version-info")
        symbols = readelf(build_directory, path, "--dyn-syms", "--wide")
        for component in ("avformat", "avcodec", "avutil", "swscale"):
            expected = f"lib{component}-bigscreen{tag}.so"
            wrong = f"lib{component}-bigscreen{other}.so"
            if expected not in dynamic or wrong in dynamic:
                raise BuildError(f"{name} has an incorrect FFmpeg dependency set.")
        if namespace not in versions or other_namespace in versions:
            raise BuildError(f"{name} is not isolated to {namespace}* symbols.")
        if factory not in symbols:
            raise BuildError(f"{name} does not export {factory}.")
    print("Dual FFmpeg backend ELF isolation validated.")


def run_with_heartbeat(arguments: list[str], cwd: pathlib.Path, environment: dict[str, str]) -> None:
    process = subprocess.Popen(
        arguments, cwd=cwd, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1,
    )
    assert process.stdout is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    started = time.monotonic()
    last_output = started
    while process.poll() is None:
        events = selector.select(timeout=1.0)
        for key, _ in events:
            line = key.fileobj.readline()
            if line:
                print(line, end="", flush=True)
                last_output = time.monotonic()
        if time.monotonic() - last_output >= 15:
            elapsed = int(time.monotonic() - started)
            print(
                f"Still building Big Screen... {elapsed} seconds elapsed. "
                "The current compiler or linker step is still running.",
                flush=True,
            )
            last_output = time.monotonic()
    for line in process.stdout:
        print(line, end="", flush=True)
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, arguments)


def clean_or_reset_build(clean: bool) -> None:
    if clean and BUILD.exists():
        remove_fixed_child(BUILD, ROOT)
        return
    cache = BUILD / "CMakeCache.txt"
    if not cache.is_file():
        return
    text = cache.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$", text, re.MULTILINE)
    if not match or pathlib.Path(match.group(1)).resolve(strict=False) == ROOT.resolve():
        return
    print(f"Resetting CMake metadata created at a different host path: {match.group(1)}")
    for name in (
        "CMakeCache.txt", "CMakeFiles", "build.ninja", "rules.ninja",
        "cmake_install.cmake", "compile_commands.json", ".ninja_deps", ".ninja_log",
    ):
        path = BUILD / name
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink(missing_ok=True)


def build_native(clean: bool = False) -> None:
    clean_or_reset_build(clean)
    prepare_quickjs()
    prepare_downloader()
    environment = os.environ.copy()
    environment["SOURCE_DATE_EPOCH"] = "946684800"
    crash_test_value = environment.get(
        "BIGSCREEN_ENABLE_LOGGER_CRASH_TEST", "OFF").upper()
    if crash_test_value not in {"0", "1", "OFF", "ON", "FALSE", "TRUE"}:
        raise BuildError(
            "BIGSCREEN_ENABLE_LOGGER_CRASH_TEST must be ON or OFF, "
            f"not {crash_test_value!r}."
        )
    crash_test_enabled = crash_test_value in {"1", "ON", "TRUE"}
    subprocess.run(
        [
            "cmake", "-Wno-deprecated", "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DBIGSCREEN_UP_DOWN_SHOWCASE=ON",
            "-DBIGSCREEN_ENABLE_LOGGER_CRASH_TEST=" +
                ("ON" if crash_test_enabled else "OFF"),
            "-S", str(ROOT), "-B", str(BUILD),
        ],
        cwd=ROOT, env=environment, check=True,
    )
    print("Building Big Screen's native Quest libraries. A clean build can take several minutes.")
    print("The final link/LTO step can stay on the last progress line for a while; a heartbeat remains visible.")
    stamp = BUILD / ".bigscreen-build-success"
    stamp.unlink(missing_ok=True)
    run_with_heartbeat(["cmake", "--build", str(BUILD)], ROOT, environment)
    validate_elf(BUILD)
    library = require_file(BUILD / "libbigscreen.so")
    inputs = [path for root in (ROOT / "src", ROOT / "include") for path in root.rglob("*") if path.is_file()]
    inputs.append(ROOT / "CMakeLists.txt")
    newest = max(path.stat().st_mtime_ns for path in inputs)
    if library.stat().st_mtime_ns < newest:
        raise BuildError(
            "The native output is older than a first-party source input; stale deployment is blocked."
        )
    stamp.write_text(
        f"completedUtc={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}\n"
        f"binarySha256={sha256(library)}",
        encoding="utf-8",
    )


def package_qmod(name: str | None = None) -> pathlib.Path:
    prepare_quickjs()
    prepare_downloader()
    manifest = generate_manifest()
    validate_elf(BUILD)
    template = json.loads((ROOT / "mod.template.json").read_text(encoding="utf-8"))
    manifest["version"] = template["version"]
    manifest["dependencies"] = [
        dependency for dependency in manifest.get("dependencies", [])
        if dependency.get("id") != "hollywood"
    ]
    manifest["libraryFiles"] = list(REQUIRED_LIBRARIES)
    stage_notices()
    manifest, runtime_sources = sync_runtime_manifest(manifest)
    mod_path = ROOT / "mod.json"
    write_json(mod_path, manifest)
    validate_manifest(mod_path)
    qmod_name = name or manifest["name"]
    if pathlib.Path(qmod_name).name != qmod_name or not qmod_name.strip():
        raise BuildError("QMOD name must be one valid file name without a directory path.")

    sources = [mod_path]
    entries = ["mod.json"]
    cover = manifest.get("coverImage")
    if cover and (ROOT / cover).is_file():
        sources.append(ROOT / cover)
        entries.append(pathlib.Path(cover).name)
    for field in ("modFiles", "lateModFiles", "libraryFiles"):
        for filename in manifest.get(field, []):
            candidate = BUILD / filename
            if not candidate.is_file():
                candidate = ROOT / "extern" / "libs" / filename
            sources.append(require_file(candidate))
            entries.append(filename)
    sources.extend(runtime_sources)
    entries.extend(source.name for source in runtime_sources)
    if len(entries) != len(set(entries)):
        duplicates = sorted({entry for entry in entries if entries.count(entry) > 1})
        raise BuildError(f"QMOD inputs contain duplicate archive names: {duplicates}")

    qmod = ROOT / f"{qmod_name}.qmod"
    temporary = ROOT / f".{qmod_name}.{uuid.uuid4().hex}.zip"
    try:
        print("Packaging and verifying the complete QMOD. Compression can take a little while.")
        write_deterministic_zip(temporary, sources, entries)
        with zipfile.ZipFile(temporary) as archive:
            actual = archive.namelist()
            if len(actual) != len(entries) or len(actual) != len(set(actual)) or sorted(actual) != sorted(entries):
                raise BuildError("Fresh QMOD entries do not exactly match mod.json and runtime inputs.")
        os.replace(temporary, qmod)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Created validated QMOD {qmod}")
    return qmod


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("prepare-quickjs")
    subcommands.add_parser("prepare-runtime")
    subcommands.add_parser("generate-manifest")
    subcommands.add_parser("validate-manifest")
    build_parser = subcommands.add_parser("build-native")
    build_parser.add_argument("--clean", action="store_true")
    package_parser = subcommands.add_parser("package")
    package_parser.add_argument("--name")
    subcommands.add_parser("validate-elf")
    subcommands.add_parser("deterministic-tool")
    args = parser.parse_args()
    try:
        if args.command == "prepare-quickjs":
            prepare_quickjs()
        elif args.command == "prepare-runtime":
            prepare_quickjs()
            prepare_downloader()
        elif args.command == "generate-manifest":
            generate_manifest()
        elif args.command == "validate-manifest":
            validate_manifest()
        elif args.command == "build-native":
            build_native(args.clean)
        elif args.command == "package":
            package_qmod(args.name)
        elif args.command == "validate-elf":
            validate_elf()
        elif args.command == "deterministic-tool":
            print(deterministic_tool())
        return 0
    except (BuildError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
