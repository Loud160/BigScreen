#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Linux-native Quest deployment, removal, and support-log workflows."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

from quest_policy import (
    parse_adb_devices,
    quest_identity,
    receipt_removal_action,
    resolve_install_state,
    version_satisfies,
)


ROOT = pathlib.Path(__file__).resolve().parent.parent
PACKAGE = "com.beatgames.beatsaber"
MOD_DATA = f"/sdcard/ModData/{PACKAGE}"
SOURCE_ROOT = f"{MOD_DATA}/BigScreen/SourceInstall"
COMPLETE_RECEIPT = f"{SOURCE_ROOT}/source-install.json"
PARTIAL_RECEIPT = f"{SOURCE_ROOT}/source-install.partial.json"
BASELINE_ROOT = f"{SOURCE_ROOT}/Baseline"
RUNTIME_ROOT = f"{MOD_DATA}/BigScreen/Runtime"
QUEST_READY_TIMEOUT_SECONDS = 30.0
QUEST_READY_POLL_SECONDS = 0.5


class QuestToolError(RuntimeError):
    pass


def local_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_remote(path: str) -> str:
    if (
        not path.startswith(MOD_DATA + "/")
        or any(character in path for character in "'\"\r\n")
        or ".." in path.split("/")
    ):
        raise QuestToolError(f"Unsafe Quest path in Big Screen metadata: {path}")
    return path


