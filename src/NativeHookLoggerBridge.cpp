// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#include "BigScreen/NativeLogger.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {
    constexpr std::size_t MaximumBridgeFieldBytes = 64u * 1024u;

    std::string_view BoundedField(
        const std::uint8_t* data,
        std::size_t length) noexcept
    {
        if(!data || length == 0)
            return {};
        return {
            reinterpret_cast<const char*>(data),
            std::min(length, MaximumBridgeFieldBytes),
        };
    }

    BigScreen::LogSeverity BridgeSeverity(int level) noexcept
    {
        // Paper2's public C ABI orders its levels as Info, Warn, Error,
        // Debug, Crit, Off. These numeric values are used only at the private
        // linker-wrap boundary; Big Screen does not initialize or call Paper.
        switch(level)
        {
            case 0:
                return BigScreen::LogSeverity::Info;
            case 1:
                return BigScreen::LogSeverity::Warning;
            case 2:
                return BigScreen::LogSeverity::Error;
            case 3:
                return BigScreen::LogSeverity::Debug;
            case 4:
                return BigScreen::LogSeverity::Critical;
            default:
                return BigScreen::LogSeverity::Debug;
        }
    }
}

// beatsaber-hook's inline SAFE_ABORT helpers currently route their diagnostics
// through two Paper2 C-ABI calls. Link-time --wrap redirects only the
// references linked into libbigscreen.so to these hidden functions. Their
// names are not exported and cannot satisfy, replace, or intercept Paper2 calls
// made by BSML, SongCore, or any other mod. This preserves hook failure details
// without giving Big Screen a runtime dependency on Paper's ABI.
extern "C" __attribute__((visibility("hidden"))) bool
__wrap_paper2_queue_log_bytes_ffi(
    int level,
    const std::uint8_t* tagData,
    std::uintptr_t tagLength,
    const std::uint8_t* messageData,
    std::uintptr_t messageLength,
    const std::uint8_t* fileData,
    std::uintptr_t fileLength,
    int line,
    int,
    const std::uint8_t* functionData,
    std::uintptr_t functionLength) noexcept
{
    try
    {
        // Off is intentionally discarded, matching the source logger's
        // disabled level without allocating or waking Big Screen's writer.
        if(level == 5)
            return true;

        const auto tag = BoundedField(tagData, tagLength);
        const auto message = BoundedField(messageData, messageLength);
        const auto file = BoundedField(fileData, fileLength);
        const auto function = BoundedField(functionData, functionLength);

        std::string record;
        record.reserve(
            tag.size() + message.size() + file.size() + function.size() + 48u);
        if(!tag.empty())
        {
            record.push_back('[');
            record.append(tag);
            record.append("] ");
        }
        record.append(message);
        if(!file.empty())
        {
            record.append(" (");
            record.append(file);
            if(line > 0)
            {
                record.push_back(':');
                record.append(std::to_string(line));
            }
            if(!function.empty())
            {
                record.append(" in ");
                record.append(function);
            }
            record.push_back(')');
        }

        BigScreen::NativeLogger::Instance().Log(
            BridgeSeverity(level),
            std::move(record));
        return true;
    }
    catch(...)
    {
        return false;
    }
}

extern "C" __attribute__((visibility("hidden"))) bool
__wrap_paper2_wait_for_flush() noexcept
{
    return BigScreen::NativeLogger::Instance().Flush(
        std::chrono::milliseconds(250));
}
