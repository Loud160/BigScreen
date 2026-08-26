// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#include "BigScreen/NativeLogger.hpp"

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace {
    constexpr std::string_view LogcatTag = "BigScreen";
    constexpr std::size_t MaximumMessageBytes = 64u * 1024u;
    constexpr std::size_t LogcatChunkBytes = 3000u;

    const char* SeverityName(BigScreen::LogSeverity severity) noexcept
    {
        switch(severity)
        {
            case BigScreen::LogSeverity::Debug:
                return "DEBUG";
            case BigScreen::LogSeverity::Info:
                return "INFO";
            case BigScreen::LogSeverity::Warning:
                return "WARN";
            case BigScreen::LogSeverity::Error:
                return "ERROR";
            case BigScreen::LogSeverity::Critical:
                return "CRITICAL";
        }
        return "UNKNOWN";
    }

#ifdef __ANDROID__
    int AndroidPriority(BigScreen::LogSeverity severity) noexcept
    {
        switch(severity)
        {
            case BigScreen::LogSeverity::Debug:
                return ANDROID_LOG_DEBUG;
            case BigScreen::LogSeverity::Info:
                return ANDROID_LOG_INFO;
            case BigScreen::LogSeverity::Warning:
                return ANDROID_LOG_WARN;
            case BigScreen::LogSeverity::Error:
                return ANDROID_LOG_ERROR;
            case BigScreen::LogSeverity::Critical:
                return ANDROID_LOG_FATAL;
        }
        return ANDROID_LOG_DEFAULT;
    }
#endif

    void PlatformLog(
        BigScreen::LogSeverity severity,
        std::string_view message) noexcept
    {
#ifdef __ANDROID__
        const int priority = AndroidPriority(severity);
        if(message.empty())
        {
            __android_log_write(priority, LogcatTag.data(), "");
            return;
        }

        for(std::size_t offset = 0; offset < message.size();)
        {
            const std::size_t length =
                std::min(LogcatChunkBytes, message.size() - offset);
            // The precision-limited write avoids allocating a temporary
            // null-terminated string on the producer thread. Large messages
            // are split below Android's practical per-record limit.
            __android_log_print(
                priority,
                LogcatTag.data(),
                "%.*s",
                static_cast<int>(length),
                message.data() + offset);
            offset += length;
        }
#else
        (void)severity;
        (void)message;
#endif
    }

    std::string BoundMessage(std::string message)
    {
        if(message.size() <= MaximumMessageBytes)
            return message;

        constexpr std::string_view suffix = " ... [message truncated]";
        message.resize(MaximumMessageBytes - suffix.size());
        message.append(suffix);
        return message;
    }

    std::string_view BaseName(const char* path) noexcept
    {
        if(!path)
            return {};
        const std::string_view value(path);
        const auto separator = value.find_last_of("/\\");
        return separator == std::string_view::npos
                   ? value
                   : value.substr(separator + 1);
    }

    std::string FormatTimestamp(
        std::chrono::system_clock::time_point timestamp)
    {
        const auto seconds =
            std::chrono::time_point_cast<std::chrono::seconds>(timestamp);
        const auto milliseconds = std::chrono::duration_cast<
            std::chrono::milliseconds>(timestamp - seconds)
                                      .count();
        const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &raw);
#else
        localtime_r(&raw, &local);
#endif
        char buffer[40]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
            local.tm_year + 1900,
            local.tm_mon + 1,
            local.tm_mday,
            local.tm_hour,
            local.tm_min,
            local.tm_sec,
            static_cast<long long>(milliseconds));
        return buffer;
    }

    template<typename T>
    void UpdatePeak(std::atomic<T>& peak, T candidate) noexcept
    {
        T current = peak.load(std::memory_order_relaxed);
        while(current < candidate &&
              !peak.compare_exchange_weak(
                  current,
                  candidate,
                  std::memory_order_relaxed,
                  std::memory_order_relaxed))
        {}
    }
}

