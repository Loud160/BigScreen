#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#include "BigScreen/QuickJsEngine.hpp"

namespace {
    void Require(bool condition, const char* message)
    {
        if(!condition) throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        const auto success = BigScreen::EvaluateJavaScript(
            "console.log(JSON.stringify({answer: 6 * 7, runtime: 'quickjs'}));");
        Require(static_cast<bool>(success), "normal JavaScript should succeed");
        Require(
            success.output.find("\"answer\":42") != std::string::npos,
            "JSON output should contain the computed value");
        Require(
            success.output.find("\"runtime\":\"quickjs\"") != std::string::npos,
            "JSON output should contain the runtime marker");

        const auto syntaxFailure = BigScreen::EvaluateJavaScript("function {");
        Require(!syntaxFailure, "invalid JavaScript should fail");
        Require(!syntaxFailure.error.empty(), "syntax failures need an explanation");

        const auto timeout = BigScreen::EvaluateJavaScript(
            "while (true) {}",
            std::chrono::milliseconds(25),
            BigScreen::QuickJsDefaultMemoryLimit);
        Require(!timeout, "an infinite loop should fail");
        Require(timeout.timedOut, "an infinite loop should trip the deadline");

        const auto recursion = BigScreen::EvaluateJavaScript(
            "function recurse(){ return recurse(); } recurse();");
        Require(!recursion, "recursive stack exhaustion should be contained");

        const auto excessiveOutput = BigScreen::EvaluateJavaScript(
            "console.log('x'.repeat(9 * 1024 * 1024));");
        Require(!excessiveOutput, "oversized console output should be rejected");
        Require(
            excessiveOutput.error.find("too much output") != std::string::npos,
            "oversized output should report its limit");

        const std::string oversizedSource(
            BigScreen::QuickJsMaximumSourceLength + 1u, ' ');
        const auto sourceFailure = BigScreen::EvaluateJavaScript(oversizedSource);
        Require(!sourceFailure, "oversized JavaScript source should be rejected");
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
