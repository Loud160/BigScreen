#pragma once

#include "scotland2/shared/modloader.h"
#include "paper/shared/logger.hpp"
#include "_config.hpp"

/// Shared structured logger used by lifecycle and playback components. Paper
/// supplies both Android logcat output and the per-mod log file registered in
/// setup(), which makes decoder failures diagnosable without attaching a native
/// debugger to the headset.
constexpr auto PaperLogger = Paper::ConstLoggerContext("bigscreen");
