// SPDX-License-Identifier: GPL-3.0-only
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
#pragma once
#include "BigScreen/AudioSyncCapture.hpp"
#include "UnityEngine/AudioClip.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"
#include <memory>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
DECLARE_CLASS_CODEGEN(BigScreen, AudioSyncCaptureTap, UnityEngine::MonoBehaviour)
{
    DECLARE_INSTANCE_FIELD(int64_t, token);
    DECLARE_INSTANCE_FIELD(ArrayW<float>, chunk);
    DECLARE_INSTANCE_FIELD(UnityEngine::AudioClip*, retainedClip);
    DECLARE_INSTANCE_METHOD(void, OnAudioFilterRead, ArrayW<float> data, int channels);
};
#pragma clang diagnostic pop

namespace BigScreen::AudioSync
{
/// Main-thread Unity bridge. A native worker consumes CaptureBuffer; it
/// never calls GetData, AudioSource or any other managed API.
class UnityCapture final
{
  public:
    UnityCapture();
    ~UnityCapture();
    std::shared_ptr<CaptureBuffer> Begin();
    void Tick(UnityEngine::AudioClip* availableClip);
    void Stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace BigScreen::AudioSync
