// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#pragma once

// Upstream miniz normally generates this definition through its own CMake
// build. Big Screen embeds only the compressor sources in a private host tool,
// so no import/export decoration is required.
#define MINIZ_EXPORT
