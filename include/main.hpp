// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "scotland2/shared/modloader.h"
#include "beatsaber-hook/shared/config/config-utils.hpp"
#include "paper/shared/logger.hpp"
#include "_config.hpp"

/// Shared structured logger used by lifecycle and playback components. Paper
/// supplies both Android logcat output and the per-mod log file registered in
/// setup(), which makes decoder failures diagnosable without attaching a native
/// debugger to the headset.
constexpr auto PaperLogger = Paper::ConstLoggerContext("bigscreen");

/// Returns the effective persisted menu-preview preference. Selection UI uses
/// this small boundary rather than depending on settings storage details.
bool IsMenuPreviewEnabled();
