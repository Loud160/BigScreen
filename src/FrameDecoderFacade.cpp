// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/FrameDecoder.hpp"

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/Settings.hpp"
#include "main.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <jni.h>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <utility>

// Scotland2 captures the process JavaVM from the JNIEnv passed to its preload
// entry point, before any early mod can initialize. Big Screen already links
// against libsl2, so this is the authoritative in-process VM pointer. Android
// deliberately hides libart's JNI_GetCreatedJavaVMs from the app linker
// namespace on current Quest firmware; probing libart is therefore not a
// valid substitute even though the symbol exists in the file on disk.
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

        void* ResolveJavaVm(std::string& error)
        {
            if(!modloader_jvm)
            {
                error = "Scotland2 did not capture the Android Java VM";
                return nullptr;
            }
            return modloader_jvm;
        }

        /// Owns decoder backends whose worker did not stop inside the short UI
        /// budget. The queue is intentionally process-lifetime: Quest never
        /// unloads a native mod from a running Beat Saber process, and avoiding
        /// static-destruction ordering prevents a late PlaybackSession teardown
        /// from targeting an already-destroyed reaper.
        class DecoderRetirementQueue final {
        public:
            static DecoderRetirementQueue& Instance()
            {
                static auto* queue = new DecoderRetirementQueue();
                return *queue;
            }

            void Retire(std::unique_ptr<FrameDecoderBackend> backend)
            {
                if(!backend)
                    return;
                {
                    std::scoped_lock lock(mutex_);
                    retired_.push_back(std::move(backend));
                }
                wake_.notify_one();
            }

        private:
            DecoderRetirementQueue()
            {
                std::thread([this]() {
                    pthread_setname_np(pthread_self(), "BigScreenReap");
                    while(true)
                    {
                        std::unique_ptr<FrameDecoderBackend> backend;
                        {
                            std::unique_lock lock(mutex_);
                            wake_.wait(lock, [this]() {
                                return !retired_.empty();
                            });
                            backend = std::move(retired_.front());
                            retired_.pop_front();
                        }
                        PaperLogger.info(
                            "Background decoder retirement started");
                        backend->Close();
                        backend.reset();
                        PaperLogger.info(
                            "Background decoder retirement finished");
                    }
                }).detach();
            }

            std::mutex mutex_;
            std::condition_variable wake_;
            std::deque<std::unique_ptr<FrameDecoderBackend>> retired_;
        };
    }

    FrameDecoder::~FrameDecoder()
    {
        Close();
    }

    bool FrameDecoder::Open(
        const std::filesystem::path& videoPath,
        int maximumOutputHeight,
        const FrameVisualEffects& visualEffects,
        std::string& error)
    {
        const auto openStarted = std::chrono::steady_clock::now();
        openMilliseconds_ = 0.0;
        const auto finishOpenMeasurement = [this, openStarted](bool success)
        {
            openMilliseconds_ = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - openStarted).count();
            PaperLogger.info(
                "Decoder startup {} in {:.2f} ms",
                success ? "completed" : "failed",
                openMilliseconds_);
        };
        PaperLogger.info(
            "Decoder open started for '{}'",
            videoPath.filename().string());
        Close();
        videoPath_ = videoPath;
        maximumOutputHeight_ = maximumOutputHeight;
        visualEffects_ = visualEffects;
        lastRequestedSeconds_ = 0.0;
        useFfmpeg9_ = Settings::Instance().UseFfmpeg9();
        hardwareRequested_ = Settings::Instance().HardwareDecodingEnabled();
        hardwareFallbackAttempted_ = false;
        accumulatedWorkerCpuMilliseconds_ = 0.0;
        accumulatedBufferAllocations_ = 0;
        retainedPeakDecodeMilliseconds_ = 0.0;

        std::string javaVmError;
        javaVm_ = hardwareRequested_ ? ResolveJavaVm(javaVmError) : nullptr;
        if(hardwareRequested_ && !javaVm_)
        {
            PaperLogger.warn(
                "Hardware video decoding is unavailable; using software: {}",
                javaVmError);
            ErrorManager::Instance().RecordError(
                "Starting hardware video decoding",
                javaVmError + "; software playback remained available");
        }

        // Construct only the selected implementation. Both FFmpeg runtimes are
        // loaded under unique SONAMEs, but an inactive backend owns no worker,
        // codec context, frame pool, or conversion state.
        backend_ = CreateSelectedBackend();
        if(!backend_)
        {
            error = "The selected FFmpeg decoder backend is unavailable.";
            finishOpenMeasurement(false);
            return false;
        }
        // A missing JVM has already been reported above. Pass an effective
        // software request so the backend does not report the same fallback.
        const bool attemptHardware = hardwareRequested_ && javaVm_;
        if(backend_->Open(
               videoPath,
               maximumOutputHeight,
               attemptHardware,
               javaVm_,
               visualEffects_,
               error))
        {
            const auto fallbackReason = backend_->HardwareFallbackReason();
            if(attemptHardware && !backend_->UsingHardwareDecoder())
            {
                hardwareFallbackAttempted_ = true;
                const std::string reason = fallbackReason.empty()
                    ? "MediaCodec was unavailable for this file"
                    : fallbackReason;
                PaperLogger.warn(
                    "Hardware video decoding did not start; using software: {}",
                    reason);
                ErrorManager::Instance().RecordError(
                    "Falling back from hardware video decoding",
                    reason);
            }
            else
            {
                PaperLogger.info(
                    "Opened {} video decoder with FFmpeg {}",
                    DecodeMethodName(),
                    backend_->RuntimeVersion());
            }
            finishOpenMeasurement(true);
            return true;
        }
        CloseAndRetainBackendMetrics();
        PaperLogger.error(
            "Decoder open failed for '{}': {}",
            videoPath.filename().string(),
            error);
        finishOpenMeasurement(false);
        return false;
    }

    void FrameDecoder::Close()
    {
        CloseAndRetainBackendMetrics();
    }

    void FrameDecoder::Request(double mediaSeconds)
    {
        if(backend_)
        {
            lastRequestedSeconds_ = std::max(0.0, mediaSeconds);
            backend_->Request(mediaSeconds);
        }
    }

    void FrameDecoder::UpdateVisualEffects(
        const FrameVisualEffects& visualEffects)
    {
        visualEffects_ = visualEffects;
        if(backend_)
            backend_->UpdateVisualEffects(visualEffects);
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
        if(!backend_)
            return std::nullopt;
        auto error = backend_->TakeError();
        if(!error || !backend_->UsingHardwareDecoder() ||
           hardwareFallbackAttempted_)
            return error;

        if(!backend_->SoftwareFallbackAllowed())
        {
            hardwareFallbackAttempted_ = true;
            const auto reason = backend_->SoftwareFallbackBlockedReason();
            PaperLogger.error(
                "Hardware video decoding stopped and software fallback is prohibited: {}",
                reason);
            ErrorManager::Instance().RecordError(
                "Stopping video after hardware decoder failure",
                *error + "; " + reason);
            return *error + "; " + reason;
        }

        std::string recoveryError;
        if(ReopenWithSoftwareAfterHardwareFailure(*error, recoveryError))
            return std::nullopt;
        return *error + "; software fallback also failed: " + recoveryError;
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
        return std::max(
            retainedPeakDecodeMilliseconds_,
            ReadBackend<double>(backend_, 0.0, [](const auto& value) {
                return value.PeakDecodeMilliseconds();
            }));
    }

    void FrameDecoder::ResetPeakDecodeMilliseconds()
    {
        retainedPeakDecodeMilliseconds_ = 0.0;
        if(backend_)
            backend_->ResetPeakDecodeMilliseconds();
    }

    double FrameDecoder::WorkerCpuMilliseconds() const
    {
        return accumulatedWorkerCpuMilliseconds_ +
            ReadBackend<double>(backend_, 0.0, [](const auto& value) {
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
        return accumulatedBufferAllocations_ +
            ReadBackend<std::uint64_t>(backend_, 0, [](const auto& value) {
                return value.BufferAllocations();
            });
    }

    const char* FrameDecoder::RuntimeVersion() const
    {
        return backend_ ? backend_->RuntimeVersion() : "not loaded";
    }

    const char* FrameDecoder::CodecName() const
    {
        return backend_ ? backend_->CodecName() : "unknown";
    }

    bool FrameDecoder::UsingHardwareDecoder() const
    {
        return backend_ && backend_->UsingHardwareDecoder();
    }

    const char* FrameDecoder::DecodeMethodName() const
    {
        if(!backend_)
            return "none";
        return UsingHardwareDecoder() ? "hardware" : "software";
    }

    std::unique_ptr<FrameDecoderBackend> FrameDecoder::CreateSelectedBackend() const
    {
        return useFfmpeg9_
            ? CreateFrameDecoder9Backend()
            : CreateFrameDecoder44Backend();
    }

    void FrameDecoder::CloseAndRetainBackendMetrics()
    {
        if(!backend_)
            return;

        backend_->RequestStop();
        const bool stoppedWithinUiBudget =
            backend_->WaitForWorkerStop(std::chrono::milliseconds(4));
        if(stoppedWithinUiBudget)
            backend_->Close();
        retainedPeakDecodeMilliseconds_ = std::max(
            retainedPeakDecodeMilliseconds_,
            backend_->PeakDecodeMilliseconds());
        accumulatedWorkerCpuMilliseconds_ += backend_->WorkerCpuMilliseconds();
        accumulatedBufferAllocations_ += backend_->BufferAllocations();
        if(stoppedWithinUiBudget)
        {
            backend_.reset();
            return;
        }

        PaperLogger.warn(
            "Decoder teardown exceeded 4 ms; cleanup moved off Unity's thread");
        DecoderRetirementQueue::Instance().Retire(std::move(backend_));
    }

    bool FrameDecoder::ReopenWithSoftwareAfterHardwareFailure(
        const std::string& hardwareError,
        std::string& recoveryError)
    {
        hardwareFallbackAttempted_ = true;
        CloseAndRetainBackendMetrics();

        PaperLogger.warn(
            "{} Reopening the same file with software decoding at {:.3f}s.",
            hardwareError,
            lastRequestedSeconds_);
        ErrorManager::Instance().RecordError(
            "Recovering from hardware video decoding",
            hardwareError + "; reopening with software");

        backend_ = CreateSelectedBackend();
        if(!backend_)
        {
            recoveryError = "The selected FFmpeg backend is unavailable";
            return false;
        }
        if(!backend_->Open(
               videoPath_,
               maximumOutputHeight_,
               false,
               nullptr,
               visualEffects_,
               recoveryError))
            return false;

        backend_->Request(lastRequestedSeconds_);
        PaperLogger.info(
            "Recovered video playback with the software decoder at {:.3f}s",
            lastRequestedSeconds_);
        return true;
    }
}