namespace BigScreen {
    struct NativeLogger::Impl {
        struct Record {
            std::uint64_t sequence = 0;
            std::chrono::system_clock::time_point timestamp;
            LogSeverity severity = LogSeverity::Info;
            std::size_t threadId = 0;
            std::string message;
            LogSource source;

            [[nodiscard]] std::size_t AccountedBytes() const noexcept
            {
                return sizeof(Record) + message.size();
            }
        };

        mutable std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable flushed;
        std::deque<Record> queue;
        std::thread writer;
        NativeLoggerOptions options;
        std::size_t queueBytes = 0;
        std::uint64_t nextSequence = 1;
        std::uint64_t writtenSequence = 0;
        std::uint64_t flushedSequence = 0;
        std::uint64_t droppedSinceNotice = 0;
        bool initialized = false;
        bool stopping = false;
        bool flushRequested = false;

        std::ofstream stream;
        std::size_t currentFileBytes = 0;
        std::chrono::steady_clock::time_point nextOpenAttempt{};
        std::chrono::steady_clock::time_point lastFlush{};

        std::atomic<std::uint64_t> acceptedMessages{0};
        std::atomic<std::uint64_t> fileMessages{0};
        std::atomic<std::uint64_t> droppedMessages{0};
        std::atomic<std::uint64_t> fileFailures{0};
        std::atomic<std::uint64_t> rotations{0};
        std::atomic<std::size_t> peakQueueBytes{0};
        std::atomic<std::size_t> peakQueueEntries{0};
        // Producer calls may occur while setup is transitioning the logger.
        // Keep the logcat policy independent from the writer-only options so
        // Log never races an options assignment merely to choose its fallback.
        std::atomic<bool> emitToLogcat{true};

        void ResetStatistics() noexcept
        {
            acceptedMessages.store(0, std::memory_order_relaxed);
            fileMessages.store(0, std::memory_order_relaxed);
            droppedMessages.store(0, std::memory_order_relaxed);
            fileFailures.store(0, std::memory_order_relaxed);
            rotations.store(0, std::memory_order_relaxed);
            peakQueueBytes.store(0, std::memory_order_relaxed);
            peakQueueEntries.store(0, std::memory_order_relaxed);
        }

        void ReportFileFailure(std::string_view operation) noexcept
        {
            fileFailures.fetch_add(1, std::memory_order_relaxed);
            try
            {
                PlatformLog(
                    LogSeverity::Error,
                    std::string("Native logger file sink failed while ") +
                        std::string(operation) +
                        "; Android logcat remains active");
            }
            catch(...)
            {
                PlatformLog(
                    LogSeverity::Error,
                    "Native logger file sink failed; Android logcat remains active");
            }
        }

        bool OpenFile() noexcept
        {
            try
            {
                if(options.activePath.empty())
                    return false;

                std::error_code error;
                const auto parent = options.activePath.parent_path();
                if(!parent.empty())
                {
                    std::filesystem::create_directories(parent, error);
                    if(error)
                    {
                        ReportFileFailure("creating the log directory");
                        nextOpenAttempt = std::chrono::steady_clock::now() +
                                          options.reopenInterval;
                        return false;
                    }
                }

                currentFileBytes = 0;
                if(std::filesystem::exists(options.activePath, error) && !error)
                {
                    currentFileBytes = static_cast<std::size_t>(
                        std::filesystem::file_size(options.activePath, error));
                    if(error)
                        currentFileBytes = 0;
                }

                if(currentFileBytes >= options.maxFileBytes && !Rotate())
                {
                    nextOpenAttempt = std::chrono::steady_clock::now() +
                                      options.reopenInterval;
                    return false;
                }

                stream.open(
                    options.activePath,
                    std::ios::binary | std::ios::app);
                if(!stream)
                {
                    ReportFileFailure("opening the active log");
                    stream.close();
                    nextOpenAttempt = std::chrono::steady_clock::now() +
                                      options.reopenInterval;
                    return false;
                }
                nextOpenAttempt = {};
                lastFlush = std::chrono::steady_clock::now();
                return true;
            }
            catch(...)
            {
                ReportFileFailure("opening the active log");
                stream.close();
                nextOpenAttempt = std::chrono::steady_clock::now() +
                                  options.reopenInterval;
                return false;
            }
        }

