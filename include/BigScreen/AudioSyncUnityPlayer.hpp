// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once
#include "BigScreen/AudioSyncAudition.hpp"
#include <optional>
namespace UnityEngine
{
class AudioSource;
}
namespace BigScreen::AudioSync
{
/// Unity owns one streaming stereo output. Its delegate captures a native
/// slot, not a menu pointer. Stopping/replacing a session retires old native
/// state without waiting for an audio callback on the main thread.
class UnityPlayer final
{
  public:
    UnityPlayer();
    ~UnityPlayer();
    void Play(std::shared_ptr<Audition::State> state, UnityEngine::AudioSource* mapChannel);
    void Stop();
    void Forget();
    void Tick();
    /// Returns the audible map-clock position represented by Unity's active
    /// streaming AudioSource. This deliberately does not use the producer or
    /// PCM-callback counters: Unity fills streamed clips in large blocks, so
    /// those counters describe buffering rather than the continuously moving
    /// playback head.
    std::optional<double> SongTime() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace BigScreen::AudioSync