class Adb:
    def __init__(self) -> None:
        self.executable = os.environ.get("BIGSCREEN_ADB_EXECUTABLE") or shutil.which("adb")
        if not self.executable:
            raise QuestToolError("ADB was not found. Run the launcher so portable ADB can be prepared.")
        try:
            subprocess.run(
                [self.executable, "start-server"], check=True, text=True,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
        except subprocess.CalledProcessError as error:
            detail = (error.stdout or "").strip()
            raise QuestToolError(
                f"ADB could not start.{f' {detail}' if detail else ''}"
            ) from error
        self.serial = self.select_quest()

    def run(
        self,
        *arguments: str,
        check: bool = True,
        text: bool = True,
        capture: bool = True,
    ) -> subprocess.CompletedProcess:
        command = [self.executable]
        if self.serial:
            command += ["-s", self.serial]
        command += list(arguments)
        return subprocess.run(
            command,
            check=check,
            text=text,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
        )

    def probe(self, serial: str, *arguments: str) -> str:
        result = subprocess.run(
            [self.executable, "-s", serial, *arguments], text=True,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        )
        return result.stdout.strip() if result.returncode == 0 else ""

    def select_quest(self) -> str:
        deadline = time.monotonic() + QUEST_READY_TIMEOUT_SECONDS
        candidates: list[dict[str, str]] = []
        devices: list[dict[str, str]] = []
        announced_wait = False
        while True:
            listing = subprocess.run(
                [self.executable, "devices", "-l"], check=True, text=True,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            ).stdout
            devices = parse_adb_devices(listing.splitlines())
            authorized = [device for device in devices if device["state"] == "device"]
            candidates = []
            for device in authorized:
                serial = device["serial"]
                model = self.probe(serial, "shell", "getprop", "ro.product.model") or device["model"]
                manufacturer = self.probe(serial, "shell", "getprop", "ro.product.manufacturer")
                package = self.probe(serial, "shell", "pm", "path", PACKAGE)
                if quest_identity(manufacturer, model) and package.startswith("package:"):
                    candidates.append({"serial": serial, "model": model or "Meta Quest"})
            if candidates or time.monotonic() >= deadline:
                break
            if not announced_wait:
                unauthorized = any(device["state"] == "unauthorized" for device in devices)
                if unauthorized:
                    print(
                        "Quest detected; waiting up to 30 seconds for USB-debugging authorization. "
                        "Put on the headset and accept the prompt."
                    )
                else:
                    print("Waiting up to 30 seconds for an authorized Quest with Beat Saber installed.")
                announced_wait = True
            time.sleep(QUEST_READY_POLL_SECONDS)
        if not candidates:
            unauthorized = any(device["state"] == "unauthorized" for device in devices)
            authorized = any(device["state"] == "device" for device in devices)
            if unauthorized:
                raise QuestToolError(
                    "A Quest was detected but USB debugging was not authorized. "
                    "Put on the headset and accept the prompt."
                )
            if authorized:
                raise QuestToolError(
                    "ADB found authorized Android devices, but none identified as a Quest "
                    "with Beat Saber installed."
                )
            raise QuestToolError(
                "No Quest was detected through ADB. Connect the headset with a data-capable "
                "USB cable, enable Developer Mode, and accept its USB-debugging prompt."
            )
        if len(candidates) > 1:
            if not sys.stdin.isatty():
                raise QuestToolError("More than one Quest is connected and interactive selection is unavailable.")
            print("More than one Quest with Beat Saber is connected:")
            for index, candidate in enumerate(candidates, 1):
                print(f"  [{index}] {candidate['model']} - serial {candidate['serial']}")
            while True:
                answer = input(f"Choose the Quest to use [1-{len(candidates)}] (Enter cancels): ").strip()
                if not answer:
                    raise QuestToolError("Quest selection was cancelled. No device was changed.")
                if answer.isdigit() and 1 <= int(answer) <= len(candidates):
                    selected = candidates[int(answer) - 1]
                    break
        else:
            selected = candidates[0]
        print(f"Using {selected['model']} ({selected['serial']}).")
        return selected["serial"]

    def shell(self, command: str, check: bool = True) -> str:
        return self.run("shell", command, check=check).stdout.strip()

    def file_exists(self, path: str) -> bool:
        safe_remote(path)
        return self.run("shell", f"test -f '{path}'", check=False).returncode == 0

    def directory_exists(self, path: str) -> bool:
        safe_remote(path + "/probe")
        return self.run("shell", f"test -d '{path}'", check=False).returncode == 0

    def remote_hash(self, path: str) -> str | None:
        if not self.file_exists(path):
            return None
        output = self.shell(f"sha256sum '{path}'", check=False)
        match = re.match(r"([0-9a-fA-F]{64})", output)
        return match.group(1).lower() if match else None

    def read_json(self, path: str) -> dict | None:
        if not self.file_exists(path):
            return None
        result = self.run("exec-out", "cat", path, check=False)
        if result.returncode or not result.stdout.strip():
            return None
        try:
            return json.loads(result.stdout)
        except json.JSONDecodeError:
            return None

    def push(self, local: pathlib.Path, remote: str) -> None:
        safe_remote(remote)
        parent = remote.rsplit("/", 1)[0]
        self.shell(f"mkdir -p '{parent}'")
        self.run("push", str(local), remote, capture=False)

    def write_json(self, value: dict, remote: str) -> None:
        safe_remote(remote)
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".json", delete=False) as stream:
            json.dump(value, stream, separators=(",", ":"))
            temporary = pathlib.Path(stream.name)
        try:
            upload = remote + ".upload"
            self.push(temporary, upload)
            self.shell(f"mv '{upload}' '{remote}'")
        finally:
            temporary.unlink(missing_ok=True)


def manifest() -> dict:
    return json.loads((ROOT / "mod.json").read_text(encoding="utf-8"))


def required_paths(package_manifest: dict) -> list[str]:
    paths: list[str] = []
    for name in package_manifest.get("modFiles", []):
        paths.append(f"{MOD_DATA}/Modloader/early_mods/{pathlib.PurePosixPath(name).name}")
    for name in package_manifest.get("lateModFiles", []):
        paths.append(f"{MOD_DATA}/Modloader/mods/{pathlib.PurePosixPath(name).name}")
    for name in package_manifest.get("libraryFiles", []):
        paths.append(f"{MOD_DATA}/Modloader/libs/{pathlib.PurePosixPath(name).name}")
    for copy in package_manifest.get("fileCopies", []):
        paths.append(safe_remote(copy["destination"]))
    return sorted(set(paths))