        bool Rotate() noexcept
        {
            try
            {
                stream.close();
                std::error_code error;
                if(options.previousPath.empty())
                {
                    options.previousPath = options.activePath.parent_path() /
                                           "bigscreen-native.previous.log";
                }

                std::filesystem::remove(options.previousPath, error);
                error.clear();
                std::filesystem::rename(
                    options.activePath,
                    options.previousPath,
                    error);
                if(error)
                {
                    error.clear();
                    std::filesystem::copy_file(
                        options.activePath,
                        options.previousPath,
                        std::filesystem::copy_options::overwrite_existing,
                        error);
                    if(error)
                    {
                        ReportFileFailure("rotating the active log");
                        return false;
                    }
                    std::ofstream truncate(
                        options.activePath,
                        std::ios::binary | std::ios::trunc);
                    if(!truncate)
                    {
                        ReportFileFailure("truncating the rotated log");
                        return false;
                    }
                }
                currentFileBytes = 0;
                rotations.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            catch(...)
            {
                ReportFileFailure("rotating the active log");
                return false;
            }
        }

        bool EnsureFile(std::size_t incomingBytes) noexcept
        {
            const auto now = std::chrono::steady_clock::now();
            if(!stream)
            {
                if(nextOpenAttempt !=
                       std::chrono::steady_clock::time_point{} &&
                   now < nextOpenAttempt)
                {
                    return false;
                }
                if(!OpenFile())
                    return false;
            }

            if(currentFileBytes > 0 &&
               currentFileBytes + incomingBytes > options.maxFileBytes)
            {
                if(!Rotate())
                    return false;
                if(!OpenFile())
                    return false;
            }
            return true;
        }

        std::string FormatRecord(const Record& record)
        {
            std::string line;
            line.reserve(record.message.size() + 128u);
            line.push_back('[');
            line.append(FormatTimestamp(record.timestamp));
            line.append("][");
            line.append(SeverityName(record.severity));
            line.append("][BigScreen][tid=");
            line.append(std::to_string(record.threadId));
            line.append("] ");
            line.append(record.message);

            const auto file = BaseName(record.source.file);
            const std::string_view function = record.source.function
                                                  ? record.source.function
                                                  : "";
            if(!file.empty() || !function.empty())
            {
                line.append(" (");
                if(!file.empty())
                {
                    line.append(file);
                    if(record.source.line > 0)
                    {
                        line.push_back(':');
                        line.append(std::to_string(record.source.line));
                    }
                }
                if(!function.empty())
                {
                    if(!file.empty())
                        line.append(" in ");
                    line.append(function);
                }
                line.push_back(')');
            }
            line.push_back('\n');
            return line;
        }

        bool WriteLine(std::string_view line) noexcept
        {
            try
            {
                if(!EnsureFile(line.size()))
                    return false;
                stream.write(
                    line.data(),
                    static_cast<std::streamsize>(line.size()));
                if(!stream)
                {
                    ReportFileFailure("writing the active log");
                    stream.close();
                    nextOpenAttempt = std::chrono::steady_clock::now() +
                                      options.reopenInterval;
                    return false;
                }
                currentFileBytes += line.size();
                return true;
            }
            catch(...)
            {
                ReportFileFailure("writing the active log");
                stream.close();
                nextOpenAttempt = std::chrono::steady_clock::now() +
                                  options.reopenInterval;
                return false;
            }
        }

        void FlushStream() noexcept
        {
            try
            {
                if(stream)
                {
                    stream.flush();
                    if(!stream)
                    {
                        ReportFileFailure("flushing the active log");
                        stream.close();
                    }
                }
            }
            catch(...)
            {
                ReportFileFailure("flushing the active log");
                stream.close();
            }
            lastFlush = std::chrono::steady_clock::now();
        }

        void WriteDroppedNotice(std::uint64_t count) noexcept
        {
            if(count == 0)
                return;
            try
            {
                const std::string message =
                    "Native logger dropped " + std::to_string(count) +
                    " messages because its bounded queue was full";
                PlatformLog(LogSeverity::Warning, message);
                Record notice;
                notice.timestamp = std::chrono::system_clock::now();
                notice.severity = LogSeverity::Warning;
                notice.threadId =
                    std::hash<std::thread::id>{}(std::this_thread::get_id());
                notice.message = message;
                WriteLine(FormatRecord(notice));
            }
            catch(...)
            {
                PlatformLog(
                    LogSeverity::Warning,
                    "Native logger dropped messages because its bounded queue was full");
            }
        }

        void WorkerLoop() noexcept
        {
            try
            {
                OpenFile();
                std::deque<Record> batch;
                for(;;)
                {
                    std::uint64_t droppedNotice = 0;
                    bool requestedFlush = false;
                    bool shouldStop = false;
                    {
                        std::unique_lock lock(mutex);
                        wake.wait_for(lock, options.flushInterval, [&]() {
                            return stopping || flushRequested ||
                                   !queue.empty() || droppedSinceNotice > 0;
                        });
                        batch.swap(queue);
                        queueBytes = 0;
                        droppedNotice = std::exchange(droppedSinceNotice, 0);
                        requestedFlush = std::exchange(flushRequested, false);
                        shouldStop = stopping;
                    }

                    WriteDroppedNotice(droppedNotice);
                    bool urgent = false;
                    std::uint64_t lastSequence = 0;
                    for(const auto& record : batch)
                    {
                        urgent = urgent ||
                                 record.severity >= LogSeverity::Error;
                        if(WriteLine(FormatRecord(record)))
                        {
                            fileMessages.fetch_add(
                                1,
                                std::memory_order_relaxed);
                        }
                        lastSequence = std::max(lastSequence, record.sequence);
                    }
                    batch.clear();

                    const auto now = std::chrono::steady_clock::now();
                    const bool periodicFlush =
                        now - lastFlush >= options.flushInterval;
                    const bool performedFlush =
                        requestedFlush || urgent || periodicFlush || shouldStop;
                    if(performedFlush)
                        FlushStream();

                    {
                        std::lock_guard lock(mutex);
                        writtenSequence =
                            std::max(writtenSequence, lastSequence);
                        if(performedFlush || !stream)
                        {
                            flushedSequence = std::max(
                                flushedSequence,
                                writtenSequence);
                        }
                    }
                    flushed.notify_all();

                    if(shouldStop)
                    {
                        std::lock_guard lock(mutex);
                        if(queue.empty())
                            break;
                    }
                }
                FlushStream();
                stream.close();
                {
                    std::lock_guard lock(mutex);
                    flushedSequence = std::max(
                        flushedSequence,
                        writtenSequence);
                }
                flushed.notify_all();
            }
            catch(...)
            {
                ReportFileFailure("running the writer thread");
                std::lock_guard lock(mutex);
                queue.clear();
                queueBytes = 0;
                writtenSequence = nextSequence > 0 ? nextSequence - 1 : 0;
                flushedSequence = writtenSequence;
                stopping = true;
                flushed.notify_all();
            }
        }
    };

