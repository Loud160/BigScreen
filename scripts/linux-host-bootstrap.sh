#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Select a predictable Linux build environment before any Big Screen build
# work starts. Conventional Debian-family systems build natively. Immutable
# hosts (including Bazzite) and unsupported distributions are normalized into
# a reusable Ubuntu 24.04 Distrobox backed by Podman. The public launchers call
# this file before bootstrap-linux.sh so missing host/container prerequisites
# are disclosed and approved instead of failing halfway through a build.

BIGSCREEN_DISTROBOX_NAME="${BIGSCREEN_DISTROBOX_NAME:-bigscreen-build}"
BIGSCREEN_DISTROBOX_IMAGE="${BIGSCREEN_DISTROBOX_IMAGE:-docker.io/library/ubuntu:24.04}"
BIGSCREEN_OS_RELEASE_FILE="${BIGSCREEN_OS_RELEASE_FILE:-/etc/os-release}"
BIGSCREEN_OSTREE_MARKER="${BIGSCREEN_OSTREE_MARKER:-/run/ostree-booted}"

BIGSCREEN_UBUNTU_BUILD_PACKAGES=(
    build-essential ca-certificates cmake curl ffmpeg
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
    ninja-build pkg-config python3 unzip xz-utils
)

bigscreen_host_prompt_yes_no() {
    local prompt="$1" answer
    if [[ ! -t 0 ]]; then
        printf '%s\n' "Interactive approval is required. Rerun in a terminal or pass --yes." >&2
        return 1
    fi
    read -r -p "${prompt}" answer
    [[ "${answer}" =~ ^[Yy]([Ee][Ss])?$ ]]
}

bigscreen_linux_os_value() {
    local key="$1"
    if [[ ! -r "${BIGSCREEN_OS_RELEASE_FILE}" ]]; then
        return 0
    fi
    (
        # /etc/os-release is the standard shell-compatible source of these
        # identifiers. Run it in a subshell so it cannot alter caller state.
        set +u
        # shellcheck disable=SC1090
        source "${BIGSCREEN_OS_RELEASE_FILE}"
        printf '%s\n' "${!key:-}"
    )
}

bigscreen_native_build_commands_ready() {
    local command_name
    for command_name in bash cmake curl make ninja nproc pkg-config python3 \
        sed sha256sum tar unzip xz; do
        command -v "${command_name}" >/dev/null 2>&1 || return 1
    done
    pkg-config --exists libavformat libavcodec libavutil libswscale \
        >/dev/null 2>&1
}

bigscreen_debian_missing_build_packages() {
    local package_name status
    for package_name in "${BIGSCREEN_UBUNTU_BUILD_PACKAGES[@]}"; do
        status="$(dpkg-query -W -f='${Status}' "${package_name}" 2>/dev/null || true)"
        [[ "${status}" == "install ok installed" ]] ||
            printf '%s\n' "${package_name}"
    done
}

bigscreen_linux_host_strategy() {
    if [[ "${BIGSCREEN_MANAGED_DISTROBOX:-0}" == "1" ]]; then
        printf 'native\n'
        return 0
    fi

    local id id_like
    id="$(bigscreen_linux_os_value ID)"
    id_like="$(bigscreen_linux_os_value ID_LIKE)"
    id="${id,,}"
    id_like="${id_like,,}"

    if [[ -e "${BIGSCREEN_OSTREE_MARKER}" ]] ||
       [[ "${id}" =~ ^(bazzite|silverblue|kinoite|sericea|onyx)$ ]]; then
        printf 'distrobox\n'
        return 0
    fi

    if [[ "${id}" =~ ^(ubuntu|debian|linuxmint)$ ]] ||
       [[ " ${id_like} " == *" debian "* ]] ||
       [[ " ${id_like} " == *" ubuntu "* ]]; then
        printf 'native\n'
        return 0
    fi

    # A non-Debian distribution with a complete compatible toolchain may
    # build natively. Otherwise use Ubuntu rather than maintaining several
    # subtly different package recipes and output paths.
    if bigscreen_native_build_commands_ready; then
        printf 'native\n'
    else
        printf 'distrobox\n'
    fi
}

bigscreen_sudo_prefix() {
    if (( EUID == 0 )); then
        return 0
    fi
    if command -v sudo >/dev/null 2>&1; then
        printf 'sudo\n'
        return 0
    fi
    printf 'Installing host packages requires root access, but sudo is not available.\n' >&2
    return 1
}

