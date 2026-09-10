// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once
#include "BigScreen/AudioSyncCapture.hpp"
#include "BigScreen/AudioSyncReader.hpp"
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace BigScreen::AudioSync
{
/// Disposable mono audition audio, not a copy held in RAM. Files use the
/// original timeline: padding before the first usable PTS is intentional.
/// A lease pins an entry against storage cleanup until every analysis and
/// audition worker has released it. Nothing in this class runs on Unity's
/// audio callback or UI thread.
class PcmLease final
{
  public:
    const std::filesystem::path path;
    const double duration;
    ~PcmLease();
    PcmLease(const PcmLease&) = delete;
    PcmLease& operator=(const PcmLease&) = delete;

  private:
    friend class PcmCache;
    PcmLease(std::filesystem::path file, double seconds) : path(std::move(file)), duration(seconds)
    {
    }
};
class PcmCache final
{
  public:
    static constexpr std::uint64_t Budget = 128ull * 1024 * 1024;
    static constexpr int Rate = 16000;
    static std::shared_ptr<const PcmLease> Prepare(const ReadRequest& source,
                                                   const std::filesystem::path& root,
                                                   const std::function<void(double)>& progress,
                                                   std::string& error);
    static std::uint64_t ClearUnused(const std::filesystem::path& root);
    static std::vector<std::filesystem::path> Unused(const std::filesystem::path& root);
    // Cleanup rechecks the lease under the same writer lock as Prepare;
    // a scan-time result alone never authorizes deleting live audition PCM.
    static bool RemoveUnused(const std::filesystem::path& path);
    static std::shared_ptr<const PcmLease>
    Capture(const std::shared_ptr<CaptureBuffer>& input, const std::string& identity,
            const std::filesystem::path& root, const CancelCheck& cancelled,
            const std::function<void(double)>& progress, std::string& error);
};
} // namespace BigScreen::AudioSync
