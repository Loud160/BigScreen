#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Build one of the two private FFmpeg runtimes used by Big Screen.
#
# Big Screen deliberately builds FFmpeg itself instead of linking against the
# GPL-configured FFmpeg libraries supplied by Hollywood.  The resulting four
# shared libraries contain only the media features Big Screen calls and use a
# version-specific SONAME plus a private ELF symbol-version namespace. Both
# isolation matter: a unique filename prevents Android from treating the two
# FFmpeg builds as the same library, while unique symbol versions prevent the
# dynamic linker from satisfying Big Screen's FFmpeg calls with Hollywood's
# already-loaded, unversioned symbols.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${script_dir}/.." && pwd)"

ffmpeg_version="${BIGSCREEN_FFMPEG_VERSION:-4.4.8}"
case "${ffmpeg_version}" in
    4.4.8)
        ffmpeg_sha256="c73848c4ae283d9eaee7be3b276affbc3543380483555500d0dd2c9b7e1c39c3"
        postproc_option="--disable-postproc"
        runtime_tag="44"
        ;;
    9.0.1)
        ffmpeg_sha256="cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635"
        # libpostproc and its configure switch were removed after FFmpeg 7.
        # The 9.x runtime is still decoder-only because --disable-everything
        # starts from no components and the allowlist below is explicit.
        postproc_option=""
        runtime_tag="9"
        ;;
    *)
        printf 'Unsupported FFmpeg comparison version: %s\n' "${ffmpeg_version}" >&2
        exit 1
        ;;
esac
build_suffix="-bigscreen${runtime_tag}"
symbol_namespace="BIGSCREEN${runtime_tag}"
ffmpeg_archive="ffmpeg-${ffmpeg_version}.tar.xz"
ffmpeg_url="https://ffmpeg.org/releases/${ffmpeg_archive}"
android_api="24"
# Increment the affected runtime only when its recipe changes. Revision 2
# removed host paths from both builds; FFmpeg 9 revision 3 adds the two explicit
# H.264 transcoders while revision 4 records their complete license/source
# metadata. The comparison FFmpeg 4 runtime remains unchanged.
if [[ "${runtime_tag}" == "9" ]]; then
    build_recipe_revision="4"
else
    build_recipe_revision="2"
fi

# FFmpeg contains no native software H.264 encoder. The FFmpeg 9 runtime uses
# Android MediaCodec first and carries one pinned x264 fallback for the rare
# device/format combination where the hardware encoder cannot start. x264 is
# GPL-2.0-or-later; Big Screen is GPL-3.0-only, so this is a compatible but
# deliberate change from the still-LGPL comparison FFmpeg 4.4 runtime.
x264_commit="b35605ace3ddf7c1a5d67a2eb553f034aef41d55"
x264_sha256="cd71a7515b0e9a012e1ac9b1f8415bebcaf6fc97d4db32286642ac4c0fbe24f9"
x264_archive="x264-${x264_commit}.tar.gz"
x264_url="https://github.com/mirror/x264/archive/${x264_commit}.tar.gz"

# Keep compilation in the native Linux filesystem.  Building thousands of
# small FFmpeg objects through WSL's /mnt/c bridge is dramatically slower and
# can leave partially copied trees after an interrupted Windows session.
cache_root="${BIGSCREEN_FFMPEG_CACHE:-${HOME}/.cache/bigscreen-ffmpeg}"
x264_archive_path="${cache_root}/${x264_archive}"
x264_download_path="${x264_archive_path}.download.$$"
source_root="${cache_root}/ffmpeg-${ffmpeg_version}"
pristine_root="${cache_root}/ffmpeg-${ffmpeg_version}-pristine"
build_root="${cache_root}/build-${ffmpeg_version}-android-arm64"
# FFmpeg writes --prefix into generated shell fragments without consistently
# quoting it. A Windows checkout such as "BigScreen-main (1)" therefore makes
# FFmpeg 4's pkg-config generator parse the parentheses as shell syntax. Keep
# the configure/install prefix entirely in WSL's native, path-safe cache and
# copy the completed installation into the repository only after `make
# install` succeeds.
# Configure uses this stable path relative to build_root. FFmpeg exposes its
# configure command through avutil_configuration(), so passing an absolute
# prefix here would make otherwise identical builds differ by developer home.
portable_install_prefix=".bigscreen-install"
native_install_root="${build_root}/${portable_install_prefix}"
portable_toolchain_root=".bigscreen-toolchain"
portable_dependency_root="${repository_root}/.cache/dependencies"
if [[ "${ffmpeg_version}" == "4.4.8" ]]; then
    install_root="${portable_dependency_root}/ffmpeg-lgpl"