bigscreen_install_container_tools() {
    local -a packages=("$@") command_prefix=()
    local sudo_command
    sudo_command="$(bigscreen_sudo_prefix)" || return 1
    [[ -n "${sudo_command}" ]] && command_prefix=("${sudo_command}")

    if command -v apt-get >/dev/null 2>&1; then
        "${command_prefix[@]}" apt-get update &&
            "${command_prefix[@]}" apt-get install -y "${packages[@]}"
    elif command -v dnf >/dev/null 2>&1; then
        "${command_prefix[@]}" dnf install -y "${packages[@]}"
    elif command -v pacman >/dev/null 2>&1; then
        "${command_prefix[@]}" pacman -Sy --needed --noconfirm "${packages[@]}"
    elif command -v zypper >/dev/null 2>&1; then
        "${command_prefix[@]}" zypper --non-interactive install "${packages[@]}"
    elif command -v rpm-ostree >/dev/null 2>&1; then
        "${command_prefix[@]}" rpm-ostree install "${packages[@]}" || return 1
        cat >&2 <<'EOF'
Distrobox/Podman were added to the immutable host deployment. A reboot is
required before those commands become available. Reboot, then run this same
one-click launcher again; it will continue with container creation.
EOF
        return 2
    else
        printf 'No supported host package manager was found for: %s\n' \
            "${packages[*]}" >&2
        printf 'Install Distrobox and Podman, then rerun this launcher.\n' >&2
        return 1
    fi
}

bigscreen_distrobox_exists() {
    distrobox list --no-color 2>/dev/null |
        awk -v expected="${BIGSCREEN_DISTROBOX_NAME}" '
            NR > 1 {
                for (field = 1; field <= NF; field++) {
                    if ($field == expected) found = 1
                }
            }
            END { exit(found ? 0 : 1) }
        '
}

bigscreen_distrobox_build_packages_ready() {
    distrobox enter --name "${BIGSCREEN_DISTROBOX_NAME}" -- \
        bash -lc '
            set -e
            for command_name in bash cmake curl make ninja nproc pkg-config python3 sed sha256sum tar unzip xz; do
                command -v "$command_name" >/dev/null 2>&1 || exit 1
            done
            pkg-config --exists libavformat libavcodec libavutil libswscale
        ' >/dev/null 2>&1
}

bigscreen_distrobox_missing_build_packages() {
    distrobox enter --name "${BIGSCREEN_DISTROBOX_NAME}" -- \
        bash -lc '
            for package_name in "$@"; do
                status="$(dpkg-query -W -f="\${Status}" "$package_name" 2>/dev/null || true)"
                [[ "$status" == "install ok installed" ]] || printf "%s\n" "$package_name"
            done
        ' bigscreen-package-audit "${BIGSCREEN_UBUNTU_BUILD_PACKAGES[@]}"
}

