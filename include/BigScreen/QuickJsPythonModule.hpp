// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <Python.h>

/// CPython initialization entry point for Big Screen's built-in QuickJS
/// bridge. Register this with PyImport_AppendInittab before Python starts.
PyMODINIT_FUNC PyInit_bigscreen_quickjs();