else
    install_root="${portable_dependency_root}/ffmpeg-lgpl-${ffmpeg_version}"
fi
archive_path="${cache_root}/${ffmpeg_archive}"
archive_download_path="${archive_path}.download.$$"
stamp_path="${install_root}/bigscreen-ffmpeg-${ffmpeg_version}.ready"
config_record_path="${install_root}/bigscreen-ffmpeg-config.mak"

# Interrupted downloads must never become the persistent cache entry. The
# process-specific temporary is removed on every exit, including Ctrl+C.
trap 'rm -f "${archive_download_path}" "${x264_download_path}"' EXIT INT TERM

# The local Windows development setup keeps a Linux NDK in WSL.  CI and other
# developers can set ANDROID_NDK_ROOT explicitly to use any equivalent Linux
# NDK installation.  A Linux-hosted toolchain is required because FFmpeg's
# configure/make build runs inside Linux or WSL.
default_ndk_root="${HOME}/.cache/bigscreen-toolchains/android-ndk-r27d"
android_ndk_root="${ANDROID_NDK_ROOT:-${default_ndk_root}}"
toolchain_bin="${android_ndk_root}/toolchains/llvm/prebuilt/linux-x86_64/bin"

required_outputs=(
    "libavutil${build_suffix}.so"
    "libavcodec${build_suffix}.so"
    "libavformat${build_suffix}.so"
    "libswscale${build_suffix}.so"
)

is_complete_install() {
    [[ -f "${stamp_path}" ]] || return 1
    [[ -d "${install_root}/include" ]] || return 1
    [[ -f "${config_record_path}" ]] || return 1
    [[ -f "${install_root}/BUILD-INFO.txt" ]] || return 1
    grep -Fxq "Build recipe revision: ${build_recipe_revision}" \
        "${install_root}/BUILD-INFO.txt" || return 1
    grep -Eq '^CFLAGS=.*(^|[[:space:]])-w([[:space:]]|$)' \
        "${config_record_path}" || return 1
    [[ -f "${install_root}/SHA256SUMS" ]] || return 1
    local library
    for library in "${required_outputs[@]}"; do
        [[ -f "${install_root}/lib/${library}" ]] || return 1
        grep -Eq "^[0-9a-fA-F]{64}[[:space:]]+.*${library}$" \
            "${install_root}/SHA256SUMS" || return 1
    done
    local expected_hash recorded_path recorded_name actual_hash
    while read -r expected_hash recorded_path; do
        recorded_name="$(basename "${recorded_path}")"
        [[ -f "${install_root}/lib/${recorded_name}" ]] || return 1
        actual_hash="$(sha256sum "${install_root}/lib/${recorded_name}" | awk '{print $1}')"
        [[ "${actual_hash}" == "${expected_hash}" ]] || return 1
    done < "${install_root}/SHA256SUMS"
    local required_option
    for required_option in \
        CONFIG_H264_DECODER \
        CONFIG_H264_MEDIACODEC_DECODER \
        CONFIG_HEVC_MEDIACODEC_DECODER \
        CONFIG_VP8_DECODER \
        CONFIG_VP8_MEDIACODEC_DECODER \
        CONFIG_VP9_DECODER \
        CONFIG_VP9_MEDIACODEC_DECODER \
        CONFIG_MATROSKA_DEMUXER \
        CONFIG_MPEGTS_DEMUXER \
        CONFIG_MP4_MUXER; do
        grep -q "^${required_option}=yes$" "${config_record_path}" || return 1
    done
    if [[ "${runtime_tag}" == "9" ]]; then
        grep -q '^CONFIG_H264_MEDIACODEC_ENCODER=yes$' \
            "${config_record_path}" || return 1
        grep -q '^CONFIG_LIBX264_ENCODER=yes$' \
            "${config_record_path}" || return 1
        grep -q '^CONFIG_GPL=yes$' "${config_record_path}" || return 1
        [[ -f "${install_root}/COPYING.X264-GPLv2" ]] || return 1
    fi
    ! grep -q '^CONFIG_HEVC_DECODER=yes$' "${config_record_path}" || return 1
}