    NativeLogger& NativeLogger::Instance() noexcept
    {
        static NativeLogger logger;
        return logger;
    }

    NativeLogger::NativeLogger() noexcept
    {
        try
        {
            impl_ = std::make_unique<Impl>();
        }
        catch(...)
        {
            PlatformLog(
                LogSeverity::Error,
                "Native logger could not allocate its private state");
        }
    }

    NativeLogger::~NativeLogger()
    {
        Shutdown();
    }

    bool NativeLogger::Initialize(
        NativeLoggerOptions options,
        std::string sessionHeader) noexcept
    {
        if(!impl_)
            return false;
        try
        {
            Shutdown();
            options.maxFileBytes = std::max<std::size_t>(
                options.maxFileBytes,
                1024u);
            options.flushInterval = std::max(
                options.flushInterval,
                std::chrono::milliseconds(10));
            options.reopenInterval = std::max(
                options.reopenInterval,
                std::chrono::milliseconds(100));
            if(options.previousPath.empty() && !options.activePath.empty())
            {
                options.previousPath = options.activePath.parent_path() /
                                       "bigscreen-native.previous.log";
            }

            {
                std::lock_guard lock(impl_->mutex);
                impl_->options = std::move(options);
                impl_->emitToLogcat.store(
                    impl_->options.emitToLogcat,
                    std::memory_order_relaxed);
                impl_->queue.clear();
                impl_->queueBytes = 0;
                impl_->nextSequence = 1;
                impl_->writtenSequence = 0;
                impl_->flushedSequence = 0;
                impl_->droppedSinceNotice = 0;
                impl_->stopping = false;
                impl_->flushRequested = false;
                impl_->currentFileBytes = 0;
                impl_->nextOpenAttempt = {};
                impl_->lastFlush = std::chrono::steady_clock::now();
                impl_->ResetStatistics();
                impl_->initialized = true;
            }

            try
            {
                impl_->writer =
                    std::thread([state = impl_.get()]() { state->WorkerLoop(); });
            }
            catch(...)
            {
                std::lock_guard lock(impl_->mutex);
                impl_->initialized = false;
                impl_->stopping = true;
                PlatformLog(
                    LogSeverity::Error,
                    "Native logger could not start its private writer thread");
                return false;
            }

            Log(
                LogSeverity::Info,
                sessionHeader.empty()
                    ? "Big Screen native log session started"
                    : std::move(sessionHeader));
            return true;
        }
        catch(...)
        {
            PlatformLog(
                LogSeverity::Error,
                "Native logger initialization failed; Android logcat remains active");
            return false;
        }
    }

