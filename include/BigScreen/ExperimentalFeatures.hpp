// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The Cinema bloom/color-blending experiment is intentionally preserved for
// future research but excluded from release builds. Keep one named feature
// gate instead of unrelated `#if 0` blocks so parsers, surfaces, hooks, UI, and
// tests cannot silently disagree about whether the experiment is active.
#ifndef BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
#define BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM 0
#endif