if is_complete_install && [[ "${1:-}" != "--force" ]]; then
    if [[ "${runtime_tag}" == "9" ]]; then
        printf 'Big Screen GPL FFmpeg %s with x264 fallback is already staged.\n' \
            "${ffmpeg_version}"
    else
        printf 'Big Screen LGPL FFmpeg %s is already staged.\n' "${ffmpeg_version}"
    fi
    exit 0
fi

if [[ ! -x "${toolchain_bin}/aarch64-linux-android${android_api}-clang" ]]; then
    printf 'Android NDK clang was not found at: %s\n' "${toolchain_bin}" >&2
    printf 'Set ANDROID_NDK_ROOT to an extracted Linux Android NDK r27d directory.\n' >&2
    exit 1
fi

mkdir -p "${cache_root}"

archive_is_valid() {
    [[ -f "${archive_path}" ]] &&
        printf '%s  %s\n' "${ffmpeg_sha256}" "${archive_path}" |
            sha256sum --check --status
}

if [[ -f "${archive_path}" ]] && ! archive_is_valid; then
    printf 'Discarding an incomplete or invalid cached FFmpeg archive: %s\n' \
        "${archive_path}" >&2
    rm -f "${archive_path}"
fi

if [[ ! -f "${archive_path}" ]]; then
    printf 'Downloading FFmpeg %s source from %s\n' "${ffmpeg_version}" "${ffmpeg_url}"
    printf 'The archive will be verified against its pinned SHA-256 before extraction.\n'
    rm -f "${archive_download_path}"
    curl --fail --location --retry 3 --output "${archive_download_path}" "${ffmpeg_url}"
    printf '%s  %s\n' "${ffmpeg_sha256}" "${archive_download_path}" |
        sha256sum --check --status || {
            printf 'Downloaded FFmpeg source failed SHA-256 verification.\n' >&2
            exit 1
        }
    mv -f "${archive_download_path}" "${archive_path}"
else
    printf 'Using cached FFmpeg %s source archive.\n' "${ffmpeg_version}"
fi

# A release URL alone is not immutable.  Refuse to build if the archive does
# not match the selected FFmpeg source that was reviewed for this runtime.
archive_is_valid || {
    printf 'FFmpeg source archive failed SHA-256 verification: %s\n' "${archive_path}" >&2
    exit 1
}

