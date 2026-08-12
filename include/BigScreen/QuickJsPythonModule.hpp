#pragma once

#include <Python.h>

/// CPython initialization entry point for Big Screen's built-in QuickJS
/// bridge. Register this with PyImport_AppendInittab before Python starts.
PyMODINIT_FUNC PyInit_bigscreen_quickjs();