def package_manifests(adb: Adb, game_version: str) -> list[tuple[str, dict]]:
    root = f"{MOD_DATA}/Packages/{game_version}"
    safe_remote(root + "/probe")
    if not adb.directory_exists(root):
        return []
    output = adb.shell(f"find '{root}' -type f -name mod.json -print 2>/dev/null", check=False)
    packages: list[tuple[str, dict]] = []
    for path in output.splitlines():
        path = path.strip()
        if not path:
            continue
        parsed = adb.read_json(safe_remote(path))
        if parsed:
            packages.append((path, parsed))
    return packages


def dependency_statuses(adb: Adb, current: dict) -> list[dict]:
    packages = package_manifests(adb, current["packageVersion"])
    statuses: list[dict] = []
    for requirement in current.get("dependencies", []):
        matches = [package for _, package in packages if package.get("id") == requirement["id"]]
        compatible = [package for package in matches if version_satisfies(package.get("version", ""), requirement["version"])]
        complete = [package for package in compatible if all(adb.file_exists(path) for path in required_paths(package))]
        if complete:
            statuses.append({
                "id": requirement["id"], "range": requirement["version"],
                "installed": complete[0].get("version", "unknown"),
                "satisfied": True, "message": "",
            })
            continue
        if compatible:
            message = "a compatible registration exists, but one or more required files are missing"
        elif matches:
            message = (
                f"installed version {matches[0].get('version', 'unknown')} does not satisfy "
                f"{requirement['version']}"
            )
        else:
            message = "it is not installed or registered for this Beat Saber version"
        statuses.append({
            "id": requirement["id"], "range": requirement["version"],
            "installed": matches[0].get("version", "") if matches else "",
            "satisfied": False, "message": message,
        })
    return statuses


def dependency_diagnosis(adb: Adb, current: dict) -> tuple[str, bool]:
    statuses = dependency_statuses(adb, current)
    failures = [status for status in statuses if not status["satisfied"]]
    lines = [
        "BIG SCREEN DEPENDENCY DIAGNOSIS",
        "================================",
        "",
        (
            "RESULT: All dependencies declared by this Big Screen build are registered, "
            "compatible, and complete."
            if not failures else
            "RESULT: Big Screen may have been prevented from loading by the dependency "
            "problem(s) below."
        ),
        "",
    ]
    for status in statuses:
        if status["satisfied"]:
            lines.append(
                f"OK: {status['id']} {status['installed']} satisfies required range "
                f"{status['range']}."
            )
        else:
            lines.append(
                f"PROBLEM: {status['id']} requires {status['range']}; {status['message']}."
            )
    if failures:
        lines += [
            "",
            "Recommended action: Open ModsBeforeFriday and update or reinstall the "
            "dependencies marked PROBLEM. Do not replace one shared library manually "
            "because other mods may depend on it too.",
        ]
    lines += [
        "",
        "This snapshot is produced by the external support collector. It remains "
        "available even when Big Screen could not start its own logger or show an "
        "in-game popup.",
        "",
    ]
    return "\n".join(lines), bool(failures)


def assert_dependencies(adb: Adb, current: dict) -> None:
    statuses = dependency_statuses(adb, current)
    failures = [
        f"{status['id']} {status['range']}: {status['message']}"
        for status in statuses if not status["satisfied"]
    ]
    if failures:
        raise QuestToolError(
            "Quest dependencies are not ready; install/update them through MBF before source deployment:\n  - "
            + "\n  - ".join(failures)
        )
    print("Quest shared dependency registrations and payloads are compatible and complete.")


def mbf_bigscreen(adb: Adb, game_version: str) -> list[tuple[str, dict, list[str]]]:
    found = []
    for path, package in package_manifests(adb, game_version):
        if package.get("id") == "bigscreen":
            paths = required_paths(package)
            found.append((path, package, paths))
    return found


