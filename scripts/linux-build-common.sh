#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Shared, pinned paths for Big Screen's native x86-64 Linux build entrypoints.
# Keep these values aligned with .github/workflows/build-ndk.yml and
# scripts/install-pinned-ndk.sh. Centralizing them prevents the bootstrap and
# build wrappers from silently selecting different QPM or NDK installations.

BIGSCREEN_LINUX_QPM_VERSION="1.5.11"
BIGSCREEN_LINUX_QPM_ARCHIVE_SHA256="4d1f15245b18066ba0ef7f17224521754563323c1855a5cc730d49ae6a4419df"
BIGSCREEN_LINUX_NDK_VERSION="27.3.13750724"

bigscreen_linux_script_dir() {
    cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

bigscreen_repository_root() {
    local script_dir
    script_dir="$(bigscreen_linux_script_dir)"
    cd "${script_dir}/.." && pwd
}

bigscreen_toolchain_root() {
    printf '%s\n' "${BIGSCREEN_TOOLCHAIN_ROOT:-${HOME}/.cache/bigscreen-toolchains}"
}

bigscreen_qpm_root() {
    printf '%s/qpm-%s\n' "$(bigscreen_toolchain_root)" \
        "${BIGSCREEN_LINUX_QPM_VERSION}"
}

bigscreen_qpm_executable() {
    printf '%s/qpm\n' "$(bigscreen_qpm_root)"
}

bigscreen_ndk_root() {
    printf '%s/android-ndk-r27d\n' "$(bigscreen_toolchain_root)"
}

bigscreen_require_x64_linux() {
    local kernel architecture
    kernel="$(uname -s)"
    architecture="$(uname -m)"
    if [[ "${kernel}" != "Linux" ]]; then
        printf 'Big Screen\047s Linux build requires a Linux host; found %s.\n' \
            "${kernel}" >&2
        return 1
    fi
    case "${architecture}" in
        x86_64|amd64)
            ;;
        *)
            printf 'Big Screen\047s native Linux build requires x86-64; found %s.\n' \
                "${architecture}" >&2
            printf 'Android NDK r27d and QPM 1.5.11 do not provide supported Linux ARM64 host binaries.\n' >&2
            return 1
            ;;
    esac
}

bigscreen_print_missing_packages() {
    cat >&2 <<'EOF'

On Ubuntu, Debian, or Linux Mint, install the native build prerequisites with:

  sudo apt-get update
  sudo apt-get install -y build-essential ca-certificates cmake curl ffmpeg \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev ninja-build \
    pkg-config python3 unzip xz-utils
EOF
}
