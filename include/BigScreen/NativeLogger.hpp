// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "NativeLoggerQuest/NativeLogger.hpp"

namespace BigScreen {
    // Keep Big Screen's established source-level names stable while the
    // implementation is supplied by the reusable, statically linked library.
    // This is a zero-cost type alias: existing call sites, file paths, queue
    // limits, and initialization/shutdown behavior are unchanged.
    using LogSeverity = NativeLoggerQuest::LogSeverity;
    using LogSource = NativeLoggerQuest::LogSource;
    using NativeLoggerOptions = NativeLoggerQuest::NativeLoggerOptions;
    using NativeLoggerStatistics = NativeLoggerQuest::NativeLoggerStatistics;
    using NativeLogger = NativeLoggerQuest::NativeLogger;
}