def classification(adb: Adb, game_version: str) -> dict:
    complete_exists = adb.file_exists(COMPLETE_RECEIPT)
    partial_exists = adb.file_exists(PARTIAL_RECEIPT)
    complete = adb.read_json(COMPLETE_RECEIPT) if complete_exists else None
    partial = adb.read_json(PARTIAL_RECEIPT) if partial_exists else None
    mbf = mbf_bigscreen(adb, game_version)
    mbf_complete = any(paths and all(adb.file_exists(path) for path in paths) for _, _, paths in mbf)
    legacy_candidates = (
        f"{MOD_DATA}/Modloader/early_mods/libbigscreen.so",
        f"{MOD_DATA}/Modloader/mods/libbigscreen.so",
        f"{MOD_DATA}/Mods/libbigscreen.so",
    )
    legacy = [path for path in legacy_candidates if adb.file_exists(path)]
    receipt_paths = {
        item.get("path") for receipt in (complete, partial) if receipt
        for item in receipt.get("files", [])
    }
    unexpected = [path for path in legacy if (complete_exists or partial_exists) and path not in receipt_paths]
    state = resolve_install_state(
        complete_receipt=complete_exists,
        partial_receipt=partial_exists,
        mbf_metadata=bool(mbf),
        mbf_payload_complete=mbf_complete,
        legacy_phase_copies=len(legacy),
        legacy_runtime=adb.directory_exists(RUNTIME_ROOT),
        receipt_unreadable=(complete_exists and complete is None) or (partial_exists and partial is None),
        unexpected_phase_copy=bool(unexpected),
    )
    return {
        "state": state, "complete": complete, "partial": partial, "mbf": mbf,
        "legacy": legacy, "unexpected": unexpected,
    }


def exclusive_library(name: str) -> bool:
    return name.startswith((
        "libbigscreen-", "libavformat-bigscreen", "libavcodec-bigscreen",
        "libavutil-bigscreen", "libswscale-bigscreen",
    )) or name in {
        "libpython3.14.so", "libssl_python.so", "libcrypto_python.so", "libsqlite3_python.so"
    }


def deployment_plan(current: dict) -> list[dict]:
    plan: list[dict] = []
    for field, remote_directory, category in (
        ("modFiles", f"{MOD_DATA}/Modloader/early_mods", "EarlyMod"),
        ("lateModFiles", f"{MOD_DATA}/Modloader/mods", "LateMod"),
    ):
        for name in current.get(field, []):
            plan.append({
                "local": ROOT / "build" / name, "path": f"{remote_directory}/{name}",
                "category": category, "ownership": "BigScreenExclusive",
            })
    for name in current.get("libraryFiles", []):
        local = ROOT / "build" / name
        if not local.is_file():
            local = ROOT / "extern" / "libs" / name
        plan.append({
            "local": local, "path": f"{MOD_DATA}/Modloader/libs/{name}",
            "category": "Library",
            "ownership": "BigScreenExclusive" if exclusive_library(name) else "SharedDependency",
        })
    for copy in current.get("fileCopies", []):
        destination = safe_remote(copy["destination"])
        if not destination.startswith(RUNTIME_ROOT + "/"):
            raise QuestToolError(f"Unrecognized development fileCopy destination: {destination}")
        relative = destination[len(RUNTIME_ROOT) + 1:]
        plan.append({
            "local": ROOT / "build" / "downloader" / pathlib.PurePosixPath(relative),
            "path": destination, "category": "DownloaderRuntime",
            "ownership": "BigScreenExclusive",
        })
    for item in plan:
        if not item["local"].is_file():
            raise QuestToolError(f"Required deployment input is missing: {item['local']}")
    return plan


