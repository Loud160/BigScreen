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
sys.path.insert(0, str(root / "scripts"))
quest_tool = load("bigscreen_quest_tool", root / "scripts" / "quest_tool.py")


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

# Deployment and uninstall deliberately use different ownership policies.
# Redeployment must stop on unknown bytes, while a confirmed uninstall may
# still remove a receipt-owned private path whose hash changed later.
assert policy.partial_receipt_recoverable(fixture, "source")
assert policy.partial_receipt_recoverable(fixture, "old-source")
assert policy.partial_receipt_recoverable(fixture, "baseline")
assert not policy.partial_receipt_recoverable(fixture, "unknown")
assert policy.managed_receipt_safe(fixture, "source")
assert not policy.managed_receipt_safe(fixture, "baseline")
absent_fixture = dict(fixture, previousState="absent", previousSha256=None)
assert policy.managed_receipt_safe(absent_fixture, None)
assert policy.retired_deployment_action(shared, "source", None) == "PreserveShared"
assert policy.retired_deployment_action(fixture, None, None) == "AlreadyAbsent"
assert policy.retired_deployment_action(fixture, "baseline", None) == "AlreadyRestored"
assert policy.retired_deployment_action(fixture, "unknown", "baseline") == "RefuseAmbiguous"
assert policy.retired_deployment_action(absent_fixture, "source", None) == "RemoveExclusive"
backed_fixture = dict(fixture, previousBackupPath="/baseline/source.bin")
assert policy.retired_deployment_action(backed_fixture, "source", "baseline") == "RestoreBaseline"
assert policy.retired_deployment_action(fixture, "source", None) == "RefuseMissingBaseline"

# A clean uninstall may preserve updater state and Python caches below Runtime.
# Those generated files must not force the next deploy through destructive
# legacy migration; an immutable shipped runtime marker still must.
generated_runtime_files = {
    "yt-dlp-active",
    "update-status.json",
    "download-status.json",
    "__pycache__/bigscreen_jsc_provider.cpython-314.pyc",
}
assert not policy.legacy_runtime_payload_present(
    lambda relative: relative in generated_runtime_files
)
assert policy.legacy_runtime_payload_present(
    lambda relative: relative == "runtime-manifest.json"
)

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
assert pipeline.require_matching_versions({"template": "1.2.3", "qpm": "1.2.3"}) == "1.2.3"
assert pipeline.require_matching_versions(
    {"template": "1.2.3", "qpm": "1.2.3"}, "v1.2.3"
) == "1.2.3"
for versions, tag in (
    ({}, None),
    ({"template": None, "qpm": None}, None),
    ({"template": "1.2.3", "qpm": "1.2.4"}, None),
    ({"template": "1.2.3", "qpm": "1.2.3"}, "v1.2.4"),
):
    try:
        pipeline.require_matching_versions(versions, tag)
        raise AssertionError("Mismatched package/release versions were accepted.")
    except pipeline.BuildError:
        pass
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

    payload = directory / "payload"
    payload.mkdir()
    for name in ("libtest.so", "libdependency.so"):
        (payload / name).write_bytes(name.encode("utf-8"))
    payload_manifest = {
        "modFiles": ["libtest.so"],
        "libraryFiles": ["libdependency.so"],
    }
    pipeline.validate_staged_native_payload(payload_manifest, payload)
    (payload / "libunexpected.so").write_bytes(b"unexpected")
    try:
        pipeline.validate_staged_native_payload(payload_manifest, payload)
        raise AssertionError("An undeclared staged native library was accepted.")
    except pipeline.BuildError as error:
        assert "unexpected" in str(error)

# The bootstrap runs this before invoking the linker. Run it again through the
# policy suite so a stale pin file, QPM URL, restored file set, or digest fails
# ordinary CI even when bootstrap behavior is later refactored.
pipeline.validate_qpm_native_inputs()

remover = (root / "scripts" / "remove-bigscreen.ps1").read_text(encoding="utf-8")
python_tool = (root / "scripts" / "quest_tool.py").read_text(encoding="utf-8")
protected = {
    f"{quest_tool.MOD_DATA}/BigScreen/Thumbnails",
    f"{quest_tool.MOD_DATA}/BigScreen/Video Import",
    f"{quest_tool.MOD_DATA}/BigScreen/library.json",
    f"{quest_tool.MOD_DATA}/BigScreen/Logs",
}
assert quest_tool.PRESERVED_USER_PATHS == protected
assert protected.isdisjoint(quest_tool.RECURSIVE_REMOVAL_ROOTS)
assert quest_tool.RECURSIVE_REMOVAL_ROOTS == {
    quest_tool.RUNTIME_ROOT,
    f"{quest_tool.MOD_DATA}/BigScreen/Videos",
    quest_tool.SOURCE_ROOT,
}

class RefusingAdb:
    def shell(self, _command):
        raise AssertionError("ADB must not run for a path outside the allowlist.")

try:
    quest_tool.remove_owned_tree(
        RefusingAdb(), f"{quest_tool.MOD_DATA}/BigScreen/Thumbnails"
    )
    raise AssertionError("A preserved user path entered recursive removal.")
except quest_tool.QuestToolError:
    pass

assert "$script:BigScreenRecursiveRemovalRoots" in remover + (
    root / "scripts" / "source-install-ownership.ps1"
).read_text(encoding="utf-8")
assert "$script:BigScreenPreservedUserPaths" in (
    root / "scripts" / "source-install-ownership.ps1"
).read_text(encoding="utf-8")
assert "Remove-BigScreenOwnedTree" in remover
assert python_tool.count('adb.shell(f"rm -rf') == 1
assert "Also remove Big Screen's downloaded videos?" in remover
assert 'BigScreen/Videos"' in remover
assert "expectedVideosPath" in remover
assert "Remove-BigScreenOwnedTree $script:SourceInstallRoot" in remover
assert "remove_owned_tree(adb, SOURCE_ROOT)" in python_tool

print("Canonical build pipeline, ownership, dependency, and deterministic ZIP tests passed.")
