#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Shared Linux ADB discovery, verified portable installation, and daemon
# ownership handling. The Quest-selection and source-ownership policy remains
# in the PowerShell deployment/removal scripts; this file only makes the same
# ADB transport available to native Linux launchers.

BIGSCREEN_LINUX_ADB_VERSION="37.0.0"
BIGSCREEN_LINUX_ADB_URL="https://dl.google.com/android/repository/platform-tools_r37.0.0-linux.zip"
BIGSCREEN_LINUX_ADB_SHA256="198ae156ab285fa555987219af237b31102fefe8b9d2bc274708a8d4f2865a07"
BIGSCREEN_LINUX_ADB_DOWNLOAD_MB="8.7"

bigscreen_adb_script_dir() {
    cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

bigscreen_adb_repository_root() {
    local script_dir
    script_dir="$(bigscreen_adb_script_dir)"
    cd "${script_dir}/.." && pwd
}

bigscreen_adb_install_root() {
    printf '%s\n' "${BIGSCREEN_ADB_INSTALL_ROOT:-$(bigscreen_adb_repository_root)/BigScreen Tools}"
}

bigscreen_adb_portable_path() {
    printf '%s/platform-tools/adb\n' "$(bigscreen_adb_install_root)"
}

bigscreen_adb_version_matches() {
    local adb_path="$1"
    local properties
    properties="$(dirname "${adb_path}")/source.properties"
    [[ -x "${adb_path}" && -f "${properties}" ]] || return 1
    grep -Eq "^Pkg\\.Revision=${BIGSCREEN_LINUX_ADB_VERSION}[[:space:]]*$" \
        "${properties}"
}

bigscreen_find_adb() {
    local candidate sdk_root
    if command -v adb >/dev/null 2>&1; then
        BIGSCREEN_ADB_EXECUTABLE="$(command -v adb)"
        export BIGSCREEN_ADB_EXECUTABLE
        return 0
    fi

    candidate="$(bigscreen_adb_portable_path)"
    if bigscreen_adb_version_matches "${candidate}"; then
        PATH="$(dirname "${candidate}"):${PATH}"
        BIGSCREEN_ADB_EXECUTABLE="${candidate}"
        export PATH BIGSCREEN_ADB_EXECUTABLE
        return 0
    fi

    for sdk_root in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}"; do
        [[ -n "${sdk_root}" ]] || continue
        candidate="${sdk_root}/platform-tools/adb"
        if [[ -x "${candidate}" ]]; then
            PATH="$(dirname "${candidate}"):${PATH}"
            BIGSCREEN_ADB_EXECUTABLE="${candidate}"
            export PATH BIGSCREEN_ADB_EXECUTABLE
            return 0
        fi
    done
    return 1
}

bigscreen_prompt_yes_no() {
    local prompt="$1"
    local timeout_seconds="${2:-0}"
    local answer=""
    if [[ ! -t 0 ]]; then
        return 1
    fi
    if (( timeout_seconds > 0 )); then
        if ! read -r -t "${timeout_seconds}" -p "${prompt}" answer; then
            printf '\n'
            return 1
        fi
    else
        read -r -p "${prompt}" answer
    fi
    [[ "${answer}" =~ ^[Yy]([Ee][Ss])?$ ]]
}

