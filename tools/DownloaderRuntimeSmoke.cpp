#include <Python.h>

#include <iostream>

namespace {
    constexpr auto RuntimeRoot = "/data/local/tmp/bigscreen-python";

    bool AppendPath(PyConfig& config, const wchar_t* path)
    {
        const auto status = PyWideStringList_Append(&config.module_search_paths, path);
        if(PyStatus_Exception(status))
        {
            std::cerr << "Could not add a Python module path: "
                      << (status.err_msg ? status.err_msg : "unknown error")
                      << '\n';
            return false;
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    // This executable isolates the official Android runtime from Beat Saber.
    // It proves ARM64 loading, compressed stdlib imports, OpenSSL networking,
    // yt-dlp zipimport, and YouTube metadata extraction on the physical Quest.
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.module_search_paths_set = 1;
    if(!AppendPath(config, L"/data/local/tmp/bigscreen-python/python314.zip") ||
       !AppendPath(config, L"/data/local/tmp/bigscreen-python/lib-dynload") ||
       !AppendPath(config, L"/data/local/tmp/bigscreen-python/certifi.whl") ||
       !AppendPath(config, L"/data/local/tmp/bigscreen-python/yt-dlp-shipped"))
    {
        PyConfig_Clear(&config);
        return 2;
    }

    auto status = PyConfig_SetBytesArgv(&config, argc, argv);
    if(!PyStatus_Exception(status))
        status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if(PyStatus_Exception(status))
    {
        std::cerr << "Python initialization failed: "
                  << (status.err_msg ? status.err_msg : "unknown error")
                  << '\n';
        return 3;
    }

    const char* test = R"PY(
import json
import ssl
import sys
import yt_dlp

url = sys.argv[1] if len(sys.argv) > 1 else 'https://youtu.be/QDaEBqV-US4'
print(json.dumps({
    'python': sys.version.split()[0],
    'openssl': ssl.OPENSSL_VERSION,
    'yt_dlp': yt_dlp.version.__version__,
    'url': url,
}), flush=True)

options = {
    'quiet': True,
    'no_warnings': True,
    'noplaylist': True,
    'skip_download': True,
}
with yt_dlp.YoutubeDL(options) as downloader:
    info = downloader.extract_info(url, download=False)
print(json.dumps({
    'id': info.get('id'),
    'title': info.get('title'),
    'age_limit': info.get('age_limit'),
    'duration': info.get('duration'),
    'formats': len(info.get('formats') or []),
}), flush=True)
)PY";

    const int result = PyRun_SimpleString(test);
    if(result != 0)
        PyErr_Print();
    const int finalizeResult = Py_FinalizeEx();
    return result == 0 && finalizeResult >= 0 ? 0 : 4;
}
