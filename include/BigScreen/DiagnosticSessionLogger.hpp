// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <chrono>
#include <fstream>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace BigScreen {
    using DiagnosticFields =
        std::vector<std::pair<std::string, std::string>>;

    /// Owns the optional, crash-oriented JSONL menu and download sessions.
    /// Important events append and flush synchronously. The two sinks never
    /// lock each other and this class never calls another Big Screen subsystem
    /// while holding a sink mutex, keeping diagnostics out of lock ordering.
    class DiagnosticSessionLogger final {
    public:
        static DiagnosticSessionLogger& Instance();

        void BeginMenuSession(DiagnosticFields context = {}) noexcept;
        void EndMenuSession(std::string reason = "menu_closed") noexcept;
        void BeginDownloadSession(DiagnosticFields context) noexcept;
        void EndDownloadSession(
            std::string outcome,
            DiagnosticFields fields = {}) noexcept;

        void MenuEvent(
            std::string event,
            std::string source,
            DiagnosticFields fields = {}) noexcept;
        void DownloadEvent(
            std::string event,
            std::string source,
            DiagnosticFields fields = {}) noexcept;

        /// Coalesces rapid slider callbacks without a worker thread. Tick()
        /// publishes the initial/final pair after 400 ms of inactivity.
        void SliderChanged(
            std::string control,
            double previousValue,
            double currentValue) noexcept;
        void Tick() noexcept;
        void FlushPendingSliders() noexcept;

        /// Mirrors only the concise correlation into active session logs. The
        /// persistent error history remains authoritative for full details.
        void CorrelatedError(
            const std::string& correlationId,
            const std::string& context,
            const std::string& conciseDetail) noexcept;

        bool MenuSessionActive() const noexcept;
        bool DownloadSessionActive() const noexcept;

        static std::string SanitizeExternalMessage(std::string message);

    private:
        struct SessionSink {
            mutable std::mutex mutex;
            std::ofstream stream;
            std::chrono::steady_clock::time_point started{};
            std::string type;
            std::string path;
            bool warned = false;
        };

        struct PendingSlider {
            double initial = 0.0;
            double latest = 0.0;
            std::chrono::steady_clock::time_point changed{};
        };

        DiagnosticSessionLogger() = default;
        void Begin(
            SessionSink& sink,
            const char* directory,
            const char* filePrefix,
            const char* sessionType,
            DiagnosticFields context) noexcept;
        void End(
            SessionSink& sink,
            std::string outcome,
            DiagnosticFields fields) noexcept;
        void Write(
            SessionSink& sink,
            const std::string& event,
            const std::string& source,
            const DiagnosticFields& fields) noexcept;
        static void RetainNewest(
            const std::string& directory,
            const std::string& activePath) noexcept;

        SessionSink menu_;
        SessionSink download_;
        std::mutex sliderMutex_;
        std::map<std::string, PendingSlider> pendingSliders_;
    };
}
