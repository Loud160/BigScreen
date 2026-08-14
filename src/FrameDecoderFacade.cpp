#include "BigScreen/FrameDecoder.hpp"

#include "BigScreen/Settings.hpp"

#include <utility>

namespace BigScreen {
    namespace {
        template<typename Value, typename Getter>
        Value ReadBackend(
            const std::unique_ptr<FrameDecoderBackend>& backend,
            Value fallback,
            Getter getter)
        {
            return backend ? getter(*backend) : fallback;
        }
    }

    FrameDecoder::~FrameDecoder()
    {
        Close();
    }

    bool FrameDecoder::Open(
        const std::filesystem::path& videoPath,
        int maximumOutputHeight,
        std::string& error)
    {
        Close();
        // Construct only the selected implementation. Both FFmpeg runtimes are
        // loaded under unique SONAMEs, but an inactive backend owns no worker,
        // codec context, frame pool, or conversion state.
        backend_ = Settings::Instance().UseFfmpeg9()
            ? CreateFrameDecoder9Backend()
            : CreateFrameDecoder44Backend();
        if(!backend_)
        {
            error = "The selected FFmpeg decoder backend is unavailable.";
            return false;
        }
        if(backend_->Open(videoPath, maximumOutputHeight, error))
            return true;
        backend_->Close();
        backend_.reset();
        return false;
    }

    void FrameDecoder::Close()
    {
        if(!backend_)
            return;
        backend_->Close();
        backend_.reset();
    }

    void FrameDecoder::Request(double mediaSeconds)
    {
        if(backend_)
            backend_->Request(mediaSeconds);
    }

    bool FrameDecoder::TryTake(VideoFrame& destination)
    {
        return backend_ && backend_->TryTake(destination);
    }

    void FrameDecoder::Recycle(VideoFrame&& frame)
    {
        if(backend_)
            backend_->Recycle(std::move(frame));
    }

    std::optional<std::string> FrameDecoder::TakeError()
    {
        return backend_ ? backend_->TakeError() : std::nullopt;
    }

    bool FrameDecoder::IsOpen() const
    {
        return backend_ && backend_->IsOpen();
    }

    int FrameDecoder::Width() const
    {
        return ReadBackend<int>(backend_, 0, [](const auto& value) {
            return value.Width();
        });
    }

    int FrameDecoder::Height() const
    {
        return ReadBackend<int>(backend_, 0, [](const auto& value) {
            return value.Height();
        });
    }

    int FrameDecoder::SourceWidth() const
    {
        return ReadBackend<int>(backend_, 0, [](const auto& value) {
            return value.SourceWidth();
        });
    }

    int FrameDecoder::SourceHeight() const
    {
        return ReadBackend<int>(backend_, 0, [](const auto& value) {
            return value.SourceHeight();
        });
    }

    double FrameDecoder::SourceFramesPerSecond() const
    {
        return ReadBackend<double>(backend_, 0.0, [](const auto& value) {
            return value.SourceFramesPerSecond();
        });
    }

    double FrameDecoder::AverageDecodeMilliseconds() const
    {
        return ReadBackend<double>(backend_, 0.0, [](const auto& value) {
            return value.AverageDecodeMilliseconds();
        });
    }

    double FrameDecoder::PeakDecodeMilliseconds() const
    {
        return ReadBackend<double>(backend_, 0.0, [](const auto& value) {
            return value.PeakDecodeMilliseconds();
        });
    }

    void FrameDecoder::ResetPeakDecodeMilliseconds()
    {
        if(backend_)
            backend_->ResetPeakDecodeMilliseconds();
    }

    double FrameDecoder::WorkerCpuMilliseconds() const
    {
        return ReadBackend<double>(backend_, 0.0, [](const auto& value) {
            return value.WorkerCpuMilliseconds();
        });
    }

    double FrameDecoder::DurationSeconds() const
    {
        return ReadBackend<double>(backend_, 0.0, [](const auto& value) {
            return value.DurationSeconds();
        });
    }

    std::uint64_t FrameDecoder::BufferAllocations() const
    {
        return ReadBackend<std::uint64_t>(backend_, 0, [](const auto& value) {
            return value.BufferAllocations();
        });
    }

    const char* FrameDecoder::RuntimeVersion() const
    {
        return backend_ ? backend_->RuntimeVersion() : "not loaded";
    }
}