bigscreen_install_portable_adb() {
    local install_root archive_dir archive_path stage_root staged_directory
    local final_directory actual_hash resolved_root resolved_final
    for command_name in curl sha256sum unzip; do
        if ! command -v "${command_name}" >/dev/null 2>&1; then
            printf 'Portable ADB setup requires %s. Install it and rerun this launcher.\n' \
                "${command_name}" >&2
            return 1
        fi
    done

    install_root="$(bigscreen_adb_install_root)"
    archive_dir="${install_root}/downloads"
    archive_path="${archive_dir}/platform-tools_r${BIGSCREEN_LINUX_ADB_VERSION}-linux.zip"
    final_directory="${install_root}/platform-tools"
    mkdir -p "${archive_dir}"

    if [[ -f "${archive_path}" ]]; then
        actual_hash="$(sha256sum "${archive_path}" | awk '{ print $1 }')"
        if [[ "${actual_hash}" != "${BIGSCREEN_LINUX_ADB_SHA256}" ]]; then
            printf 'The cached Platform Tools archive failed SHA-256 verification and will be replaced.\n' >&2
            rm -f -- "${archive_path}"
        else
            printf 'Using the verified cached Linux Platform Tools archive.\n'
        fi
    fi

    if [[ ! -f "${archive_path}" ]]; then
        printf 'Downloading Google Android SDK Platform Tools %s for Linux (%s MB).\n' \
            "${BIGSCREEN_LINUX_ADB_VERSION}" "${BIGSCREEN_LINUX_ADB_DOWNLOAD_MB}"
        printf 'Source: %s\n' "${BIGSCREEN_LINUX_ADB_URL}"
        printf 'Progress is shown below; a slow connection has not frozen the launcher.\n'
        if ! curl --fail --location --progress-bar \
            --output "${archive_path}.partial" "${BIGSCREEN_LINUX_ADB_URL}"; then
            rm -f -- "${archive_path}.partial"
            return 1
        fi
        mv -f -- "${archive_path}.partial" "${archive_path}" || return 1
    fi

    printf 'Verifying the official Linux Platform Tools archive SHA-256.\n'
    actual_hash="$(sha256sum "${archive_path}" | awk '{ print $1 }')"
    if [[ "${actual_hash}" != "${BIGSCREEN_LINUX_ADB_SHA256}" ]]; then
        rm -f -- "${archive_path}"
        printf 'The downloaded Platform Tools SHA-256 did not match the pinned official archive.\n' >&2
        return 1
    fi

    stage_root="$(mktemp -d "${install_root}/installing-platform-tools.XXXXXX")" || return 1
    printf 'Extracting the verified portable ADB tools.\n'
    if ! unzip -q "${archive_path}" -d "${stage_root}"; then
        rm -rf -- "${stage_root}"
        return 1
    fi
    staged_directory="${stage_root}/platform-tools"
    if ! chmod +x "${staged_directory}/adb"; then
        rm -rf -- "${stage_root}"
        return 1
    fi
    if ! bigscreen_adb_version_matches "${staged_directory}/adb"; then
        printf 'The verified archive did not contain the expected ADB %s layout.\n' \
            "${BIGSCREEN_LINUX_ADB_VERSION}" >&2
        rm -rf -- "${stage_root}"
        return 1
    fi

    resolved_root="$(realpath -m "${install_root}")"
    resolved_final="$(realpath -m "${final_directory}")"
    case "${resolved_final}" in
        "${resolved_root}"/*) ;;
        *)
            printf 'Refusing to replace a Platform Tools directory outside BigScreen Tools.\n' >&2
            rm -rf -- "${stage_root}"
            return 1
            ;;
    esac
    if [[ -e "${final_directory}" ]]; then
        rm -rf -- "${final_directory}"
    fi
    if ! mv -- "${staged_directory}" "${final_directory}"; then
        rm -rf -- "${stage_root}"
        return 1
    fi
    rmdir "${stage_root}" 2>/dev/null || true

    printf 'Portable ADB installed successfully: %s\n' \
        "${final_directory}/adb"
}

bigscreen_ensure_adb() {
    local action="${BIGSCREEN_ADB_MISSING_ACTION:-Ask}"
    if bigscreen_find_adb; then
        printf 'Using ADB: %s\n' "${BIGSCREEN_ADB_EXECUTABLE}"
        return 0
    fi

    printf '\nADB was not found.\n' >&2
    printf 'Big Screen can download Google Android SDK Platform Tools %s for Linux.\n' \
        "${BIGSCREEN_LINUX_ADB_VERSION}" >&2
    printf 'Download: approximately %s MB\n' "${BIGSCREEN_LINUX_ADB_DOWNLOAD_MB}" >&2
    printf 'Destination: %s\n' "$(bigscreen_adb_install_root)" >&2
    printf 'This is portable, needs no administrator access, and does not modify the system PATH.\n' >&2
    printf 'Google Android SDK terms: https://developer.android.com/studio/terms\n' >&2

    case "${action}" in
        Install)
            ;;
        Decline)
            printf 'ADB is required. Nothing was downloaded.\n' >&2
            return 1
            ;;
        Ask)
            printf 'If no choice is made within five minutes, nothing will be downloaded.\n' >&2
            if ! bigscreen_prompt_yes_no \
                'Download and use the portable ADB tools? [Y/N] ' 300; then
                printf 'ADB is required. Nothing was downloaded.\n' >&2
                return 1
            fi
            ;;
        *)
            printf 'Invalid BIGSCREEN_ADB_MISSING_ACTION: %s\n' "${action}" >&2
            return 2
            ;;
    esac

    bigscreen_install_portable_adb
    if ! bigscreen_find_adb; then
        printf 'Portable ADB was installed but could not be selected.\n' >&2
        return 1
    fi
    printf 'Using ADB: %s\n' "${BIGSCREEN_ADB_EXECUTABLE}"
}

bigscreen_adb_is_running() {
    pgrep -x adb >/dev/null 2>&1
}

bigscreen_stop_adb() {
    if command -v adb >/dev/null 2>&1; then
        adb kill-server >/dev/null 2>&1 || true
    fi
    sleep 0.3
    if bigscreen_adb_is_running; then
        pkill -x adb >/dev/null 2>&1 || true
    fi
}

bigscreen_complete_adb_session() {
    local was_running_at_start="$1"
    local action="${BIGSCREEN_EXISTING_ADB_ACTION:-Ask}"
    bigscreen_adb_is_running || return 0

    if [[ "${was_running_at_start}" == "0" ]]; then
        printf 'Stopping the ADB server started by this workflow...\n'
        bigscreen_stop_adb
        printf 'ADB was stopped.\n'
        return 0
    fi

    case "${action}" in
        Stop)
            bigscreen_stop_adb
            printf 'ADB was stopped.\n'
            ;;
        Leave)
            printf 'ADB was left running.\n'
            ;;
        Ask)
            printf '\nADB was already running before this workflow.\n'
            printf 'Stopping it can help ModsBeforeFriday connect. No response within five minutes defaults to No.\n'
            if bigscreen_prompt_yes_no 'Stop ADB now? [Y/N] ' 300; then
                bigscreen_stop_adb
                printf 'ADB was stopped.\n'
            else
                printf 'ADB was left running.\n'
            fi
            ;;
        *)
            printf 'Invalid BIGSCREEN_EXISTING_ADB_ACTION: %s; ADB was left running.\n' \
                "${action}" >&2
            return 2
            ;;
    esac
}
