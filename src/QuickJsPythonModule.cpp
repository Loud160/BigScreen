#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <chrono>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>

#include "BigScreen/QuickJsEngine.hpp"

namespace {
    /// Releases the GIL while QuickJS performs CPU work and restores it during
    /// every normal or exceptional exit from the scope.
    class AllowPythonThreads final {
    public:
        AllowPythonThreads() : state_(PyEval_SaveThread()) {}
        ~AllowPythonThreads() { PyEval_RestoreThread(state_); }
        AllowPythonThreads(const AllowPythonThreads&) = delete;
        AllowPythonThreads& operator=(const AllowPythonThreads&) = delete;

    private:
        PyThreadState* state_;
    };

    PyObject* Execute(
        PyObject*,
        PyObject* arguments,
        PyObject* keywords)
    {
        const char* source = nullptr;
        Py_ssize_t sourceLength = 0;
        unsigned long timeoutMilliseconds = static_cast<unsigned long>(
            BigScreen::QuickJsDefaultTimeout.count());
        unsigned long memoryMebibytes = static_cast<unsigned long>(
            BigScreen::QuickJsDefaultMemoryLimit / (1024u * 1024u));
        static const char* names[]{
            "source", "timeout_ms", "memory_mib", nullptr};
        if(!PyArg_ParseTupleAndKeywords(
               arguments,
               keywords,
               "s#|kk:execute",
               const_cast<char**>(names),
               &source,
               &sourceLength,
               &timeoutMilliseconds,
               &memoryMebibytes))
            return nullptr;

        if(timeoutMilliseconds < 1000 || timeoutMilliseconds > 120000)
        {
            PyErr_SetString(
                PyExc_ValueError,
                "timeout_ms must be between 1000 and 120000");
            return nullptr;
        }
        if(memoryMebibytes < 16 || memoryMebibytes > 512)
        {
            PyErr_SetString(
                PyExc_ValueError,
                "memory_mib must be between 16 and 512");
            return nullptr;
        }
        if(sourceLength < 0 ||
           static_cast<unsigned long long>(sourceLength) >
               std::numeric_limits<std::size_t>::max())
        {
            PyErr_SetString(PyExc_OverflowError, "JavaScript source is too large");
            return nullptr;
        }
        if(static_cast<unsigned long long>(sourceLength) >
           BigScreen::QuickJsMaximumSourceLength)
        {
            PyErr_SetString(
                PyExc_ValueError,
                "source exceeds Big Screen's 16 MiB JavaScript limit");
            return nullptr;
        }

        BigScreen::JavaScriptEvaluation result;
        try
        {
            AllowPythonThreads allowThreads;
            result = BigScreen::EvaluateJavaScript(
                std::string_view{
                    source, static_cast<std::size_t>(sourceLength)},
                std::chrono::milliseconds(timeoutMilliseconds),
                static_cast<std::size_t>(memoryMebibytes) * 1024u * 1024u);
        }
        catch(const std::bad_alloc&)
        {
            PyErr_NoMemory();
            return nullptr;
        }
        catch(const std::exception& error)
        {
            PyErr_SetString(PyExc_RuntimeError, error.what());
            return nullptr;
        }
        catch(...)
        {
            PyErr_SetString(
                PyExc_RuntimeError,
                "Big Screen stopped an unexpected native JavaScript error");
            return nullptr;
        }

        if(!result)
        {
            PyErr_SetString(PyExc_RuntimeError, result.error.c_str());
            return nullptr;
        }
        return PyUnicode_DecodeUTF8(
            result.output.data(), result.output.size(), "strict");
    }

    PyMethodDef Methods[]{
        {
            "execute",
            reinterpret_cast<PyCFunction>(Execute),
            METH_VARARGS | METH_KEYWORDS,
            "Execute a yt-dlp JavaScript challenge in the sandboxed in-process QuickJS-NG runtime."
        },
        {nullptr, nullptr, 0, nullptr}
    };

    PyModuleDef Module{
        PyModuleDef_HEAD_INIT,
        "bigscreen_quickjs",
        "Big Screen's in-process QuickJS-NG bridge for yt-dlp.",
        -1,
        Methods
    };
}

PyMODINIT_FUNC PyInit_bigscreen_quickjs()
{
    PyObject* module = PyModule_Create(&Module);
    if(!module)
        return nullptr;
    if(PyModule_AddStringConstant(
           module,
           "version",
           BigScreen::QuickJsVersion.data()) < 0)
    {
        Py_DECREF(module);
        return nullptr;
    }
    return module;
}