if [[ "${runtime_tag}" == "9" ]]; then
    x264_archive_is_valid() {
        [[ -f "${x264_archive_path}" ]] &&
            printf '%s  %s\n' "${x264_sha256}" "${x264_archive_path}" |
                sha256sum --check --status
    }
    if [[ -f "${x264_archive_path}" ]] && ! x264_archive_is_valid; then
        printf 'Discarding an incomplete or invalid cached x264 archive: %s\n' \
            "${x264_archive_path}" >&2
        rm -f "${x264_archive_path}"
    fi
    if [[ ! -f "${x264_archive_path}" ]]; then
        printf 'Downloading pinned x264 software H.264 fallback source.\n'
        curl --fail --location --retry 3 \
            --output "${x264_download_path}" "${x264_url}"
        printf '%s  %s\n' "${x264_sha256}" "${x264_download_path}" |
            sha256sum --check --status || {
                printf 'Downloaded x264 source failed SHA-256 verification.\n' >&2
                exit 1
            }
        mv -f "${x264_download_path}" "${x264_archive_path}"
    else
        printf 'Using cached pinned x264 source archive.\n'
    fi
    x264_archive_is_valid || {
        printf 'x264 source archive failed SHA-256 verification: %s\n' \
            "${x264_archive_path}" >&2
        exit 1
    }
fi

rm -rf "${source_root}" "${pristine_root}" "${build_root}" \
    "${native_install_root}" "${install_root}"
tar -xf "${archive_path}" -C "${cache_root}"
cp -a "${source_root}" "${pristine_root}"

# Android API 23 and newer supports ELF symbol versioning.  FFmpeg disables it
# for Android by default, so enable it and give each library a private version
# namespace.  Big Screen then records requirements such as
# BIGSCREEN_LIBAVCODEC_58 instead of accepting any unversioned avcodec symbol.
sed -i '/^[[:space:]]*android)$/,/^[[:space:]]*;;/ s/^[[:space:]]*disable symver$/        enable symver/' \
    "${source_root}/configure"
sed -i "s/^LIBAVCODEC_/${symbol_namespace}_LIBAVCODEC_/" "${source_root}/libavcodec/libavcodec.v"
sed -i "s/^LIBAVFORMAT_/${symbol_namespace}_LIBAVFORMAT_/" "${source_root}/libavformat/libavformat.v"
sed -i "s/^LIBAVUTIL_/${symbol_namespace}_LIBAVUTIL_/" "${source_root}/libavutil/libavutil.v"
sed -i "s/^LIBSWSCALE_/${symbol_namespace}_LIBSWSCALE_/" "${source_root}/libswscale/libswscale.v"

mkdir -p "${build_root}"
# Expose the real NDK through a fixed build-local name. The relative compiler,
# linker, and sysroot arguments below are deliberately part of the reproducible
# binary interface: FFmpeg embeds their literal spellings in libavutil.
ln -s "${toolchain_bin}/.." "${build_root}/${portable_toolchain_root}"
cd "${build_root}"

ffmpeg_license_options=()
ffmpeg_encoder_options=()
ffmpeg_pkg_config_path=""
if [[ "${runtime_tag}" == "9" ]]; then
    printf 'Building the pinned x264 software fallback for FFmpeg 9.\n'
    mkdir -p x264-source .bigscreen-x264-install
    tar -xf "${x264_archive_path}" -C x264-source --strip-components=1
    (
        cd x264-source
        # Keep pinned third-party warnings out of the player-facing build
        # transcript just as the FFmpeg compilation below does. Configure,
        # compiler, assembler, archive, and link errors remain fatal.
        CC="../${portable_toolchain_root}/bin/aarch64-linux-android${android_api}-clang" \
        AR="../${portable_toolchain_root}/bin/llvm-ar" \
        RANLIB="../${portable_toolchain_root}/bin/llvm-ranlib" \
        STRIP="../${portable_toolchain_root}/bin/llvm-strip" \
        ./configure \
            --host=aarch64-linux \
            --sysroot="../${portable_toolchain_root}/sysroot" \
            --prefix=".bigscreen-x264-install" \
            --enable-static \
            --disable-cli \
            --disable-opencl \
            --enable-pic \
            --bit-depth=8 \
            --chroma-format=420 \
            --extra-cflags='-O3 -fPIC -w'
        make -j"$(nproc)"
        make install
        cp -a .bigscreen-x264-install/. ../.bigscreen-x264-install/
    )
    ffmpeg_license_options=(--enable-gpl --enable-libx264)
    ffmpeg_encoder_options=(
        --enable-encoder=h264_mediacodec
        --enable-encoder=libx264)
    ffmpeg_pkg_config_path=".bigscreen-x264-install/lib/pkgconfig"
