#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Policy and deterministic-package tests for the canonical Linux build."""

from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import sys
import tempfile
import zipfile


root = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent


def load(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


pipeline = load("bigscreen_build_pipeline", root / "scripts" / "build_pipeline.py")
policy = load("bigscreen_quest_policy", root / "scripts" / "quest_policy.py")


assert not policy.version_satisfies("4.6.4", "^4.8.0")
assert not policy.version_satisfies("4.7.0", "^4.8.0")
assert policy.version_satisfies("4.8.0", "^4.8.0")
assert policy.version_satisfies("4.9.2", "^4.8.0")
assert not policy.version_satisfies("5.0.0", "^4.8.0")
assert policy.version_satisfies("0.4.55", "^0.4.54")
assert not policy.version_satisfies("0.5.0", "^0.4.54")

state_cases = (
    ("NOT_INSTALLED", dict(complete_receipt=False, partial_receipt=False, mbf_metadata=False, mbf_payload_complete=False, legacy_phase_copies=0)),
    ("SOURCE_MANAGED", dict(complete_receipt=True, partial_receipt=False, mbf_metadata=False, mbf_payload_complete=False, legacy_phase_copies=1)),
    ("SOURCE_PARTIAL", dict(complete_receipt=False, partial_receipt=True, mbf_metadata=False, mbf_payload_complete=False, legacy_phase_copies=1)),
    ("MBF_MANAGED", dict(complete_receipt=False, partial_receipt=False, mbf_metadata=True, mbf_payload_complete=True, legacy_phase_copies=1)),
    ("MBF_REGISTERED_NOT_INSTALLED", dict(complete_receipt=False, partial_receipt=False, mbf_metadata=True, mbf_payload_complete=False, legacy_phase_copies=0)),
    ("LEGACY_SOURCE", dict(complete_receipt=False, partial_receipt=False, mbf_metadata=False, mbf_payload_complete=False, legacy_phase_copies=1)),
    ("LEGACY_SOURCE", dict(complete_receipt=False, partial_receipt=False, mbf_metadata=False, mbf_payload_complete=False, legacy_phase_copies=0, legacy_runtime=True)),
    ("MIXED_OR_AMBIGUOUS", dict(complete_receipt=False, partial_receipt=False, mbf_metadata=False, mbf_payload_complete=False, legacy_phase_copies=2)),
    ("MIXED_OR_AMBIGUOUS", dict(complete_receipt=True, partial_receipt=False, mbf_metadata=True, mbf_payload_complete=True, legacy_phase_copies=1)),
)
for expected, arguments in state_cases:
    assert policy.resolve_install_state(**arguments) == expected

fixture = {
    "ownership": "BigScreenExclusive",
    "previousState": "present",
    "previousSha256": "baseline",
    "preDeploySha256": "old-source",
    "preDeployWasSourceOwned": True,
    "installedSha256": "source",
}
assert policy.receipt_removal_action(fixture, "source", True) == "RemoveExclusive"
assert policy.receipt_removal_action(fixture, "baseline", True) == "RemoveExclusive"
assert policy.receipt_removal_action(fixture, "old-source", True) == "RemoveExclusive"
assert policy.receipt_removal_action(fixture, "unknown", True) == "RemoveExclusive"
assert policy.receipt_removal_action(fixture, "baseline", False) == "RemoveExclusive"
assert policy.receipt_removal_action(fixture, None, False) == "AlreadyAbsent"
shared = dict(fixture, ownership="SharedDependency")
assert policy.receipt_removal_action(shared, "source", True) == "PreserveShared"

devices = policy.parse_adb_devices([
    "List of devices attached",
    # ADB 37 aligns the serial and state with spaces. Older releases commonly
    # used a tab, so both real output forms must remain accepted.
    "QUEST123         device product:hollywood model:Quest_2 device:hollywood",
    "PHONE456\tunauthorized product:e1quew model:SM-S921U1 device:e1q",
])
assert [device["serial"] for device in devices] == ["QUEST123", "PHONE456"]
assert devices[0]["model"] == "Quest 2"
assert policy.quest_identity("Oculus", "Quest 2")
assert not policy.quest_identity("Samsung", "SM-S921U1")
assert policy.select_quest([{"serial": "QUEST123"}], True)["serial"] == "QUEST123"
try:
    policy.select_quest([{"serial": "QUEST123"}, {"serial": "QUEST456"}], True)
    raise AssertionError("Multiple Quests were not rejected in noninteractive mode.")
except ValueError as error:
    assert "More than one Quest" in str(error)

manifest = {
    "_QPVersion": "0.1.1",
    "name": "Test",
    "id": "test",
    "author": "Test",
    "version": "1.2.3-alpha.1",
    "modFiles": ["libtest.so"],
    "dependencies": [],
    "fileCopies": [],
}
pipeline.validate_schema_contract(manifest)
for invalid in (
    dict(manifest, version="not-semver"),
    dict(manifest, id="bad id"),
    dict(manifest, modFiles=["same.so", "same.so"]),
):
    try:
        pipeline.validate_schema_contract(invalid)
        raise AssertionError(f"Invalid manifest was accepted: {invalid}")
    except pipeline.BuildError:
        pass

with tempfile.TemporaryDirectory(prefix="BigScreen-PipelineTests-") as temporary:
    directory = pathlib.Path(temporary)
    first = directory / "first.bin"
    second = directory / "second.txt"
    first.write_bytes(bytes((0, 1, 2, 3, 254, 255)))
    second.write_text("Big Screen deterministic archive test\n" * 4096, encoding="utf-8")
    archive_a = directory / "a.zip"
    archive_b = directory / "b.zip"
    pipeline.write_deterministic_zip(
        archive_a, [second, first], ["z/second.txt", "a/first.bin"]
    )
    pipeline.write_deterministic_zip(
        archive_b, [first, second], ["a/first.bin", "z/second.txt"]
    )
    hash_a = hashlib.sha256(archive_a.read_bytes()).hexdigest()
    hash_b = hashlib.sha256(archive_b.read_bytes()).hexdigest()
    assert hash_a == hash_b
    with zipfile.ZipFile(archive_a) as archive:
        assert archive.namelist() == ["a/first.bin", "z/second.txt"]
        assert archive.read("a/first.bin") == first.read_bytes()
        assert any(info.compress_size < info.file_size for info in archive.infolist())
        assert all(info.date_time[:3] == (2000, 1, 1) for info in archive.infolist())
    try:
        pipeline.write_deterministic_zip(
            directory / "duplicate.zip",
            [first, second],
            ["duplicate.bin", "duplicate.bin"],
        )
        raise AssertionError("Duplicate ZIP entries were not rejected.")
    except pipeline.BuildError as error:
        assert "Duplicate ZIP entry" in str(error)

remover = (root / "scripts" / "remove-bigscreen.ps1").read_text(encoding="utf-8")
for protected in (
    "BigScreen/Thumbnails", "BigScreen/Video Import",
    "library.json", "BigScreen/Logs",
):
    assert not __import__("re").search(r"rm.*" + __import__("re").escape(protected), remover)
assert "Also remove Big Screen's downloaded videos?" in remover
assert 'BigScreen/Videos"' in remover
assert "expectedVideosPath" in remover

print("Canonical build pipeline, ownership, dependency, and deterministic ZIP tests passed.")