def make_receipt(adb: Adb, plan: list[dict], current: dict, prior: dict | None) -> dict:
    prior_by_path = {item["path"]: item for item in prior.get("files", [])} if prior else {}
    files = []
    for item in plan:
        old = prior_by_path.get(item["path"])
        current_hash = adb.remote_hash(item["path"])
        if old:
            previous_state = old.get("previousState")
            previous_hash = old.get("previousSha256")
            backup = old.get("previousBackupPath")
        else:
            previous_state = "present" if current_hash else "absent"
            previous_hash = current_hash
            backup = (
                f"{BASELINE_ROOT}/{current_hash}.bin"
                if current_hash and item["ownership"] == "BigScreenExclusive" else None
            )
        source_hashes = []
        if old:
            source_hashes.append(old.get("installedSha256"))
            if old.get("preDeployWasSourceOwned"):
                source_hashes.append(old.get("preDeploySha256"))
        files.append({
            "path": item["path"], "category": item["category"],
            "ownership": item["ownership"], "previousState": previous_state,
            "previousSha256": previous_hash, "previousBackupPath": backup,
            "preDeployState": "present" if current_hash else "absent",
            "preDeploySha256": current_hash,
            "preDeployWasSourceOwned": bool(current_hash and current_hash in source_hashes),
            "installedSha256": local_sha256(item["local"]), "copyCompleted": False,
        })
    return {
        "schemaVersion": 1, "state": "partial", "modId": "bigscreen",
        "bigScreenVersion": current["version"], "sourceCommit": "source-archive",
        "buildType": "Release", "gameVersion": current["packageVersion"],
        "installedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(), "files": files,
    }


def reconcile_item(adb: Adb, item: dict, partial: bool) -> str | None:
    action = receipt_removal_action(item, adb.remote_hash(item["path"]), partial)
    if action in {"PreserveShared", "AlreadyAbsent"}:
        return None
    # Removal is intentionally based on the receipt's ownership classification,
    # not the installed hash. Big Screen must remain removable after a partial
    # update, manual replacement, or other change to one of its private files.
    # Shared dependencies never reach this branch.
    adb.shell(f"rm -f -- '{safe_remote(item['path'])}'")
    return item["path"] if adb.file_exists(item["path"]) else None


def deploy() -> None:
    current = manifest()
    stamp = ROOT / "build" / ".bigscreen-build-success"
    stamp_text = stamp.read_text(encoding="utf-8") if stamp.is_file() else ""
    match = re.search(r"^binarySha256=([0-9a-fA-F]{64})\s*$", stamp_text, re.MULTILINE)
    library = ROOT / "build" / "libbigscreen.so"
    if not match or not library.is_file() or local_sha256(library) != match.group(1).lower():
        raise QuestToolError("The native build does not match its verified success stamp; deployment is blocked.")
    adb = Adb()
    assert_dependencies(adb, current)
    state = classification(adb, current["packageVersion"])
    print(f"Detected Big Screen installation state: {state['state']}")
    if state["state"] in {"MBF_MANAGED", "MBF_REGISTERED_NOT_INSTALLED"}:
        raise QuestToolError("Big Screen is registered with MBF. Remove it through MBF before source deployment.")
    if state["state"] == "MIXED_OR_AMBIGUOUS":
        raise QuestToolError("Big Screen source/MBF ownership is mixed or ambiguous; no Quest files were changed.")
    plan = deployment_plan(current)
    prior = state["partial"] if state["state"] == "SOURCE_PARTIAL" else state["complete"]
    if state["state"] == "LEGACY_SOURCE":
        if not sys.stdin.isatty() or input(
            "A legacy pre-receipt source install was found. Clean its exact private payload and continue? [Y/N] "
        ).strip().lower() not in {"y", "yes"}:
            print("No Quest files were changed.")
            return
        private_paths = set(state["legacy"])
        private_paths.update(item["path"] for item in plan if item["ownership"] == "BigScreenExclusive")
        for path in private_paths:
            adb.shell(f"rm -f -- '{safe_remote(path)}'")
        if adb.directory_exists(RUNTIME_ROOT):
            adb.shell(f"rm -rf -- '{RUNTIME_ROOT}'")
        prior = None
    receipt = make_receipt(adb, plan, current, prior)
    adb.write_json(receipt, PARTIAL_RECEIPT)
    current_paths = {item["path"] for item in plan}
    if prior:
        ambiguous = [
            path for item in prior.get("files", []) if item["path"] not in current_paths
            for path in [reconcile_item(adb, item, True)] if path
        ]
        if ambiguous:
            raise QuestToolError(f"Retired source paths changed externally and were preserved: {ambiguous}")
    by_path = {item["path"]: item for item in plan}
    print("Deploying and hash-verifying Big Screen's complete source payload.")
    for receipt_item in receipt["files"]:
        if receipt_item.get("previousBackupPath") and not adb.file_exists(receipt_item["previousBackupPath"]):
            adb.shell(
                f"mkdir -p '{BASELINE_ROOT}' && cp '{safe_remote(receipt_item['path'])}' "
                f"'{safe_remote(receipt_item['previousBackupPath'])}'"
            )
            if adb.remote_hash(receipt_item["previousBackupPath"]) != receipt_item["previousSha256"]:
                raise QuestToolError(f"Could not preserve the baseline for {receipt_item['path']}.")
        plan_item = by_path[receipt_item["path"]]
        adb.push(plan_item["local"], receipt_item["path"])
        if adb.remote_hash(receipt_item["path"]) != receipt_item["installedSha256"]:
            raise QuestToolError(f"Deployment verification failed for {receipt_item['path']}.")
        receipt_item["copyCompleted"] = True
        adb.write_json(receipt, PARTIAL_RECEIPT)
        print(f"Verified: {receipt_item['path']}")
    receipt["state"] = "complete"
    adb.write_json(receipt, PARTIAL_RECEIPT)
    adb.shell(f"mv '{PARTIAL_RECEIPT}' '{COMPLETE_RECEIPT}'")
    adb.shell(f"am force-stop '{PACKAGE}'")
    adb.shell(f"am start '{PACKAGE}/com.unity3d.player.UnityPlayerActivity'")
    print("Big Screen source deployment completed and Beat Saber was asked to restart.")