fi

# FFmpeg 4.4 remains the minimal LGPL decode/remux comparison runtime. FFmpeg 9
# additionally enables the Android H.264 encoder and pinned x264 software
# fallback used only after direct-MP4 repair and alternate/HLS recovery fail
# and the player explicitly approves a potentially long transcode. Neither
# runtime enables version3/nonfree features or unrelated encoders/filters.
#
# Current yt-dlp clients can select fragmented HLS/MPEG-TS media when a direct
# MP4 is unavailable. Keep the MPEG-TS demuxer and MP4 muxer in both private
# FFmpeg builds so Big Screen can normalize that payload without re-encoding.
PKG_CONFIG_PATH="${ffmpeg_pkg_config_path}" "${source_root}/configure" \
    --prefix="${portable_install_prefix}" \
    --target-os=android \
    --arch=aarch64 \
    --cpu=armv8-a \
    --build-suffix="${build_suffix}" \
    --enable-cross-compile \
    --sysroot="${portable_toolchain_root}/sysroot" \
    --cc="${portable_toolchain_root}/bin/aarch64-linux-android${android_api}-clang" \
    --cxx="${portable_toolchain_root}/bin/aarch64-linux-android${android_api}-clang++" \
    --ar="${portable_toolchain_root}/bin/llvm-ar" \
    --nm="${portable_toolchain_root}/bin/llvm-nm" \
    --ranlib="${portable_toolchain_root}/bin/llvm-ranlib" \
    --strip="${portable_toolchain_root}/bin/llvm-strip" \
    --enable-pic \
    --enable-shared \
    --disable-static \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-network \
    --disable-autodetect \
    --disable-avdevice \
    --disable-avfilter \
    --disable-swresample \
    ${postproc_option} \
    --disable-everything \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swscale \
    --enable-jni \
    --enable-mediacodec \
    "${ffmpeg_license_options[@]}" \
    "${ffmpeg_encoder_options[@]}" \
    --enable-decoder=h264 \
    --enable-decoder=h264_mediacodec \
    --enable-decoder=hevc_mediacodec \
    --enable-decoder=vp8 \
    --enable-decoder=vp8_mediacodec \
    --enable-decoder=vp9 \
    --enable-decoder=vp9_mediacodec \
    --enable-demuxer=mov \
    --enable-demuxer=matroska \
    --enable-demuxer=mpegts \
    --enable-muxer=mp4 \
    --enable-parser=h264 \
    --enable-parser=hevc \
    --enable-parser=vp8 \
    --enable-parser=vp9 \
    --enable-protocol=file \
    --extra-cflags='-O3 -fPIC -w' \
    --extra-ldflags='-Wl,-Bsymbolic'

# -Bsymbolic binds calls inside each private FFmpeg DSO to that DSO's own
# versioned symbols. This is the second isolation layer after unique SONAME and
# symbol namespaces; it prevents an already-loaded Hollywood FFmpeg runtime
# from interposing on Big Screen's internal cross-library references.

# Treat the license boundary as a machine-checked build invariant. FFmpeg's
# generated makefile records disabled configure features with negated entries;
# any positive GPL/version3/nonfree entry means a future edit changed the
# license and must stop before producing redistributable binaries.
for forbidden_option in CONFIG_VERSION3 CONFIG_NONFREE; do
    if grep -q "^${forbidden_option}=yes$" "${build_root}/ffbuild/config.mak"; then
        printf 'Forbidden FFmpeg license option was enabled: %s\n' "${forbidden_option}" >&2
        exit 1
    fi
