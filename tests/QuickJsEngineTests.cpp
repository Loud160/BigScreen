#include <cassert>
#include <chrono>
#include <string>

#include "BigScreen/QuickJsEngine.hpp"

int main()
{
    const auto success = BigScreen::EvaluateJavaScript(
        "console.log(JSON.stringify({answer: 6 * 7, runtime: 'quickjs'}));");
    assert(success);
    assert(success.output.find("\"answer\":42") != std::string::npos);
    assert(success.output.find("\"runtime\":\"quickjs\"") != std::string::npos);

    const auto syntaxFailure = BigScreen::EvaluateJavaScript("function {");
    assert(!syntaxFailure);
    assert(!syntaxFailure.error.empty());

    const auto timeout = BigScreen::EvaluateJavaScript(
        "while (true) {}",
        std::chrono::milliseconds(25),
        BigScreen::QuickJsDefaultMemoryLimit);
    assert(!timeout);
    assert(timeout.timedOut);

    return 0;
}
