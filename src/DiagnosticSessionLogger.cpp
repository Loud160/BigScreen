// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/DiagnosticSessionLogger.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "main.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace BigScreen {
    namespace {
        constexpr const char* MenuDirectory =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/Sessions/Menu";
        constexpr const char* DownloadDirectory =
            "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Logs/Sessions/Download";
        constexpr std::size_t RetainedSessionCount = 10;
        std::atomic<std::uint64_t> collisionCounter{0};

        std::tm UtcTime(std::time_t value) noexcept
        {
            std::tm result{};
#if defined(_WIN32)
            gmtime_s(&result, &value);
#else
            gmtime_r(&value, &result);
#endif
            return result;
        }

        std::string TimestampUtc(
            std::chrono::system_clock::time_point now,
            bool fileSafe)
        {
            const auto milliseconds = std::chrono::duration_cast<
                std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
            const auto time = std::chrono::system_clock::to_time_t(now);
            const auto utc = UtcTime(time);
            std::ostringstream output;
            output << std::put_time(
                &utc,
                fileSafe ? "%Y-%m-%d-%H%M%S" : "%Y-%m-%dT%H:%M:%S")
                   << (fileSafe ? "-" : ".")
                   << std::setw(3) << std::setfill('0')
                   << milliseconds.count();
            if(!fileSafe)
                output << 'Z';
            return output.str();
        }

        std::string Number(double value)
        {
            std::ostringstream output;
            output << std::fixed << std::setprecision(4) << value;
            auto result = output.str();
            while(result.size() > 1 && result.back() == '0') result.pop_back();
            if(!result.empty() && result.back() == '.') result.pop_back();
            return result;
        }
    }

    DiagnosticSessionLogger& DiagnosticSessionLogger::Instance()
    {
        static DiagnosticSessionLogger logger;
        return logger;
    }

    void DiagnosticSessionLogger::BeginMenuSession(
        DiagnosticFields context) noexcept
    {
        Begin(
            menu_, MenuDirectory, "BigScreen-Menu-", "menu",
            std::move(context));
    }

    void DiagnosticSessionLogger::EndMenuSession(std::string reason) noexcept
    {
        FlushPendingSliders();
        End(menu_, std::move(reason), {});
    }

    void DiagnosticSessionLogger::BeginDownloadSession(
        DiagnosticFields context) noexcept
    {
        // A single DownloadManager operation is allowed at a time. Close an
        // abandoned pre-transfer choice before opening the next click session.
        End(download_, "replaced_by_new_download_action", {});
        Begin(
            download_, DownloadDirectory, "BigScreen-Download-", "download",
            std::move(context));
    }

    void DiagnosticSessionLogger::EndDownloadSession(
        std::string outcome,
        DiagnosticFields fields) noexcept
    {
        End(download_, std::move(outcome), std::move(fields));
    }

    void DiagnosticSessionLogger::MenuEvent(
        std::string event,
        std::string source,
        DiagnosticFields fields) noexcept
    {
        Write(menu_, event, source, fields);
    }

    void DiagnosticSessionLogger::DownloadEvent(
        std::string event,
        std::string source,
        DiagnosticFields fields) noexcept
    {
        Write(download_, event, source, fields);
    }

    void DiagnosticSessionLogger::SliderChanged(
        std::string control,
        double previousValue,
        double currentValue) noexcept
    {
        if(!MenuSessionActive() || std::abs(previousValue - currentValue) < 0.00001)
            return;
        try
        {
            std::scoped_lock lock(sliderMutex_);
            auto [position, inserted] = pendingSliders_.try_emplace(
                std::move(control),
                PendingSlider{previousValue, currentValue,
                    std::chrono::steady_clock::now()});
            if(!inserted)
            {
                position->second.latest = currentValue;
                position->second.changed = std::chrono::steady_clock::now();
            }
        }
        catch(...)
        {
            // Optional diagnostics must never interfere with the UI callback.
        }
    }

    void DiagnosticSessionLogger::Tick() noexcept
    {
        std::vector<std::pair<std::string, PendingSlider>> ready;
        try
        {
            const auto now = std::chrono::steady_clock::now();
            {
                std::scoped_lock lock(sliderMutex_);
                for(auto iterator = pendingSliders_.begin();
                    iterator != pendingSliders_.end();)
                {
                    if(now - iterator->second.changed <
                       std::chrono::milliseconds(400))
                    {
                        ++iterator;
                        continue;
                    }
                    ready.emplace_back(iterator->first, iterator->second);
                    iterator = pendingSliders_.erase(iterator);
                }
            }
            for(const auto& [control, slider] : ready)
                MenuEvent("setting_changed", "SettingsMenu", {
                    {"control", control},
                    {"initialValue", Number(slider.initial)},
                    {"finalValue", Number(slider.latest)},
                    {"interaction", "slider"}});
        }
        catch(...)
        {
        }
    }

    void DiagnosticSessionLogger::FlushPendingSliders() noexcept
    {
        std::vector<std::pair<std::string, PendingSlider>> pending;
        try
        {
            {
                std::scoped_lock lock(sliderMutex_);
                for(const auto& value : pendingSliders_)
                    pending.push_back(value);
                pendingSliders_.clear();
            }
            for(const auto& [control, slider] : pending)
                MenuEvent("setting_changed", "SettingsMenu", {
                    {"control", control},
                    {"initialValue", Number(slider.initial)},
                    {"finalValue", Number(slider.latest)},
                    {"interaction", "slider"}});
        }
        catch(...)
        {
        }
    }

    void DiagnosticSessionLogger::CorrelatedError(
        const std::string& correlationId,
        const std::string& context,
        const std::string& conciseDetail) noexcept
    {
        const DiagnosticFields fields{
            {"correlationId", correlationId},
            {"context", context},
            {"message", SanitizeExternalMessage(conciseDetail)},
            {"history", "BigScreen/Logs/error-history.log"}};
        MenuEvent("error", "ErrorManager", fields);
        DownloadEvent("error", "ErrorManager", fields);
    }

    bool DiagnosticSessionLogger::MenuSessionActive() const noexcept
    {
        std::scoped_lock lock(menu_.mutex);
        return menu_.stream.is_open();
    }

    bool DiagnosticSessionLogger::DownloadSessionActive() const noexcept
    {
        std::scoped_lock lock(download_.mutex);
        return download_.stream.is_open();
    }

    std::string DiagnosticSessionLogger::SanitizeExternalMessage(
        std::string message)
    {
        // Operational messages may contain a temporary media URL. Preserve
        // the useful host/path category while removing every query/fragment,
        // Authorization/cookie/token line, and common bearer-like value.
        const auto lower = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        };
        std::istringstream input(message);
        std::ostringstream output;
        std::string line;
        bool first = true;
        while(std::getline(input, line))
        {
            const auto normalized = lower(line);
            if(normalized.find("authorization:") != std::string::npos ||
               normalized.find("cookie:") != std::string::npos ||
               normalized.find("po_token") != std::string::npos ||
               normalized.find("potoken") != std::string::npos)
                line = "[redacted sensitive downloader value]";
            std::size_t search = 0;
            while((search = line.find("http", search)) != std::string::npos)
            {
                const auto query = line.find_first_of("?#", search);
                if(query == std::string::npos) break;
                const auto end = line.find_first_of(" \t\r\n'\"", query);
                line.replace(query, end == std::string::npos
                    ? line.size() - query : end - query, "?[redacted]");
                search = query + 11;
            }
            if(!first) output << '\n';
            output << line;
            first = false;
        }
        auto result = output.str();
        constexpr std::size_t MaximumMessageBytes = 2048;
        if(result.size() > MaximumMessageBytes)
            result = result.substr(0, MaximumMessageBytes) + "...[truncated]";
        return result;
    }

    void DiagnosticSessionLogger::Begin(
        SessionSink& sink,
        const char* directory,
        const char* filePrefix,
        const char* sessionType,
        DiagnosticFields context) noexcept
    {
        try
        {
            std::string activePath;
            {
                std::scoped_lock lock(sink.mutex);
                if(sink.stream.is_open())
                {
                    sink.stream.flush();
                    sink.stream.close();
                }
                std::filesystem::create_directories(directory);
                const auto now = std::chrono::system_clock::now();
                const auto unique = collisionCounter.fetch_add(1);
                activePath = (std::filesystem::path(directory) /
                    (std::string(filePrefix) + TimestampUtc(now, true) + "-" +
                     std::to_string(unique) + ".jsonl")).string();
                sink.stream.open(activePath, std::ios::out | std::ios::app);
                if(!sink.stream)
                    return;
                sink.started = std::chrono::steady_clock::now();
                sink.type = sessionType;
                sink.path = activePath;
                sink.warned = false;
            }
            RetainNewest(directory, activePath);
            context.emplace_back("bigScreenVersion", VERSION);
            Write(sink, "session_start", "BigScreen", context);
        }
        catch(const std::exception& exception)
        {
            bool warn = false;
            {
                std::scoped_lock lock(sink.mutex);
                warn = !sink.warned;
                sink.warned = true;
                if(sink.stream.is_open()) sink.stream.close();
            }
            if(warn)
                BigScreen::BigScreenLogger.warn(
                    "Detailed diagnostic session could not start: {}",
                    exception.what());
        }
        catch(...)
        {
        }
    }

    void DiagnosticSessionLogger::End(
        SessionSink& sink,
        std::string outcome,
        DiagnosticFields fields) noexcept
    {
        fields.emplace_back("outcome", std::move(outcome));
        Write(sink, "session_end", "BigScreen", fields);
        try
        {
            std::scoped_lock lock(sink.mutex);
            if(sink.stream.is_open())
            {
                sink.stream.flush();
                sink.stream.close();
            }
            sink.path.clear();
        }
        catch(...)
        {
        }
    }

    void DiagnosticSessionLogger::Write(
        SessionSink& sink,
        const std::string& event,
        const std::string& source,
        const DiagnosticFields& fields) noexcept
    {
        bool reportFailure = false;
        try
        {
            {
                std::scoped_lock lock(sink.mutex);
                if(!sink.stream.is_open()) return;
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                writer.StartObject();
                writer.Key("timestampUtc");
                writer.String(TimestampUtc(
                    std::chrono::system_clock::now(), false).c_str());
                writer.Key("elapsedMs");
                writer.Int64(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - sink.started).count());
                writer.Key("sessionType"); writer.String(sink.type.c_str());
                writer.Key("event"); writer.String(event.c_str());
                writer.Key("source"); writer.String(source.c_str());
                writer.Key("data"); writer.StartObject();
                for(const auto& [name, value] : fields)
                {
                    writer.Key(name.c_str());
                    writer.String(value.c_str());
                }
                writer.EndObject();
                writer.EndObject();
                sink.stream << buffer.GetString() << '\n';
                sink.stream.flush();
                if(!sink.stream)
                {
                    sink.stream.close();
                    reportFailure = !sink.warned;
                    sink.warned = true;
                }
            }
            // Never call Paper while the sink mutex is held. Diagnostics do
            // not participate in another subsystem's lock ordering.
            if(reportFailure)
                BigScreen::BigScreenLogger.warn(
                    "Detailed diagnostic session stopped after a write failure");
        }
        catch(...)
        {
            // Never report through ErrorManager: that would recurse back here.
            try
            {
                std::scoped_lock lock(sink.mutex);
                if(sink.stream.is_open()) sink.stream.close();
            }
            catch(...)
            {
            }
        }
    }

    void DiagnosticSessionLogger::RetainNewest(
        const std::string& directory,
        const std::string& activePath) noexcept
    {
        try
        {
            std::vector<std::filesystem::directory_entry> sessions;
            for(const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if(entry.is_regular_file() && entry.path().extension() == ".jsonl")
                    sessions.push_back(entry);
            }
            std::sort(sessions.begin(), sessions.end(),
                [](const auto& left, const auto& right) {
                    return left.last_write_time() > right.last_write_time();
                });
            std::unordered_set<std::string> retained;
            retained.insert(activePath);
            for(const auto& session : sessions)
            {
                if(retained.size() >= RetainedSessionCount) break;
                retained.insert(session.path().string());
            }
            for(const auto& session : sessions)
            {
                if(retained.contains(session.path().string())) continue;
                std::error_code error;
                std::filesystem::remove(session.path(), error);
            }
        }
        catch(...)
        {
            // Rotation failure must not disable an otherwise writable session.
        }
    }
}