    void NativeLogger::Log(
        LogSeverity severity,
        std::string message,
        LogSource source) noexcept
    {
        try
        {
            message = BoundMessage(std::move(message));
            if(!impl_ ||
               impl_->emitToLogcat.load(std::memory_order_relaxed))
                PlatformLog(severity, message);
            if(!impl_)
                return;

            Impl::Record record;
            record.timestamp = std::chrono::system_clock::now();
            record.severity = severity;
            record.threadId =
                std::hash<std::thread::id>{}(std::this_thread::get_id());
            record.message = std::move(message);
            record.source = source;
            const std::size_t recordBytes = record.AccountedBytes();

            std::unique_lock lock(impl_->mutex);
            if(!impl_->initialized || impl_->stopping)
                return;

            const bool urgent = severity >= LogSeverity::Warning;
            const std::size_t entryLimit =
                impl_->options.maxQueueEntries +
                (urgent ? impl_->options.urgentReserveEntries : 0u);
            const std::size_t byteLimit =
                impl_->options.maxQueueBytes +
                (urgent ? impl_->options.urgentReserveBytes : 0u);
            auto wouldOverflow = [&]() {
                return impl_->queue.size() >= entryLimit ||
                       recordBytes > byteLimit ||
                       impl_->queueBytes > byteLimit - recordBytes;
            };

            if(urgent)
            {
                while(wouldOverflow() && !impl_->queue.empty())
                {
                    auto discard = std::find_if(
                        impl_->queue.begin(),
                        impl_->queue.end(),
                        [](const Impl::Record& queued) {
                            return queued.severity < LogSeverity::Warning;
                        });
                    if(discard == impl_->queue.end())
                        discard = impl_->queue.begin();
                    impl_->queueBytes -= discard->AccountedBytes();
                    impl_->queue.erase(discard);
                    ++impl_->droppedSinceNotice;
                    impl_->droppedMessages.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }
            }

            if(wouldOverflow())
            {
                ++impl_->droppedSinceNotice;
                impl_->droppedMessages.fetch_add(
                    1,
                    std::memory_order_relaxed);
                lock.unlock();
                impl_->wake.notify_one();
                return;
            }

            const bool wasEmpty = impl_->queue.empty();
            record.sequence = impl_->nextSequence++;
            impl_->queueBytes += recordBytes;
            impl_->queue.push_back(std::move(record));
            impl_->acceptedMessages.fetch_add(1, std::memory_order_relaxed);
            UpdatePeak(
                impl_->peakQueueBytes,
                impl_->queueBytes);
            UpdatePeak(
                impl_->peakQueueEntries,
                impl_->queue.size());
            lock.unlock();
            if(wasEmpty || urgent)
                impl_->wake.notify_one();
        }
        catch(...)
        {
            PlatformLog(
                LogSeverity::Error,
                "Native logger dropped a message after an internal failure");
        }
    }