done
if [[ "${runtime_tag}" != "9" ]] &&
   grep -q '^CONFIG_GPL=yes$' "${build_root}/ffbuild/config.mak"; then
    printf 'The comparison FFmpeg 4.4 runtime must remain LGPL-only.\n' >&2
    exit 1
fi

# A successful configure can silently disable a requested component when a
# future FFmpeg dependency changes. Refuse to publish a runtime whose build
# record cannot actually provide both the experimental hardware decoder and
# the software fallback promised by Big Screen's UI.
for required_option in \
    CONFIG_JNI \
    CONFIG_MEDIACODEC \
    CONFIG_H264_DECODER \
    CONFIG_H264_MEDIACODEC_DECODER \
    CONFIG_HEVC_MEDIACODEC_DECODER \
    CONFIG_VP8_DECODER \
    CONFIG_VP8_MEDIACODEC_DECODER \
    CONFIG_VP9_DECODER \
    CONFIG_VP9_MEDIACODEC_DECODER \
    CONFIG_MOV_DEMUXER \
    CONFIG_MATROSKA_DEMUXER \
    CONFIG_MPEGTS_DEMUXER \
    CONFIG_MP4_MUXER \
    CONFIG_H264_PARSER \
    CONFIG_HEVC_PARSER \
    CONFIG_VP8_PARSER \
    CONFIG_VP9_PARSER; do
    if ! grep -q "^${required_option}=yes$" "${build_root}/ffbuild/config.mak"; then
        printf 'Required FFmpeg decoder option was not enabled: %s\n' "${required_option}" >&2
        exit 1
    fi
done
if [[ "${runtime_tag}" == "9" ]]; then
    for required_encoder in \
        CONFIG_H264_MEDIACODEC_ENCODER \
        CONFIG_LIBX264_ENCODER; do
        if ! grep -q "^${required_encoder}=yes$" \
               "${build_root}/ffbuild/config.mak"; then
            printf 'Required FFmpeg transcoder option was not enabled: %s\n' \
                "${required_encoder}" >&2
            exit 1
        fi
    done
fi

# HEVC must remain hardware-only. This negative assertion is as important as
# the positive decoder checks above: accidentally enabling CONFIG_HEVC_DECODER
# would add a software HEVC implementation and violate Big Screen's stated
# runtime and licensing policy.
if grep -q '^CONFIG_HEVC_DECODER=yes$' "${build_root}/ffbuild/config.mak"; then
    printf 'Software HEVC decoder must not be enabled.\n' >&2
    exit 1
fi

make -j"$(nproc)"
make install

# The native install tree is complete at this point, so crossing WSL's /mnt/c
# bridge is now one bounded staging operation rather than part of FFmpeg's
# generated build rules. This also ensures an interrupted compile cannot leave
# a ready-looking partial runtime in the repository.
mkdir -p "${install_root}"
cp -a "${native_install_root}/." "${install_root}/"

# Preserve the exact build inputs needed for LGPL corresponding-source
# releases.  The generated diff describes every change made to upstream
# FFmpeg, while config files capture the complete detected toolchain state.
emit_stable_diff() {
    local relative_path="$1"
    local diff_status=0
    diff -u \
        --label "ffmpeg-${ffmpeg_version}/original/${relative_path}" \
        --label "ffmpeg-${ffmpeg_version}/bigscreen/${relative_path}" \
        "${pristine_root}/${relative_path}" \
        "${source_root}/${relative_path}" || diff_status=$?
    if (( diff_status > 1 )); then
        printf 'Could not generate the FFmpeg source-change record for %s.\n' \
            "${relative_path}" >&2
        return "${diff_status}"
    fi
}

