#!/usr/bin/env bash

# Installs the exact Linux NDK used by Big Screen's first-party and FFmpeg CI
# builds. The archive checksum comes from Google's official repository index.
set -euo pipefail

toolchain_root="${BIGSCREEN_TOOLCHAIN_ROOT:-${HOME}/.cache/bigscreen-toolchains}"
archive_path="${toolchain_root}/android-ndk-r27d-linux.zip"
install_path="${toolchain_root}/android-ndk-r27d"
archive_url="https://dl.google.com/android/repository/android-ndk-r27d-linux.zip"
archive_sha1="22105e410cf29afcf163760cc95522b9fb981121"

mkdir -p "${toolchain_root}"
if [[ ! -f "${archive_path}" ]]; then
    curl --fail --location --retry 3 --output "${archive_path}" "${archive_url}"
fi
printf '%s  %s\n' "${archive_sha1}" "${archive_path}" |
    sha1sum --check --strict
if [[ ! -f "${install_path}/source.properties" ]]; then
    unzip -q "${archive_path}" -d "${toolchain_root}"
fi
grep -E 'Pkg.Revision|Pkg.ReleaseName' "${install_path}/source.properties"