def remove(
    confirm: bool,
    remove_settings: bool,
    remove_videos: bool,
    noninteractive: bool,
) -> None:
    current = json.loads((ROOT / "mod.template.json").read_text(encoding="utf-8"))
    adb = Adb()
    state = classification(adb, current["packageVersion"])
    print(f"Detected Big Screen installation state: {state['state']}")
    if state["state"] == "NOT_INSTALLED":
        print("No source-managed Big Screen installation was found; user data was not changed.")
        return
    if state["state"] in {"MBF_MANAGED", "MBF_REGISTERED_NOT_INSTALLED", "MIXED_OR_AMBIGUOUS"}:
        raise QuestToolError("This installation is MBF-managed or ownership is ambiguous; remove/repair it through MBF.")
    print(
        "Big Screen will be removed. Settings and managed downloads are preserved "
        "unless their separate prompts are accepted; map-folder videos, Video Import "
        "files, library data, thumbnails, and logs are always preserved."
    )
    if not confirm:
        if noninteractive:
            raise QuestToolError("Removal confirmation is required.")
        if input("Continue removing the source installation? [Y/N] ").strip().lower() not in {"y", "yes"}:
            print("No Quest files were changed.")
            return
    if not noninteractive and not remove_settings:
        remove_settings = input("Also remove Big Screen settings? [y/N] ").strip().lower() in {"y", "yes"}
    if not noninteractive and not remove_videos:
        print(
            "This removes only videos downloaded and managed by Big Screen. "
            "Map-folder videos and files in Video Import will be preserved."
        )
        remove_videos = input("Also remove Big Screen's downloaded videos? [y/N] ").strip().lower() in {
            "y", "yes"
        }
    adb.shell(f"am force-stop '{PACKAGE}'")
    failed: list[str] = []
    receipt = state["partial"] if state["state"] == "SOURCE_PARTIAL" else state["complete"]
    if receipt:
        partial = state["state"] == "SOURCE_PARTIAL"
        for item in receipt.get("files", []):
            path = reconcile_item(adb, item, partial)
            if path:
                failed.append(path)
    elif state["state"] == "LEGACY_SOURCE":
        for path in state["legacy"]:
            adb.shell(f"rm -f -- '{safe_remote(path)}'")
        if adb.directory_exists(RUNTIME_ROOT):
            adb.shell(f"rm -rf -- '{RUNTIME_ROOT}'")
    if failed:
        raise QuestToolError(f"Big Screen files could not be removed from the Quest: {failed}")
    # Receipt-owned runtime files have been reconciled above. Remove only the
    # directories that are now empty, deepest first; an unexpected file is
    # deliberately preserved instead of being swept up by recursive deletion.
    adb.shell(f"find '{RUNTIME_ROOT}' -depth -type d -empty -delete 2>/dev/null", check=False)
    if remove_settings:
        adb.shell(f"rm -f -- '{MOD_DATA}/Configs/bigscreen.json'")
        print("Big Screen settings were removed by explicit request.")
    else:
        print("Big Screen settings were preserved.")
    if remove_videos:
        videos = f"{MOD_DATA}/BigScreen/Videos"
        adb.shell(f"rm -rf -- '{safe_remote(videos)}'")
        if adb.directory_exists(videos):
            raise QuestToolError("Big Screen's downloaded-video directory could not be removed.")
        print("Big Screen-managed downloaded videos were removed by explicit request.")
    else:
        print("Big Screen-managed downloaded videos were preserved.")
    adb.shell(f"rm -rf -- '{SOURCE_ROOT}'")
    print("Source installation removed. Map-folder videos, Video Import files, and logs were preserved.")


