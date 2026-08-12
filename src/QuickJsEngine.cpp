#include "BigScreen/QuickJsEngine.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <new>
#include <string>

extern "C" {
#include "quickjs.h"
}

namespace BigScreen {
    namespace {
        struct ExecutionState {
            std::string output;
            std::chrono::steady_clock::time_point deadline;
            bool timedOut = false;
            bool outputLimitExceeded = false;
        };

        struct RuntimeDeleter {
            void operator()(JSRuntime* runtime) const
            {
                if(runtime) JS_FreeRuntime(runtime);
            }
        };

        struct ContextDeleter {
            void operator()(JSContext* context) const
            {
                if(context) JS_FreeContext(context);
            }
        };

        class ValueOwner final {
        public:
            ValueOwner(JSContext* context, JSValue value)
                : context_(context), value_(value) {}
            ~ValueOwner() { JS_FreeValue(context_, value_); }
            ValueOwner(const ValueOwner&) = delete;
            ValueOwner& operator=(const ValueOwner&) = delete;
            JSValue value() const { return value_; }

        private:
            JSContext* context_;
            JSValue value_;
        };

        int InterruptExecution(JSRuntime*, void* opaque)
        {
            auto& state = *static_cast<ExecutionState*>(opaque);
            if(std::chrono::steady_clock::now() < state.deadline)
                return 0;
            state.timedOut = true;
            return 1;
        }

        JSValue CaptureConsole(
            JSContext* context,
            JSValueConst,
            int argumentCount,
            JSValueConst* arguments)
        {
            auto* state = static_cast<ExecutionState*>(
                JS_GetContextOpaque(context));
            if(!state)
                return JS_ThrowInternalError(
                    context, "Big Screen output capture is unavailable");

            try
            {
                for(int index = 0; index < argumentCount; ++index)
                {
                    std::size_t length = 0;
                    const char* text = JS_ToCStringLen(
                        context, &length, arguments[index]);
                    if(!text)
                        return JS_EXCEPTION;

                    const std::size_t separator = index > 0 ? 1u : 0u;
                    const std::size_t remaining =
                        QuickJsMaximumCapturedOutput - state->output.size();
                    if(separator > remaining || length > remaining - separator)
                    {
                        JS_FreeCString(context, text);
                        state->outputLimitExceeded = true;
                        return JS_ThrowRangeError(
                            context,
                            "Big Screen stopped JavaScript output larger than %zu bytes",
                            QuickJsMaximumCapturedOutput);
                    }
                    if(separator)
                        state->output.push_back(' ');
                    state->output.append(text, length);
                    JS_FreeCString(context, text);
                }
                if(state->output.size() == QuickJsMaximumCapturedOutput)
                {
                    state->outputLimitExceeded = true;
                    return JS_ThrowRangeError(
                        context,
                        "Big Screen stopped JavaScript output larger than %zu bytes",
                        QuickJsMaximumCapturedOutput);
                }
                state->output.push_back('\n');
                return JS_UNDEFINED;
            }
            catch(const std::bad_alloc&)
            {
                // No C++ exception may cross QuickJS's C callback frames.
                return JS_ThrowOutOfMemory(context);
            }
            catch(...)
            {
                return JS_ThrowInternalError(
                    context, "Big Screen could not capture JavaScript output");
            }
        }

        bool InstallConsole(JSContext* context)
        {
            JSValue global = JS_GetGlobalObject(context);
            if(JS_IsException(global))
                return false;

            JSValue console = JS_NewObject(context);
            if(JS_IsException(console))
            {
                JS_FreeValue(context, global);
                return false;
            }

            // EJS emits its response through console.log. Provide the familiar
            // aliases as well so upstream diagnostic output remains captured
            // instead of escaping to Android stdout.
            constexpr std::array names{
                "log", "info", "warn", "error", "debug"};
            for(const char* name : names)
            {
                if(JS_SetPropertyStr(
                       context,
                       console,
                       name,
                       JS_NewCFunction(context, CaptureConsole, name, 1)) < 0)
                {
                    JS_FreeValue(context, console);
                    JS_FreeValue(context, global);
                    return false;
                }
            }

            if(JS_SetPropertyStr(context, global, "console", console) < 0 ||
               JS_SetPropertyStr(
                   context,
                   global,
                   "print",
                   JS_NewCFunction(
                       context, CaptureConsole, "print", 1)) < 0)
            {
                JS_FreeValue(context, global);
                return false;
            }

            // qjs normally exposes an empty argument list to scripts. The EJS
            // solver does not currently use it, but matching that environment
            // avoids a needless incompatibility if upstream begins checking it.
            JSValue arguments = JS_NewArray(context);
            if(JS_IsException(arguments) ||
               JS_SetPropertyStr(
                   context, global, "scriptArgs", arguments) < 0)
            {
                if(JS_IsException(arguments))
                    JS_FreeValue(context, arguments);
                JS_FreeValue(context, global);
                return false;
            }

            JS_FreeValue(context, global);
            return true;
        }

