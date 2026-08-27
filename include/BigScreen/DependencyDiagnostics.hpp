// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

namespace BigScreen::DependencyDiagnostics {
    /// Audits the completed Scotland2 dependency set once after the late-mod
    /// phase. Calls before that phase are harmless no-ops; the first stable
    /// menu update performs the check. Problems are written in plain language
    /// and queued for Beat Saber's shared frontmost dialog.
    void CheckLoadedDependencies() noexcept;
}
