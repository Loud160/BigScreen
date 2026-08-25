#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Shared pure policy used by Linux Quest tools and host tests.

ADB transport is intentionally kept outside this module.  These functions are
deterministic and side-effect free, which lets the build prove device
selection, dependency ranges, install ownership, and removal decisions without
connecting to a headset.
"""

from __future__ import annotations

from dataclasses import dataclass
import re


@dataclass(frozen=True)
class SemanticVersion:
    major: int
    minor: int
    patch: int
    prerelease: str = ""


def semantic_version(text: str) -> SemanticVersion:
    value = text.strip()
    if value.lower().startswith("v"):
        value = value[1:]
    match = re.fullmatch(
        r"(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?",
        value,
    )
    if not match:
        raise ValueError(f"Unsupported semantic version {text!r}.")
    return SemanticVersion(
        int(match.group(1)), int(match.group(2)), int(match.group(3)), match.group(4) or ""
    )


def compare_versions(left: SemanticVersion, right: SemanticVersion) -> int:
    left_core = (left.major, left.minor, left.patch)
    right_core = (right.major, right.minor, right.patch)
    if left_core != right_core:
        return -1 if left_core < right_core else 1
    if not left.prerelease and not right.prerelease:
        return 0
    if not left.prerelease:
        return 1
    if not right.prerelease:
        return -1
    left_pre = left.prerelease.lower()
    right_pre = right.prerelease.lower()
    return (left_pre > right_pre) - (left_pre < right_pre)


def version_satisfies(version: str, requirement: str) -> bool:
    candidate = semantic_version(version)
    expression = requirement.strip()
    if expression.startswith("^"):
        lower = semantic_version(expression[1:])
        if lower.major:
            upper = SemanticVersion(lower.major + 1, 0, 0)
        elif lower.minor:
            upper = SemanticVersion(0, lower.minor + 1, 0)
        else:
            upper = SemanticVersion(0, 0, lower.patch + 1)
        return compare_versions(candidate, lower) >= 0 and compare_versions(candidate, upper) < 0
    if expression.startswith("="):
        expression = expression[1:]
    return compare_versions(candidate, semantic_version(expression)) == 0


def resolve_install_state(
    *,
    complete_receipt: bool,
    partial_receipt: bool,
    mbf_metadata: bool,
    mbf_payload_complete: bool,
    legacy_phase_copies: int,
    legacy_runtime: bool = False,
    receipt_unreadable: bool = False,
    unexpected_phase_copy: bool = False,
) -> str:
    if receipt_unreadable or ((complete_receipt or partial_receipt) and unexpected_phase_copy):
        return "MIXED_OR_AMBIGUOUS"
    if mbf_metadata and (complete_receipt or partial_receipt):
        return "MIXED_OR_AMBIGUOUS"
    if mbf_metadata:
        return "MBF_MANAGED" if mbf_payload_complete else "MBF_REGISTERED_NOT_INSTALLED"
    if partial_receipt:
        return "SOURCE_PARTIAL"
    if complete_receipt:
        return "SOURCE_MANAGED"
    if legacy_phase_copies > 1:
        return "MIXED_OR_AMBIGUOUS"
    if legacy_phase_copies == 1 or legacy_runtime:
        return "LEGACY_SOURCE"
    return "NOT_INSTALLED"


def receipt_removal_action(item: dict, current_sha256: str | None, partial: bool = False) -> str:
    """Choose an uninstall action without making hashes an uninstall gate.

    Receipts still identify which paths are private to Big Screen and which are
    shared dependencies.  Once the user confirms removal, every present
    BigScreenExclusive path is removed even if its bytes changed after the
    source deployment.  A stale hash must never make the mod impossible to
    uninstall.  Shared dependencies remain outside Big Screen's ownership.
    """
    if item.get("ownership") != "BigScreenExclusive":
        return "PreserveShared"
    if not current_sha256:
        return "AlreadyAbsent"
    return "RemoveExclusive"


def parse_adb_devices(lines: list[str]) -> list[dict[str, str]]:
    devices: list[dict[str, str]] = []
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("List of devices"):
            continue
        # Platform Tools output is whitespace-delimited, not tab-delimited.
        # Older versions commonly printed a tab after the serial, while ADB 37
        # can align the same field with spaces.  Parsing arbitrary whitespace
        # keeps both forms valid and prevents a connected Quest from being
        # silently discarded during Linux deployment.
        identity = line.split(None, 1)
        if len(identity) != 2:
            continue
        serial, details = identity
        fields = details.split()
        state = fields[0]
        metadata = {}
        for field in fields[1:]:
            if ":" in field:
                key, value = field.split(":", 1)
                metadata[key] = value.replace("_", " ")
        devices.append({
            "serial": serial,
            "state": state,
            "model": metadata.get("model", ""),
            "product": metadata.get("product", ""),
            "device": metadata.get("device", ""),
        })
    return devices


def quest_identity(manufacturer: str, model: str) -> bool:
    maker = manufacturer.strip().lower()
    product = model.strip().lower().replace("_", " ")
    return maker in {"oculus", "meta"} or "quest" in product


def select_quest(candidates: list[dict[str, str]], noninteractive: bool = False) -> dict[str, str]:
    if not candidates:
        raise ValueError("No authorized Quest with Beat Saber was found.")
    if len(candidates) == 1:
        return candidates[0]
    if noninteractive:
        raise ValueError("More than one Quest with Beat Saber is connected.")
    raise ValueError("Interactive selection must be performed by the launcher.")