        std::string ExceptionText(JSContext* context)
        {
            ValueOwner exception{context, JS_GetException(context)};
            std::string message = "JavaScript execution failed";
            if(const char* text = JS_ToCString(context, exception.value()))
            {
                message = text;
                JS_FreeCString(context, text);
            }

            ValueOwner stack{
                context,
                JS_GetPropertyStr(context, exception.value(), "stack")};
            if(!JS_IsUndefined(stack.value()) && !JS_IsNull(stack.value()))
            {
                if(const char* text = JS_ToCString(context, stack.value()))
                {
                    const std::string stackText{text};
                    if(!stackText.empty() && stackText != message)
                        message += "\n" + stackText;
                    JS_FreeCString(context, text);
                }
            }
            return message;
        }
    }

    JavaScriptEvaluation EvaluateJavaScript(
        std::string_view source,
        std::chrono::milliseconds timeout,
        std::size_t memoryLimitBytes)
    {
        JavaScriptEvaluation result;
        if(source.empty())
        {
            result.error = "The JavaScript challenge was empty.";
            return result;
        }
        if(source.size() > QuickJsMaximumSourceLength)
        {
            result.error = "The YouTube JavaScript challenge was unexpectedly large.";
            return result;
        }
        if(timeout <= std::chrono::milliseconds::zero())
        {
            result.error = "The JavaScript execution timeout was invalid.";
            return result;
        }
        if(memoryLimitBytes < 16u * 1024u * 1024u)
        {
            result.error = "The JavaScript memory limit was too small.";
            return result;
        }

        ExecutionState state;
        state.deadline = std::chrono::steady_clock::now() + timeout;

        std::unique_ptr<JSRuntime, RuntimeDeleter> runtime{JS_NewRuntime()};
        if(!runtime)
        {
            result.error = "QuickJS-NG could not create a runtime.";
            return result;
        }
        JS_SetMemoryLimit(runtime.get(), memoryLimitBytes);
        // Android mod worker stacks are shared with CPython and native frames.
        // Stay below QuickJS's 1 MiB desktop default so recursion is rejected
        // before reaching the pthread guard page on Quest.
        JS_SetMaxStackSize(runtime.get(), QuickJsMaximumStackSize);
        JS_SetInterruptHandler(runtime.get(), InterruptExecution, &state);

        std::unique_ptr<JSContext, ContextDeleter> context{
            JS_NewContext(runtime.get())};
        if(!context)
        {
            result.error = "QuickJS-NG could not create an execution context.";
            return result;
        }
        JS_SetContextOpaque(context.get(), &state);

        if(!InstallConsole(context.get()))
        {
            result.error = ExceptionText(context.get());
        }
        else
        {
            ValueOwner value{context.get(), JS_Eval(
                context.get(),
                source.data(),
                source.size(),
                "<big-screen-youtube-challenge>",
                JS_EVAL_TYPE_GLOBAL)};
            if(JS_IsException(value.value()))
                result.error = ExceptionText(context.get());

            // The current EJS solver is synchronous, but drain pending jobs so
            // a future compatible solver can use resolved promises without
            // silently losing its final console output.
            if(result.error.empty())
            {
                JSContext* jobContext = nullptr;
                int jobResult = 0;
                while((jobResult = JS_ExecutePendingJob(
                           runtime.get(), &jobContext)) > 0)
                {
                }
                if(jobResult < 0)
                    result.error = ExceptionText(
                        jobContext ? jobContext : context.get());
            }
        }

        result.timedOut = state.timedOut;
        if(result.timedOut)
            result.error = "QuickJS-NG stopped a YouTube challenge that exceeded its time limit.";
        else if(state.outputLimitExceeded)
            result.error = "QuickJS-NG stopped a YouTube challenge that produced too much output.";
        result.output = std::move(state.output);

        JS_SetContextOpaque(context.get(), nullptr);

        if(result.error.empty() && result.output.empty())
            result.error = "The YouTube JavaScript challenge produced no output.";
        return result;
    }
}
