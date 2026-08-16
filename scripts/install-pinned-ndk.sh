#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Installs the exact Linux NDK used by Big Screen's first-party and FFmpeg CI
# builds. The archive checksum comes from Google's official repository index.
set -euo pipefail

toolchain_root="${BIGSCREEN_TOOLCHAIN_ROOT:-${HOME}/.cache/bigscreen-toolchains}"
archive_path="${toolchain_root}/android-ndk-r27d-linux.zip"
install_path="${toolchain_root}/android-ndk-r27d"
archive_url="https://dl.google.com/android/repository/android-ndk-r27d-linux.zip"
archive_sha256="601246087a682d1944e1e16dd85bc6e49560fe8b6d61255be2829178c8ed15d9"

mkdir -p "${toolchain_root}"
if [[ ! -f "${archive_path}" ]]; then
    printf 'Downloading Android NDK r27d for Linux/WSL from %s\n' "${archive_url}"
    printf 'The archive will be checked against the pinned Google repository checksum.\n'
    curl --fail --location --retry 3 --output "${archive_path}" "${archive_url}"
else
    printf 'Using cached Android NDK r27d Linux archive.\n'
fi
printf '%s  %s\n' "${archive_sha256}" "${archive_path}" |
    sha256sum --check --strict
if [[ ! -f "${install_path}/source.properties" ]]; then
    unzip -q "${archive_path}" -d "${toolchain_root}"
fi
grep -E 'Pkg.Revision|Pkg.ReleaseName' "${install_path}/source.properties"