def collect_logs(since_minutes: int, output_root: pathlib.Path) -> pathlib.Path:
    adb = Adb()
    now = dt.datetime.now().astimezone()
    folder = output_root / f"BigScreen-Support-{now:%Y%m%d-%H%M%S}"
    folder.mkdir(parents=True)
    commands = {
        "adb-devices.txt": [adb.executable, "devices", "-l"],
        "beat-saber-exit-info.txt": [adb.executable, "-s", adb.serial, "shell", "dumpsys", "activity", "exit-info", PACKAGE],
        "quest-crash-buffer.txt": [adb.executable, "-s", adb.serial, "logcat", "-b", "crash", "-d"],
        "quest-logcat.txt": [adb.executable, "-s", adb.serial, "logcat", "-d", "-v", "threadtime"],
        "quest-dropbox.txt": [adb.executable, "-s", adb.serial, "shell", "dumpsys", "dropbox", "--print"],
    }
    for name, command in commands.items():
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (folder / name).write_text(result.stdout, encoding="utf-8", errors="replace")
    try:
        diagnosis_text, dependency_failure = dependency_diagnosis(adb, manifest())
    except (OSError, ValueError, KeyError, QuestToolError, subprocess.CalledProcessError) as error:
        diagnosis_text = f"The collector could not complete the dependency audit: {error}\n"
        dependency_failure = False
    (folder / "DEPENDENCY-DIAGNOSIS.txt").write_text(
        diagnosis_text, encoding="utf-8"
    )
    remote_roots = (
        f"{MOD_DATA}/BigScreen/Logs",
        f"/sdcard/Android/data/{PACKAGE}/files",
    )
    pulled = []
    for remote_root in remote_roots:
        listing = adb.run(
            "shell", f"find '{remote_root}' -type f -mmin -{since_minutes} -print 2>/dev/null",
            check=False,
        ).stdout
        for remote in listing.splitlines():
            remote = remote.strip()
            if not remote:
                continue
            local = folder / "recent-files" / remote.lstrip("/").replace("/", "__")
            local.parent.mkdir(parents=True, exist_ok=True)
            result = adb.run("pull", remote, str(local), check=False, capture=True)
            if result.returncode == 0 and local.is_file():
                pulled.append(remote)
    dependency_summary = (
        "DEPENDENCY PROBLEM FOUND: Review DEPENDENCY-DIAGNOSIS.txt first.\n\n"
        if dependency_failure else
        "Dependency snapshot: review DEPENDENCY-DIAGNOSIS.txt.\n\n"
    )
    report = (
        "Big Screen support bundle\n"
        f"Collected: {now.isoformat()}\n"
        f"Requested incident window: last {since_minutes} minutes\n"
        f"Selected Quest: {adb.serial}\n"
        f"Fresh device files pulled: {len(pulled)}\n\n"
        + dependency_summary
        +
        "The logcat, crash-buffer, exit-info, and Dropbox snapshots are current command output.\n"
        "Files under recent-files passed the requested freshness window; absence is recorded rather than replaced with stale data.\n"
    )
    (folder / "REPORT.txt").write_text(report, encoding="utf-8")
    (folder / "manifest.json").write_text(
        json.dumps({
            "collectedAt": now.isoformat(), "sinceMinutes": since_minutes,
            "questSerial": adb.serial, "freshRemoteFiles": pulled,
        }, indent=2), encoding="utf-8",
    )
    archive_path = output_root / f"{folder.name}.zip"
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(item for item in folder.rglob("*") if item.is_file()):
            archive.write(path, path.relative_to(folder).as_posix())
    shutil.rmtree(folder)
    print(f"Support bundle created: {archive_path}")
    return archive_path


