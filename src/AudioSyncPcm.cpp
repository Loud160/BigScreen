// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncPcm.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unordered_map>
extern "C"
{
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace BigScreen::AudioSync
{
namespace
{
std::mutex CacheMutex;
std::unordered_map<std::string, std::weak_ptr<const PcmLease>> Pins;
// All writes/evictions are serialized, never playback reads. This is
// worker-only and prevents two preparations overcommitting the quota.
std::mutex WriterMutex;
std::string Key(const ReadRequest& source)
{
    std::ostringstream identity;
    identity << SourceFingerprint(source.path) << "|pcm-v1|" << std::setprecision(17)
             << source.sourceToFinalShiftSeconds;
    std::uint64_t hash = 14695981039346656037ull;
    for(unsigned char c : identity.str())
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream name;
    name << std::hex << hash << ".wav";
    return name.str();
}
void Word(std::ostream& out, std::uint32_t value, int bytes)
{
    for(int n = 0; n < bytes; ++n)
        out.put(static_cast<char>(value >> (8 * n)));
}
void Header(std::ostream& out, std::uint32_t samples)
{
    out.seekp(0);
    out.write("RIFF", 4);
    Word(out, 36 + samples * 4, 4);
    out.write("WAVEfmt ", 8);
    Word(out, 16, 4);
    Word(out, 3, 2);
    Word(out, 1, 2);
    Word(out, PcmCache::Rate, 4);
    Word(out, PcmCache::Rate * 4, 4);
    Word(out, 4, 2);
    Word(out, 32, 2);
    out.write("data", 4);
    Word(out, samples * 4, 4);
}
struct Entry
{
    std::filesystem::path path;
    std::uint64_t size;
    std::filesystem::file_time_type stamp;
};
std::vector<Entry> Entries(const std::filesystem::path& root)
{
    std::vector<Entry> result;
    std::error_code ec;
    for(std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->path().extension() != ".wav" || !it->is_regular_file(ec))
            continue;
        const auto size = it->file_size(ec);
        if(ec)
            break;
        const auto stamp = it->last_write_time(ec);
        if(ec)
            break;
        result.push_back({it->path(), size, stamp});
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.stamp < b.stamp; });
    return result;
}
bool Pinned(const std::filesystem::path& path)
{
    std::scoped_lock lock(CacheMutex);
    const auto it = Pins.find(path.string());
    return it != Pins.end() && !it->second.expired();
}
void RecoverInterruptedWrites(const std::filesystem::path& root)
{
    // Caller owns WriterMutex: no preparation/capture can still be
    // writing a part file. Recover only our generated cache names,
    // never arbitrary files in this directory. Process termination
    // skips RAII cleanup; without this pass those partial tracks
    // would occupy space outside the completed-file quota forever.
    std::error_code error;
    for(std::filesystem::directory_iterator it(root, error), end; !error && it != end;
        it.increment(error))
    {
        auto name = it->path().filename().string();
        if(!name.ends_with(".wav.part"))
            continue;
        name.resize(name.size() - 9);
        if(name.starts_with("capture-"))
            name.erase(0, 8);
        if(name.empty() || name.size() > 20 ||
           !std::all_of(name.begin(), name.end(), [](unsigned char c)
                        { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            continue;
        if(it->is_regular_file(error))
            std::filesystem::remove(it->path(), error);
    }
    std::scoped_lock pins(CacheMutex);
    std::erase_if(Pins, [](const auto& item) { return item.second.expired(); });
}
} // namespace
PcmLease::~PcmLease() = default;
std::shared_ptr<const PcmLease>
PcmCache::Capture(const std::shared_ptr<CaptureBuffer>& input, const std::string& identity,
                  const std::filesystem::path& root, const CancelCheck& cancelled,
                  const std::function<void(double)>& progress, std::string& error)
{
    // Only this worker touches disk. Unity supplies bounded chunks after
    // accepting becomes true, so no samples are lost during quota checks.
    struct StopProducer
    {
        CaptureBuffer& input;
        std::string& error;
        ~StopProducer()
        {
            if(error.empty() && input.failure.load())
                error = input.failure.load() == 1
                            ? "The game's song audio has unsupported sample metadata."
                            : "The game could not read this resident audio clip.";
            input.stop.store(true);
        }
    } stop{*input, error};
    const auto cancelledNow = [&] { return (cancelled && cancelled()) || input->stop.load(); };
    const auto started = std::chrono::steady_clock::now();
    while(!input->configured.load(std::memory_order_acquire))
    {
        if(cancelledNow())
            return {};
        if(std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
        {
            error = "The game did not provide this song's audio clip within 30 seconds.";
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::unique_lock writer(WriterMutex, std::defer_lock);
    while(!writer.try_lock())
    {
        if(cancelledNow())
            return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if(ec)
    {
        error = "Cannot create the audio sync cache: " + ec.message();
        return {};
    }
    RecoverInterruptedWrites(root);
    std::uint64_t hash = 14695981039346656037ull;
    const auto key = identity + "|unity-mono-v1|" + std::to_string(input->rate) + "|" +
                     std::to_string(input->maximum);
    for(unsigned char c : key)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    const auto path = root / ("capture-" + std::to_string(hash) + ".wav");
    {
        std::scoped_lock lock(CacheMutex);
        if(auto live = Pins[path.string()].lock())
            return live;
    }
    const auto expected = input->maximum * 4 + 44;
    if(expected > Budget || input->maximum == 0)
    {
        error = "This song exceeds the 128 MB synchronization cache budget.";
        return {};
    }
    const auto existing = ProbeAudio(path, cancelledNow);
    if(std::filesystem::file_size(path, ec) == expected && !ec && existing.available &&
       existing.codec == "pcm_f32le" && existing.sampleRate == Rate && existing.channels == 1)
    {
        auto lease =
            std::shared_ptr<const PcmLease>(new PcmLease(path, input->maximum / double(Rate)));
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
        std::scoped_lock lock(CacheMutex);
        Pins[path.string()] = lease;
        return lease;
    }
    std::filesystem::remove(path, ec);
    auto entries = Entries(root);
    std::uint64_t used = 0;
    for(const auto& entry : entries)
        used += entry.size;
    for(const auto& entry : entries)
        if(used + expected > Budget && !Pinned(entry.path))
            if(std::filesystem::remove(entry.path, ec))
                used -= entry.size;
    const auto space = std::filesystem::space(root, ec);
    if(used + expected > Budget || ec || space.available < expected + 64 * 1024 * 1024)
    {
        error = "Not enough available audio cache space to capture this song.";
        return {};
    }
    const auto temporary = std::filesystem::path(path.string() + ".part");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{temporary};
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    Header(out, 0);
    if(!out)
    {
        error = "Cannot open the temporary song audio file.";
        return {};
    }
    input->accepting.store(true, std::memory_order_release);
    std::uint64_t count = 0;
    std::array<float, 4096> chunk{};
    std::array<float, 8192> converted{};
    SwrContext* resampler =
        swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_FLT, Rate, AV_CH_LAYOUT_MONO,
                           AV_SAMPLE_FMT_FLT, input->rate, 0, nullptr);
    struct ReleaseResampler
    {
        SwrContext*& pointer;
        ~ReleaseResampler() { swr_free(&pointer); }
    } release{resampler};
    if(!resampler || swr_init(resampler) < 0)
    {
        error = "Cannot prepare captured-audio resampling.";
        return {};
    }
    const auto convert = [&](const float* samples, int length)
    {
        const auto* in = reinterpret_cast<const std::uint8_t*>(samples);
        auto* output = reinterpret_cast<std::uint8_t*>(converted.data());
        const int amount =
            swr_convert(resampler, &output, converted.size(), samples ? &in : nullptr, length);
        if(amount < 0)
        {
            error = "Captured audio could not be resampled.";
            return false;
        }
        const auto writeCount = std::min<std::uint64_t>(amount, input->maximum - count);
        out.write(reinterpret_cast<const char*>(converted.data()), writeCount * 4);
        count += writeCount;
        return static_cast<bool>(out);
    };
    auto lastData = std::chrono::steady_clock::now();
    while(!cancelledNow())
    {
        const auto size = input->Pop(chunk);
        if(size)
        {
            if(!convert(chunk.data(), size))
            {
                if(error.empty())
                    error = "Could not save captured song audio.";
                return {};
            }
            lastData = std::chrono::steady_clock::now();
            if(progress)
                progress(count / double(Rate));
        }
        else if(input->done.load(std::memory_order_acquire))
            break;
        else if(std::chrono::steady_clock::now() - lastData > std::chrono::seconds(15))
        {
            error = "Song audio capture stopped receiving samples. Cancel and reopen Audio Sync.";
            return {};
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(input->overflow.load())
    {
        error =
            "Audio capture could not keep up with storage. Try again without other recording or "
            "download tasks.";
        return {};
    }
    if(cancelledNow())
        return {};
    if(!convert(nullptr, 0))
    {
        if(error.empty())
            error = "Could not finalize captured song audio.";
        return {};
    }
    if(count != input->maximum)
    {
        error = "The game supplied incomplete song audio; no partial capture was accepted.";
        return {};
    }
    Header(out, static_cast<std::uint32_t>(count));
    out.flush();
    if(!out)
    {
        error = "Could not finalize captured audio.";
        return {};
    }
    out.close();
    if(cancelledNow())
        return {};
    std::filesystem::rename(temporary, path, ec);
    if(ec)
    {
        error = "Could not publish captured audio: " + ec.message();
        return {};
    }
    auto lease = std::shared_ptr<const PcmLease>(new PcmLease(path, count / double(Rate)));
    std::scoped_lock lock(CacheMutex);
    Pins[path.string()] = lease;
    return lease;
}
std::vector<std::filesystem::path> PcmCache::Unused(const std::filesystem::path& root)
{
    // Storage scans must not wait for a streaming song capture to finish.
    // Conservatively offer no entries while a writer owns quota/eviction.
    std::unique_lock writer(WriterMutex, std::try_to_lock);
    if(!writer.owns_lock())
        return {};
    RecoverInterruptedWrites(root);
    std::vector<std::filesystem::path> paths;
    for(const auto& entry : Entries(root))
        if(!Pinned(entry.path))
            paths.push_back(entry.path);
    return paths;
}
bool PcmCache::RemoveUnused(const std::filesystem::path& path)
{
    std::unique_lock writer(WriterMutex, std::try_to_lock);
    if(!writer.owns_lock())
        return false;
    if(Pinned(path) || path.extension() != ".wav")
        return false;
    std::error_code error;
    return std::filesystem::remove(path, error) && !error;
}
std::uint64_t PcmCache::ClearUnused(const std::filesystem::path& root)
{
    std::unique_lock writer(WriterMutex, std::try_to_lock);
    if(!writer.owns_lock())
        return 0;
    RecoverInterruptedWrites(root);
    std::uint64_t removed = 0;
    for(const auto& entry : Entries(root))
        if(!Pinned(entry.path))
        {
            std::error_code ec;
            if(std::filesystem::remove(entry.path, ec))
                removed += entry.size;
        }
    return removed;
}
std::shared_ptr<const PcmLease> PcmCache::Prepare(const ReadRequest& source,
                                                  const std::filesystem::path& root,
                                                  const std::function<void(double)>& progress,
                                                  std::string& error)
{
    // try_lock makes contention cancellable. Never join a blocked decoder
    // on the UI thread; Operation retains this worker through cleanup.
    std::unique_lock writer(WriterMutex, std::defer_lock);
    while(!writer.try_lock())
    {
        if(source.cancelled && source.cancelled())
            return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if(ec)
    {
        error = "Cannot create audio cache: " + ec.message();
        return {};
    }
    RecoverInterruptedWrites(root);
    const auto path = root / Key(source);
    {
        std::scoped_lock lock(CacheMutex);
        if(auto live = Pins[path.string()].lock())
            return live;
    }
    if(std::filesystem::is_regular_file(path, ec))
    {
        const auto info = ProbeAudio(path, source.cancelled);
        if(info.available && info.codec == "pcm_f32le" && info.sampleRate == Rate &&
           info.channels == 1)
        {
            auto lease = std::shared_ptr<const PcmLease>(new PcmLease(path, info.durationSeconds));
            std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(),
                                             ec);
            std::scoped_lock lock(CacheMutex);
            Pins[path.string()] = lease;
            return lease;
        }
        std::filesystem::remove(path, ec);
    }
    const auto info = ProbeAudio(source.path, source.cancelled);
    if(!info.available)
    {
        error = info.error;
        return {};
    }
    const double end =
        info.durationSeconds + info.streamStartSeconds + source.sourceToFinalShiftSeconds;
    if(!std::isfinite(end) || end <= 0 || end * Rate * 4 + 44 > Budget)
    {
        error = "The audio duration is unknown or exceeds the 128 MB synchronization cache limit.";
        return {};
    }
    const auto needed = static_cast<std::uint64_t>(std::ceil(end) * Rate * 4) + 44;
    auto entries = Entries(root);
    std::uint64_t used = 0;
    for(const auto& e : entries)
        used += e.size;
    for(const auto& e : entries)
        if(used + needed > Budget && !Pinned(e.path))
        {
            if(std::filesystem::remove(e.path, ec))
                used -= e.size;
        }
    if(used + needed > Budget)
    {
        error =
            "These tracks need more than the 128 MB audio cache budget. Close other audio work and "
            "try shorter tracks.";
        return {};
    }
    const auto space = std::filesystem::space(root, ec);
    if(ec || space.available < needed + 64 * 1024 * 1024)
    {
        error = "Not enough free storage to prepare synchronization audio.";
        return {};
    }
    const auto temporary = std::filesystem::path(path.string() + ".part");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{temporary};
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    Header(out, 0);
    std::uint64_t count = 0;
    std::array<float, 4096> silence{};
    auto request = source;
    request.outputRate = Rate;
    const auto read = ReadAudio(
        request,
        [&](std::span<const float> samples, double time)
        {
            if(source.cancelled && source.cancelled())
                return false;
            const auto position = static_cast<std::int64_t>(std::llround(time * Rate));
            if(position < 0 ||
               static_cast<std::uint64_t>(position) + samples.size() > (Budget - used - 44) / 4)
            {
                error = "Decoded audio exceeds its storage budget or has invalid timestamps.";
                return false;
            }
            if(position + 2 < static_cast<std::int64_t>(count))
            {
                error = "Audio has overlapping timestamps; it cannot be synchronized reliably.";
                return false;
            }
            // Resampler timestamp rounding may repeat at most two sample
            // positions. Trim that overlap rather than appending it: doing so
            // at every packet would accumulate drift across a long song.
            if(position < static_cast<std::int64_t>(count))
                samples = samples.subspan(std::min<std::size_t>(samples.size(), count - position));
            while(count < static_cast<std::uint64_t>(position))
            {
                const auto zeros = std::min<std::uint64_t>(silence.size(), position - count);
                out.write(reinterpret_cast<const char*>(silence.data()), zeros * 4);
                count += zeros;
            }
            // Both supported targets and host fixtures are little endian.
            // RIFF metadata above is explicit-endian; PCM float payload follows
            // IEEE-754 little endian, checked by the compile-time assertion.
            static_assert(std::endian::native == std::endian::little);
            out.write(reinterpret_cast<const char*>(samples.data()), samples.size_bytes());
            count += samples.size();
            if(!out)
            {
                error = "Could not write prepared audio (storage full or disconnected).";
                return false;
            }
            if(progress)
                progress(time);
            return true;
        });
    if(read.cancelled || (source.cancelled && source.cancelled()))
        return {};
    if(!read.error.empty())
        error = read.error;
    if(!error.empty() || !count)
        return {};
    Header(out, static_cast<std::uint32_t>(count));
    out.flush();
    if(!out)
    {
        error = "Could not finalize prepared audio.";
        return {};
    }
    out.close();
    std::filesystem::rename(temporary, path, ec);
    if(ec)
    {
        error = "Could not publish prepared audio: " + ec.message();
        return {};
    }
    auto lease = std::shared_ptr<const PcmLease>(new PcmLease(path, count / double(Rate)));
    std::scoped_lock lock(CacheMutex);
    Pins[path.string()] = lease;
    return lease;
}
} // namespace BigScreen::AudioSync
