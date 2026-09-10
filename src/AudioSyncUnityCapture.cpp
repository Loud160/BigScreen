// SPDX-License-Identifier: GPL-3.0-only
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// SPDX-FileCopyrightText: © 2026 Loud160 and the Big Screen contributors
#include "BigScreen/AudioSyncUnityCapture.hpp"
#include "UnityEngine/AudioClipLoadType.hpp"
#include "UnityEngine/AudioDataLoadState.hpp"
#include "UnityEngine/AudioSettings.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "main.hpp"
#include <vector>

DEFINE_TYPE(BigScreen, AudioSyncCaptureTap);

namespace
{
// One private capture owner exists. Token identity additionally rejects a
// late callback from a destroyed source after a newer capture has begun.
// Reader admission and retirement use the same protocol as UnityPlayer.
struct CaptureSlot
{
    std::atomic<BigScreen::AudioSync::CaptureBuffer*> current{nullptr};
    std::atomic<unsigned> readers{0};
    std::atomic<int64_t> token{0};
} Slot;
} // namespace
void BigScreen::AudioSyncCaptureTap::OnAudioFilterRead(ArrayW<float> data, int channels)
{
    Slot.readers.fetch_add(1, std::memory_order_seq_cst);
    auto* buffer = Slot.current.load(std::memory_order_seq_cst);
    // Acquire the pointer BEFORE checking its generation. Checking the token
    // first would allow Stop/Begin between the two loads, pairing an old clip's
    // samples with the new buffer. Reader admission retains an acquired old
    // buffer until this callback leaves; its stop flag rejects late samples.
    if(buffer && token == Slot.token.load(std::memory_order_seq_cst))
        buffer->Push({data.begin(), data.size()}, channels);
    // Never set AudioSource.mute/volume=0: Unity may then virtualize it and
    // omit sample delivery. The tap is the last stage on our private object
    // and silences EVERY sample, even on cancellation or stale callbacks.
    std::fill(data.begin(), data.end(), 0.0f);
    Slot.readers.fetch_sub(1, std::memory_order_seq_cst);
}
namespace BigScreen::AudioSync
{
struct UnityCapture::Impl
{
    std::shared_ptr<CaptureBuffer> buffer;
    std::vector<std::shared_ptr<CaptureBuffer>> retired;
    UnityW<UnityEngine::GameObject> object;
    UnityW<UnityEngine::AudioSource> source;
    UnityW<AudioSyncCaptureTap> tap;
    bool direct = false, playing = false;
    int frame = 0, frames = 0, channels = 0;
};
UnityCapture::UnityCapture() : impl_(std::make_unique<Impl>()) {}
UnityCapture::~UnityCapture() = default;
std::shared_ptr<CaptureBuffer> UnityCapture::Begin()
{
    Stop();
    impl_->buffer = std::make_shared<CaptureBuffer>();
    return impl_->buffer;
}
void UnityCapture::Stop()
{
    Slot.current.store(nullptr, std::memory_order_seq_cst);
    if(impl_->buffer)
    {
        impl_->buffer->stop.store(true);
        impl_->retired.push_back(std::move(impl_->buffer));
    }
    if(impl_->source)
        impl_->source->Stop();
    if(impl_->object)
        UnityEngine::Object::Destroy(impl_->object);
    impl_->source = nullptr;
    impl_->tap = nullptr;
    impl_->object = nullptr;
    impl_->playing = false;
    impl_->frame = 0;
    if(Slot.readers.load(std::memory_order_seq_cst) == 0)
        impl_->retired.clear();
}
void UnityCapture::Tick(UnityEngine::AudioClip* clip)
{
    auto& s = *impl_;
    if(Slot.readers.load(std::memory_order_seq_cst) == 0)
        s.retired.clear();
    if(!s.buffer)
        return;
    if(s.buffer->stop.load())
    {
        Stop();
        return;
    }
    if(!s.tap)
    {
        if(!UnityW<UnityEngine::AudioClip>::isAlive(clip))
            return;
        if(clip->get_loadState() != UnityEngine::AudioDataLoadState::Loaded)
            return;
        s.frames = clip->get_samples();
        s.channels = clip->get_channels();
        const auto rate = clip->get_frequency();
        if(s.frames <= 0 || s.channels < 1 || s.channels > 8 || rate < 16000 || rate > 192000)
        {
            s.buffer->failure.store(1);
            s.buffer->stop.store(true);
            return;
        }
        s.direct = clip->get_loadType() == UnityEngine::AudioClipLoadType::DecompressOnLoad;
        s.object = UnityEngine::GameObject::New_ctor("Big Screen private audio capture");
        s.tap = s.object->AddComponent<AudioSyncCaptureTap*>();
        s.tap->retainedClip = clip; // Managed field keeps the clip rooted.
        s.tap->token = Slot.token.fetch_add(1, std::memory_order_seq_cst) + 1;
        s.buffer->Configure(s.direct ? rate
                                     : UnityEngine::AudioSettings::get_outputSampleRate(),
                            s.frames / double(rate));
        if(s.direct)
            s.tap->chunk = ArrayW<float>(2048 * s.channels);
        else
        {
            s.source = s.object->AddComponent<UnityEngine::AudioSource*>();
            s.source->set_playOnAwake(false);
            s.source->set_clip(clip);
            s.source->set_loop(false);
            s.source->set_spatialBlend(0);
            s.source->set_pitch(1);
            s.source->set_volume(1);
            s.source->set_priority(0);
            Slot.current.store(s.buffer.get(), std::memory_order_seq_cst);
        }
        BigScreenLogger.info(
            "Audio Sync map acquisition: {} rate={} channels={} duration={:.3f}s",
            s.direct ? "bounded resident GetData" : "silent sequential capture", rate,
            s.channels, s.frames / double(rate));
    }
    if(!s.buffer->accepting.load() || s.buffer->done.load())
        return;
    if(!s.direct)
    {
        if(!s.playing)
        {
            s.source->Play();
            s.playing = true;
        }
        return;
    }
    // One 2048-frame copy per rendered frame, never a whole AudioClip.
    // If disk cannot keep up, postpone GetData without losing source time.
    if(s.buffer->write.load() - s.buffer->read.load() > CaptureBuffer::Capacity - 4096)
        return;
    const int count = std::min(2048, s.frames - s.frame);
    if(count <= 0)
    {
        s.buffer->Finish();
        return;
    }
    if(!s.tap->retainedClip->GetData(s.tap->chunk, s.frame))
    {
        s.buffer->failure.store(2);
        s.buffer->stop.store(true);
        return;
    }
    s.buffer->Push({s.tap->chunk.begin(), static_cast<std::size_t>(count * s.channels)},
                   s.channels);
    s.frame += count;
    if(s.frame >= s.frames)
        s.buffer->Finish();
}
} // namespace BigScreen::AudioSync