bigscreen_prepare_native_build_packages() {
    local assume_yes="$1"
    if bigscreen_native_build_commands_ready; then
        return 0
    fi

    if ! command -v apt-get >/dev/null 2>&1 ||
       ! command -v dpkg-query >/dev/null 2>&1; then
        printf 'The native Linux toolchain is incomplete and cannot be installed with apt.\n' >&2
        printf 'Run this launcher on a supported Debian-family host or install Distrobox and Podman.\n' >&2
        return 1
    fi

    local -a missing_packages=()
    mapfile -t missing_packages < <(bigscreen_debian_missing_build_packages)
    if (( ${#missing_packages[@]} == 0 )); then
        printf 'Required Debian packages are installed, but one or more build commands or FFmpeg pkg-config modules are unavailable.\n' >&2
        return 1
    fi

    printf '\nMissing native Linux build packages:\n  %s\n' \
        "${missing_packages[*]}"
    printf 'Installing these packages uses apt and may request sudo/root authentication.\n'
    if [[ "${assume_yes}" != "1" ]] &&
       ! bigscreen_host_prompt_yes_no \
            'Install the missing native build packages now? [Y/N] '; then
        printf 'Cancelled. No native build packages were installed.\n' >&2
        return 1
    fi
    bigscreen_install_container_tools "${missing_packages[@]}" || return 1
    bigscreen_native_build_commands_ready || {
        printf 'The native Linux build prerequisites are still incomplete after package installation.\n' >&2
        return 1
    }
}

bigscreen_prepare_distrobox() {
    local assume_yes="$1"
    local -a missing_tools=()
    command -v distrobox >/dev/null 2>&1 || missing_tools+=(distrobox)
    command -v podman >/dev/null 2>&1 || missing_tools+=(podman)

    if (( ${#missing_tools[@]} > 0 )); then
        printf '\nThis Linux host needs an Ubuntu build container.\n'
        printf 'Missing host tools: %s\n' "${missing_tools[*]}"
        printf 'The installer will use the host package manager and may request sudo/root authentication.\n'
        if [[ "${assume_yes}" != "1" ]] &&
           ! bigscreen_host_prompt_yes_no \
                'Install the missing Distrobox/Podman tools now? [Y/N] '; then
            printf 'Cancelled. No container tools were installed.\n' >&2
            return 1
        fi
        bigscreen_install_container_tools "${missing_tools[@]}" || return 1
    fi

    command -v distrobox >/dev/null 2>&1 || {
        printf 'Distrobox is still unavailable after host setup.\n' >&2
        return 1
    }
    command -v podman >/dev/null 2>&1 || {
        printf 'Podman is still unavailable after host setup.\n' >&2
        return 1
    }

    if ! bigscreen_distrobox_exists; then
        printf '\nBig Screen will create this reusable build container:\n'
        printf '  Name:  %s\n' "${BIGSCREEN_DISTROBOX_NAME}"
        printf '  Image: %s\n' "${BIGSCREEN_DISTROBOX_IMAGE}"
        printf 'The image is downloaded once and retained for later builds.\n'
        if [[ "${assume_yes}" != "1" ]] &&
           ! bigscreen_host_prompt_yes_no \
                'Create the Big Screen Ubuntu build container now? [Y/N] '; then
            printf 'Cancelled. No build container was created.\n' >&2
            return 1
        fi
        distrobox create --yes --name "${BIGSCREEN_DISTROBOX_NAME}" \
            --image "${BIGSCREEN_DISTROBOX_IMAGE}" || return 1
    else
        printf 'Using existing Distrobox container: %s\n' \
            "${BIGSCREEN_DISTROBOX_NAME}"
    fi

    # The first enter finishes Distrobox initialization and reports that work
    # live instead of hiding it inside the following package-audit capture.
    distrobox enter --name "${BIGSCREEN_DISTROBOX_NAME}" -- true || return 1

    local container_identity
    container_identity="$(distrobox enter --name "${BIGSCREEN_DISTROBOX_NAME}" -- \
        bash -lc '. /etc/os-release; printf "%s:%s" "${ID:-}" "${VERSION_ID:-}"' \
        2>/dev/null)"
    if [[ "${container_identity}" != "ubuntu:24.04" ]]; then
        printf 'Existing container %s is %s, not the required Ubuntu 24.04 environment.\n' \
            "${BIGSCREEN_DISTROBOX_NAME}" "${container_identity:-unknown}" >&2
        printf 'Big Screen will not replace an existing container automatically. Rename/remove it or set BIGSCREEN_DISTROBOX_NAME to another name.\n' >&2
        return 1
    fi

    if ! bigscreen_distrobox_build_packages_ready; then
        local -a missing_packages=()
        mapfile -t missing_packages < <(bigscreen_distrobox_missing_build_packages)
        if (( ${#missing_packages[@]} == 0 )); then
            printf 'The Ubuntu container packages are installed, but required build commands or FFmpeg pkg-config modules are unavailable.\n' >&2
            return 1
        fi
        printf '\nThe Ubuntu build container is missing Big Screen build packages.\n'
        printf 'Packages to install inside %s\n  %s\n' \
            "${BIGSCREEN_DISTROBOX_NAME}" \
            "${missing_packages[*]}"
        printf 'These packages are installed only inside the reusable container.\n'
        if [[ "${assume_yes}" != "1" ]] &&
           ! bigscreen_host_prompt_yes_no \
                'Install the missing container build packages now? [Y/N] '; then
            printf 'Cancelled. Container packages were not installed.\n' >&2
            return 1
        fi
        distrobox enter --name "${BIGSCREEN_DISTROBOX_NAME}" -- \
            bash -lc "sudo apt-get update && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y ${missing_packages[*]}" ||
            return 1
        bigscreen_distrobox_build_packages_ready || {
            printf 'The Ubuntu container prerequisites are still incomplete after package installation.\n' >&2
            return 1
        }
    else
        printf 'The reusable Ubuntu container already has all build packages.\n'
    fi
}

bigscreen_prepare_or_reexec_linux_host() {
    local launcher_path="$1" assume_yes="$2"
    shift 2

    local architecture strategy repository_root
    architecture="$(uname -m)"
    case "${architecture}" in
        x86_64|amd64) ;;
        *)
            printf 'Big Screen requires an x86-64 Linux build host; found %s.\n' \
                "${architecture}" >&2
            return 1
            ;;
    esac

    strategy="$(bigscreen_linux_host_strategy)"
    if [[ "${strategy}" == "native" ]]; then
        bigscreen_prepare_native_build_packages "${assume_yes}"
        return $?
    fi

    printf '\nThis host uses an immutable or unsupported Linux base.\n'
    printf 'Big Screen will use a reusable Ubuntu 24.04 Distrobox so the host OS remains clean.\n'
    bigscreen_prepare_distrobox "${assume_yes}" || return 1

    repository_root="$(cd "$(dirname "${launcher_path}")" && pwd)"
    printf '\nContinuing the same operation inside %s.\n' \
        "${BIGSCREEN_DISTROBOX_NAME}"
    exec distrobox enter --name "${BIGSCREEN_DISTROBOX_NAME}" -- \
        env BIGSCREEN_MANAGED_DISTROBOX=1 \
        bash "${repository_root}/$(basename "${launcher_path}")" "$@"
}