    bool NativeLogger::Flush(std::chrono::milliseconds timeout) noexcept
    {
        if(!impl_)
            return false;
        try
        {
            std::unique_lock lock(impl_->mutex);
            if(!impl_->initialized)
                return true;
            const std::uint64_t target =
                impl_->nextSequence > 0 ? impl_->nextSequence - 1 : 0;
            impl_->flushRequested = true;
            impl_->wake.notify_one();
            return impl_->flushed.wait_for(lock, timeout, [&]() {
                return impl_->flushedSequence >= target ||
                       !impl_->initialized;
            });
        }
        catch(...)
        {
            return false;
        }
    }

    void NativeLogger::Shutdown() noexcept
    {
        if(!impl_)
            return;
        try
        {
            {
                std::lock_guard lock(impl_->mutex);
                if(!impl_->initialized && !impl_->writer.joinable())
                    return;
                impl_->stopping = true;
                impl_->flushRequested = true;
            }
            impl_->wake.notify_all();
            if(impl_->writer.joinable())
                impl_->writer.join();
            {
                std::lock_guard lock(impl_->mutex);
                impl_->initialized = false;
                impl_->queue.clear();
                impl_->queueBytes = 0;
            }
            impl_->flushed.notify_all();
        }
        catch(...)
        {
            PlatformLog(
                LogSeverity::Error,
                "Native logger shutdown did not complete cleanly");
        }
    }

    bool NativeLogger::IsInitialized() const noexcept
    {
        if(!impl_)
            return false;
        try
        {
            std::lock_guard lock(impl_->mutex);
            return impl_->initialized;
        }
        catch(...)
        {
            return false;
        }
    }

    NativeLoggerStatistics NativeLogger::Statistics() const noexcept
    {
        NativeLoggerStatistics result;
        if(!impl_)
            return result;
        result.acceptedMessages =
            impl_->acceptedMessages.load(std::memory_order_relaxed);
        result.fileMessages =
            impl_->fileMessages.load(std::memory_order_relaxed);
        result.droppedMessages =
            impl_->droppedMessages.load(std::memory_order_relaxed);
        result.fileFailures =
            impl_->fileFailures.load(std::memory_order_relaxed);
        result.rotations =
            impl_->rotations.load(std::memory_order_relaxed);
        result.peakQueueBytes =
            impl_->peakQueueBytes.load(std::memory_order_relaxed);
        result.peakQueueEntries =
            impl_->peakQueueEntries.load(std::memory_order_relaxed);
        return result;
    }
}
