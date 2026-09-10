// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncUnityPlayer.hpp"
#include "UnityEngine/AudioSettings.hpp"
#include "UnityEngine/AudioClip.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "custom-types/shared/delegate.hpp"
#include <algorithm>
#include <cmath>

namespace BigScreen::AudioSync
{
struct UnityPlayer::Impl
{
    struct Slot
    {
        std::atomic<Audition::State*> current{nullptr};
        std::atomic<unsigned> readers{0};
        std::atomic<std::uint64_t> clipGeneration{0};
        void Render(ArrayW<float> data, std::uint64_t generation) noexcept
        {
            readers.fetch_add(1, std::memory_order_seq_cst);
            auto* state = current.load(std::memory_order_seq_cst);
            // Destroy is deferred by Unity. A callback belonging to a
            // retired streaming clip must never consume a NEW session's
            // samples after the menu/clip was recreated. Reader admission
            // still protects any old state already acquired before Stop.
            if(state && generation == clipGeneration.load(std::memory_order_seq_cst))
                state->Render(data.begin(), data.size() / 2, 2);
            else
                std::fill(data.begin(), data.end(), 0.0f);
            readers.fetch_sub(1, std::memory_order_seq_cst);
        }
    };
    std::shared_ptr<Slot> slot = std::make_shared<Slot>();
    std::shared_ptr<Audition::State> owner;
    std::vector<std::shared_ptr<Audition::State>> retired;
    UnityW<UnityEngine::GameObject> object;
    UnityW<UnityEngine::AudioClip> clip;
    UnityW<UnityEngine::AudioSource> source;
    int rate = 0;
    double songStart = 0;
    double songSpeed = 1;
    double dspStart = 0;
    bool clockActive = false;
};
UnityPlayer::UnityPlayer() : impl_(std::make_unique<Impl>()) {}
UnityPlayer::~UnityPlayer() = default;
void UnityPlayer::Tick()
{
    // seq_cst pairs the admission count with pointer exchange. If a new
    // callback starts after zero was observed, it can only see the NEW
    // pointer. An old callback must have entered before that exchange and
    // keeps this count nonzero until its last access has finished.
    if(impl_->slot->readers.load(std::memory_order_seq_cst) == 0)
        impl_->retired.clear();
}
void UnityPlayer::Stop()
{
    impl_->slot->current.store(nullptr, std::memory_order_seq_cst);
    if(impl_->owner)
        impl_->retired.push_back(std::move(impl_->owner));
    if(impl_->source)
        impl_->source->Stop();
    impl_->clockActive = false;
    Tick();
}
void UnityPlayer::Forget()
{
    Stop();
    impl_->slot->clipGeneration.fetch_add(1, std::memory_order_seq_cst);
    if(impl_->object)
        UnityEngine::Object::Destroy(impl_->object);
    if(impl_->clip)
        UnityEngine::Object::Destroy(impl_->clip);
    impl_->object = nullptr;
    impl_->source = nullptr;
    impl_->clip = nullptr;
    impl_->rate = 0;
    impl_->songStart = 0;
    impl_->songSpeed = 1;
    impl_->dspStart = 0;
}
void UnityPlayer::Play(std::shared_ptr<Audition::State> state, UnityEngine::AudioSource* mapChannel)
{
    Stop();
    if(impl_->rate != state->rate)
        Forget();
    if(!impl_->object)
    {
        impl_->object = UnityEngine::GameObject::New_ctor("Big Screen Audio Sync Audition");
        impl_->source = impl_->object->AddComponent<UnityEngine::AudioSource*>();
        impl_->source->set_playOnAwake(false);
        impl_->source->set_spatialBlend(0);
        impl_->source->set_loop(true);
        const auto slot = impl_->slot;
        const auto generation = slot->clipGeneration.load(std::memory_order_seq_cst);
        auto* callback = custom_types::MakeDelegate<UnityEngine::AudioClip_PCMReaderCallback*>(
            std::function<void(ArrayW<float>)>{[slot, generation](ArrayW<float> data)
                                               { slot->Render(data, generation); }});
        impl_->clip = UnityEngine::AudioClip::Create("Big Screen synchronization audio",
                                                     state->rate, 2, state->rate, true, callback);
        impl_->source->set_clip(impl_->clip);
        impl_->rate = state->rate;
    }
    // Use the same game mixer bus and gain as the map preview. This keeps
    // the user's music-volume preference effective; neither source is
    // routed through a separate Android player with unrelated latency.
    if(mapChannel)
    {
        impl_->source->set_outputAudioMixerGroup(mapChannel->get_outputAudioMixerGroup());
        impl_->source->set_volume(mapChannel->get_volume());
    }
    else
        impl_->source->set_volume(.5f);
    impl_->owner = std::move(state);
    impl_->owner->paused.store(false, std::memory_order_release);
    impl_->slot->current.store(impl_->owner.get(), std::memory_order_seq_cst);
    impl_->source->Play();
    impl_->songStart = impl_->owner->songStart;
    impl_->songSpeed = impl_->owner->speed;
    impl_->dspStart = UnityEngine::AudioSettings::get_dspTime();
    impl_->clockActive = true;
}

std::optional<double> UnityPlayer::SongTime() const
{
    if(!impl_->clockActive || !impl_->source || !impl_->owner || impl_->rate <= 0)
        return std::nullopt;
    if(!impl_->source->get_isPlaying())
        return std::nullopt;

    // The generated streaming clip is exactly one second long and loops so
    // Unity can continue requesting PCM without allocating a song-sized
    // AudioClip. AudioSource::timeSamples is the audible playhead within that
    // one-second clip; AudioSettings::dspTime tells us which loop it belongs
    // to. Reconstructing the nearest unwrapped position gives the video a
    // smooth per-frame clock. The former Audition::State::read clock advanced
    // only when Unity requested its next 600-800 ms PCM block, which made the
    // video present one sampled frame per callback and falsely report nearly
    // every intervening video frame as dropped.
    const auto sample = std::clamp(impl_->source->get_timeSamples(), 0, impl_->rate - 1);
    const double phaseSeconds = static_cast<double>(sample) / impl_->rate;
    const double expectedSeconds = std::max(
        0.0, UnityEngine::AudioSettings::get_dspTime() - impl_->dspStart);
    const double completedLoops = std::max(0.0, std::round(expectedSeconds - phaseSeconds));
    const double audibleSeconds = completedLoops + phaseSeconds;
    return impl_->songStart + audibleSeconds * impl_->songSpeed;
}
} // namespace BigScreen::AudioSync
