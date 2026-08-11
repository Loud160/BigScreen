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

/// Returns the persisted menu-preview preference. Keeping the query behind a
/// function lets a future BSML settings screen change its storage without
/// coupling the playback code to a particular UI framework.
bool IsMenuPreviewEnabled();