def print_quest_connection_recovery(error: Exception) -> None:
    """Explain how to recover after ADB could not select an authorized Quest."""
    print(f"\nQuest connection check failed: {error}", file=sys.stderr)
    print(
        """
To authorize and retry:
  1. Put on the Quest and keep the headset awake and unlocked.
  2. Confirm Developer Mode is enabled for the headset.
  3. Disconnect and reconnect the USB data cable if no prompt is visible.
  4. In the USB debugging prompt, select "Always allow from this computer"
     if desired, then choose Allow. The ordinary USB/file-access notice is not
     the USB-debugging authorization prompt.
  5. Return to this terminal and choose Retry.
""".strip()
    )


def prompt_quest_connection_retry(input_function=input) -> bool:
    """Return True for Retry and False for Cancel, rejecting ambiguous input."""
    while True:
        answer = input_function("Retry the Quest connection check or cancel [R/C]? ").strip().lower()
        if answer in {"r", "retry"}:
            return True
        if answer in {"c", "cancel", ""}:
            return False
        print("Enter R to retry or C to cancel.")


def check_quest_connection(allow_retry: bool) -> None:
    """Require a usable Quest, with interactive recovery for one-click deploys."""
    while True:
        try:
            Adb()
            print("Quest connection preflight passed.")
            return
        except (QuestToolError, OSError, subprocess.CalledProcessError, ValueError) as error:
            # Non-interactive callers must fail deterministically instead of
            # blocking forever. The public terminal launcher opts into retry;
            # automated checks retain the original one-attempt behavior.
            if not allow_retry or not sys.stdin.isatty():
                raise
            print_quest_connection_recovery(error)
            if not prompt_quest_connection_retry():
                raise QuestToolError(
                    "Quest connection was cancelled. No build or deployment was started."
                ) from error


def main() -> int:
    parser = argparse.ArgumentParser()
    subcommands = parser.add_subparsers(dest="command", required=True)
    check_parser = subcommands.add_parser("check")
    check_parser.add_argument(
        "--retry", action="store_true",
        help="offer interactive retry instructions until the Quest is ready",
    )
    subcommands.add_parser("deploy")
    remove_parser = subcommands.add_parser("remove")
    remove_parser.add_argument("--confirm", action="store_true")
    remove_parser.add_argument("--remove-settings", action="store_true")
    remove_parser.add_argument("--remove-videos", action="store_true")
    remove_parser.add_argument("--noninteractive", action="store_true")
    logs = subcommands.add_parser("collect-logs")
    logs.add_argument("--since-minutes", type=int, default=30)
    logs.add_argument("--output-root", type=pathlib.Path,
                      default=ROOT / "BigScreen Support Logs")
    args = parser.parse_args()
    try:
        if args.command == "check":
            check_quest_connection(args.retry)
        elif args.command == "deploy":
            deploy()
        elif args.command == "remove":
            remove(args.confirm, args.remove_settings, args.remove_videos, args.noninteractive)
        elif args.command == "collect-logs":
            if args.since_minutes < 1:
                raise QuestToolError("The support window must be at least one minute.")
            args.output_root.mkdir(parents=True, exist_ok=True)
            collect_logs(args.since_minutes, args.output_root.resolve())
        return 0
    except (QuestToolError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
