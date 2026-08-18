// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// Small C ABI intentionally exported for native Quest mods that need the two
// scene-transition decisions PC Cinema exposes as managed events. Consumers
// should resolve these symbols dynamically so Big Screen remains optional.
extern "C" {
    __attribute__((visibility("default")))
    bool bigscreen_cinema_presentation_active();

    __attribute__((visibility("default")))
    bool bigscreen_cinema_allows_custom_platform();
}