{
    emit_stable_diff "configure"
    emit_stable_diff "libavcodec/libavcodec.v"
    emit_stable_diff "libavformat/libavformat.v"
    emit_stable_diff "libavutil/libavutil.v"
    emit_stable_diff "libswscale/libswscale.v"
} > "${install_root}/bigscreen-ffmpeg-changes.diff"
cp "${build_root}/config.h" "${install_root}/bigscreen-ffmpeg-config.h"
cp "${build_root}/ffbuild/config.mak" "${config_record_path}"
cp "${source_root}/COPYING.LGPLv2.1" "${install_root}/COPYING.LGPLv2.1"
if [[ "${runtime_tag}" == "9" ]]; then
    cp "${build_root}/x264-source/COPYING" \
        "${install_root}/COPYING.X264-GPLv2"
fi

# Fail the build if a future FFmpeg/configure change silently reintroduces a
# normal libav dependency or drops the private symbol namespace.
for library in "${required_outputs[@]}"; do
    library_path="${install_root}/lib/${library}"
    [[ -f "${library_path}" ]] || {
        printf 'Expected FFmpeg library was not produced: %s\n' "${library_path}" >&2
        exit 1
    }
    dynamic_metadata="$("${toolchain_bin}/llvm-readelf" -d "${library_path}")"
    version_metadata="$("${toolchain_bin}/llvm-readelf" --version-info "${library_path}")"
    if grep -Eq 'Shared library: \[lib(avcodec|avformat|avutil|swscale)\.so' <<<"${dynamic_metadata}"; then
        printf 'Unisolated FFmpeg dependency found in %s\n' "${library_path}" >&2
        exit 1
    fi
    if ! grep -q "${symbol_namespace}_LIB" <<<"${version_metadata}"; then
        printf 'Private FFmpeg symbol versions are missing from %s\n' "${library_path}" >&2
        exit 1
    fi
    for host_path in "${cache_root}" "${android_ndk_root}" "${repository_root}"; do
        if grep -aFq "${host_path}" "${library_path}"; then
            printf 'Host-specific path was embedded in %s: %s\n' \
                "${library_path}" "${host_path}" >&2
            exit 1
        fi
    done
done

if grep -Fq "${cache_root}" "${install_root}/bigscreen-ffmpeg-changes.diff"; then
    printf 'Host-specific cache path was written to the FFmpeg change record.\n' >&2
    exit 1
fi

if [[ "${runtime_tag}" == "9" ]]; then
    license_configuration="GPL-2.0-or-later FFmpeg build with pinned GPL-2.0-or-later x264; version3 and nonfree components disabled"
    encoder_configuration="H.264 encoders: Android MediaCodec primary, x264 software fallback"
    x264_build_information="x264 source: ${x264_url}
x264 commit: ${x264_commit}
x264 SHA-256: ${x264_sha256}"
else
    license_configuration="LGPL-2.1-or-later; GPL, version3, and nonfree components disabled"
    encoder_configuration="H.264 encoders: disabled"
    x264_build_information="x264 source: not used"
fi

cat > "${install_root}/BUILD-INFO.txt" <<EOF
FFmpeg version: ${ffmpeg_version}
Upstream source: ${ffmpeg_url}
Upstream SHA-256: ${ffmpeg_sha256}
Android ABI: arm64-v8a
Minimum Android API: ${android_api}
NDK: $(basename "${android_ndk_root}")
License configuration: ${license_configuration}
${encoder_configuration}
${x264_build_information}
Third-party warning policy: compiler warnings suppressed for pinned FFmpeg sources; configure checks and compiler errors remain active
Build script: scripts/build-ffmpeg-lgpl.sh
Build recipe revision: ${build_recipe_revision}
EOF

(cd "${install_root}/lib" && \
    sha256sum *"${build_suffix}".so) > "${install_root}/SHA256SUMS"
printf 'FFmpeg %s private runtime built successfully.\n' "${ffmpeg_version}" > "${stamp_path}"
if [[ "${runtime_tag}" == "9" ]]; then
    printf 'Staged Big Screen GPL FFmpeg with x264 fallback at %s\n' \
        "${install_root}"
else
    printf 'Staged Big Screen LGPL FFmpeg at %s\n' "${install_root}"
fi
