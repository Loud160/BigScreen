#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace BigScreen {
    inline constexpr std::string_view QuickJsVersion = "0.16.1";
    inline constexpr std::chrono::milliseconds QuickJsDefaultTimeout{30000};
    inline constexpr std::size_t QuickJsDefaultMemoryLimit =
        128u * 1024u * 1024u;
    inline constexpr std::size_t QuickJsMaximumSourceLength =
        16u * 1024u * 1024u;
    inline constexpr std::size_t QuickJsMaximumCapturedOutput =
        8u * 1024u * 1024u;
    inline constexpr std::size_t QuickJsMaximumStackSize =
        512u * 1024u;

    /// Result returned by the isolated JavaScript engine. Each evaluation owns
    /// a new runtime, so a failed YouTube challenge cannot poison a later job.
    struct JavaScriptEvaluation {
        std::string output;
        std::string error;
        bool timedOut = false;

        explicit operator bool() const { return error.empty(); }
    };

    /// Evaluates one yt-dlp EJS challenge script inside QuickJS-NG. The engine
    /// has no file or network APIs, has a fixed memory ceiling, and is stopped
    /// by an interrupt callback if it exceeds the deadline.
    JavaScriptEvaluation EvaluateJavaScript(
        std::string_view source,
        std::chrono::milliseconds timeout = QuickJsDefaultTimeout,
        std::size_t memoryLimitBytes = QuickJsDefaultMemoryLimit);
}
