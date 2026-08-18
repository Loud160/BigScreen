// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/CinemaBloomRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "GlobalNamespace/BloomPrePass.hpp"
#include "GlobalNamespace/BloomPrePassEffectContainerSO.hpp"
#include "GlobalNamespace/BloomPrePassEffectSO.hpp"
#include "GlobalNamespace/BloomPrePassRenderDataSO.hpp"
#include "GlobalNamespace/BloomPrePassRendererSO.hpp"
#include "GlobalNamespace/KawaseBlurRendererSO.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GL.hpp"
#include "UnityEngine/Graphics.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Matrix4x4.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/RenderTextureDescriptor.hpp"
#include "UnityEngine/RenderTextureFormat.hpp"
#include "UnityEngine/RenderTextureReadWrite.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr int BloomDownsample = 1;

        UnityEngine::RenderTextureDescriptor TemporaryDescriptor(
            UnityEngine::RenderTexture* reference,
            int width,
            int height)
        {
            auto descriptor = reference->get_descriptor();
            descriptor._width_k__BackingField = std::max(1, width);
            descriptor._height_k__BackingField = std::max(1, height);
            descriptor._msaaSamples_k__BackingField = 1;
            descriptor._depthStencilFormat_k__BackingField = {};
            return descriptor;
        }

        class TemporaryTexture final {
        public:
            explicit TemporaryTexture(UnityEngine::RenderTexture* value = nullptr)
                : value_(value) {}
            ~TemporaryTexture()
            {
                if(value_)
                    UnityEngine::RenderTexture::ReleaseTemporary(value_);
            }
            TemporaryTexture(const TemporaryTexture&) = delete;
            TemporaryTexture& operator=(const TemporaryTexture&) = delete;
            UnityEngine::RenderTexture* Get() const { return value_; }
        private:
            UnityEngine::RenderTexture* value_ = nullptr;
        };

        class RenderStateGuard final {
        public:
            RenderStateGuard()
                : active_(UnityEngine::RenderTexture::get_active()),
                  sRgbWrite_(UnityEngine::GL::get_sRGBWrite())
            {
                UnityEngine::GL::set_sRGBWrite(false);
            }
            ~RenderStateGuard()
            {
                // A camera callback must never leak Big Screen's temporary
                // render target or color-space state into Beat Saber.
                UnityEngine::RenderTexture::set_active(active_);
                UnityEngine::GL::set_sRGBWrite(sRgbWrite_);
            }
            RenderStateGuard(const RenderStateGuard&) = delete;
            RenderStateGuard& operator=(const RenderStateGuard&) = delete;
        private:
            UnityEngine::RenderTexture* active_ = nullptr;
            bool sRgbWrite_ = false;
        };

        class MatrixGuard final {
        public:
            MatrixGuard() { UnityEngine::GL::PushMatrix(); }
            ~MatrixGuard() { UnityEngine::GL::PopMatrix(); }
            MatrixGuard(const MatrixGuard&) = delete;
            MatrixGuard& operator=(const MatrixGuard&) = delete;
        };

        float TextureRatio(float fovDegrees, float projectionScale)
        {
            const float tangent = std::tan(
                fovDegrees * 0.5f * (Pi / 180.0f));
            const float denominator = tangent * projectionScale;
            if(std::abs(denominator) < 0.000001f)
                return 1.0f;
            return std::clamp(1.0f / denominator, 0.0f, 1.0f);
        }

        float Distance(UnityEngine::Vector3 left, UnityEngine::Vector3 right)
        {
            const float x = left.x - right.x;
            const float y = left.y - right.y;
            const float z = left.z - right.z;
            return std::sqrt(x * x + y * y + z * z);
        }

        void ApplyCinemaDoubleBlur(
            GlobalNamespace::KawaseBlurRendererSO* renderer,
            UnityEngine::RenderTexture* source,
            UnityEngine::RenderTexture* destination,
            GlobalNamespace::KawaseBlurRendererSO_KernelSize firstKernelSize,
            float firstBoost,
            GlobalNamespace::KawaseBlurRendererSO_KernelSize secondKernelSize,
            float secondBoost,
            float secondBlurAlpha,
            int downsample)
        {
            if(!renderer || !source || !destination)
                return;

            // PC Cinema does not call KawaseBlurRendererSO::DoubleBlur. Its
            // bloom pre-pass deliberately shares the common prefix of the two
            // blur kernels, then completes and combines their unique tails.
            // Beat Saber's convenience DoubleBlur has different internal
            // weighting on Quest and can saturate the entire video surface to
            // white when the game's bloom pipeline is active.
            const auto firstKernel = renderer->GetBlurKernel(firstKernelSize);
            const auto secondKernel = renderer->GetBlurKernel(secondKernelSize);
            const auto firstLength = static_cast<int>(firstKernel.size());
            const auto secondLength = static_cast<int>(secondKernel.size());
            int commonLength = 0;
            while(commonLength < firstLength &&
                  commonLength < secondLength &&
                  firstKernel[commonLength] == secondKernel[commonLength])
            {
                ++commonLength;
            }

            TemporaryTexture commonTexture(
                UnityEngine::RenderTexture::GetTemporary(
                    TemporaryDescriptor(
                        source,
                        source->get_width() >> downsample,
                        source->get_height() >> downsample)));
            if(!commonTexture.Get())
                return;

            constexpr float NoBoost = 0.0f;
            constexpr float NoAlphaWeights = 0.0f;
            constexpr float FullAdditiveAlpha = 1.0f;
            constexpr bool GammaCorrect = true;
            const auto noWeights =
                GlobalNamespace::KawaseBlurRendererSO_WeightsType::None;

            renderer->Blur(
                source,
                commonTexture.Get(),
                firstKernel,
                NoBoost,
                downsample,
                0,
                commonLength,
                NoAlphaWeights,
                FullAdditiveAlpha,
                false,
                GammaCorrect,
                noWeights);
            renderer->Blur(
                commonTexture.Get(),
                destination,
                firstKernel,
                firstBoost,
                0,
                commonLength,
                firstLength - commonLength,
                NoAlphaWeights,
                FullAdditiveAlpha,
                false,
                GammaCorrect,
                noWeights);
            renderer->Blur(
                commonTexture.Get(),
                destination,
                secondKernel,
                secondBoost,
                0,
                commonLength,
                secondLength - commonLength,
                NoAlphaWeights,
                secondBlurAlpha,
                true,
                GammaCorrect,
                noWeights);
        }

        void ApplyQuestSingleBlur(
            GlobalNamespace::KawaseBlurRendererSO* renderer,
            UnityEngine::RenderTexture* source,
            UnityEngine::RenderTexture* destination,
            float boost,
            int downsample)
        {
            if(!renderer || !source || !destination)
                return;
            const auto kernel = renderer->GetBlurKernel(
                GlobalNamespace::KawaseBlurRendererSO_KernelSize::Kernel35);
            constexpr float NoAlphaWeights = 0.0f;
            constexpr float FullAdditiveAlpha = 1.0f;
            constexpr bool GammaCorrect = true;
            renderer->Blur(
                source,
                destination,
                kernel,
                boost,
                downsample,
                0,
                static_cast<int>(kernel.size()),
                NoAlphaWeights,
                FullAdditiveAlpha,
                false,
                GammaCorrect,
                GlobalNamespace::KawaseBlurRendererSO_WeightsType::None);
        }
    }

    CinemaBloomRenderer& CinemaBloomRenderer::Instance()
    {
        static CinemaBloomRenderer renderer;
        return renderer;
    }

    void CinemaBloomRenderer::RegisterSource(
        UnityEngine::GameObject* videoObject,
        float intensity,
        float screenWidth,
        float screenHeight,
        bool pcStyle)
    {
        if(!videoObject)
            return;
        UpdateSource(
            videoObject, intensity, screenWidth, screenHeight, pcStyle);
    }

    void CinemaBloomRenderer::UpdateSource(
        UnityEngine::GameObject* videoObject,
        float intensity,
        float screenWidth,
        float screenHeight,
        bool pcStyle)
    {
        if(!videoObject)
            return;
        const auto match = std::find_if(
            sources_.begin(),
            sources_.end(),
            [videoObject](const Source& source)
            {
                return source.videoObject.unsafePtr() == videoObject;
            });
        const Source next{
            videoObject,
            CoreLogic::CinemaBloomIntensity(intensity),
            std::max(0.0001f, std::abs(screenWidth)),
            std::max(0.0001f, std::abs(screenHeight)),
            pcStyle};
        if(match == sources_.end())
            sources_.push_back(next);
        else
            *match = next;
    }

    void CinemaBloomRenderer::UnregisterSource(
        UnityEngine::GameObject* videoObject)
    {
        std::erase_if(
            sources_,
            [videoObject](const Source& source)
            {
                return !UnityW<UnityEngine::GameObject>::isAlive(
                           source.videoObject.unsafePtr()) ||
                    source.videoObject.unsafePtr() == videoObject;
            });
    }

    GlobalNamespace::KawaseBlurRendererSO*
    CinemaBloomRenderer::ResolveBlurRenderer()
    {
        if(UnityW<GlobalNamespace::KawaseBlurRendererSO>::isAlive(
               blurRenderer_.unsafePtr()))
            return blurRenderer_.unsafePtr();

        blurRenderer_ = nullptr;
        const auto renderers = UnityEngine::Resources::FindObjectsOfTypeAll<
            GlobalNamespace::KawaseBlurRendererSO*>();
        for(auto* renderer : renderers)
        {
            if(UnityW<GlobalNamespace::KawaseBlurRendererSO>::isAlive(renderer))
            {
                blurRenderer_ = renderer;
                return renderer;
            }
        }
        return nullptr;
    }

    bool CinemaBloomRenderer::EnsureAdditiveMaterial(
        GlobalNamespace::KawaseBlurRendererSO* renderer)
    {
        if(UnityW<UnityEngine::Material>::isAlive(additiveMaterial_.unsafePtr()))
            return true;
        if(!renderer)
            return false;
        auto* sourceMaterial =
            renderer->__cordl_internal_get__additiveMaterial().ptr();
        auto* shader = sourceMaterial ? sourceMaterial->get_shader().ptr() : nullptr;
        if(!shader)
            return false;
        additiveMaterial_ = UnityEngine::Material::New_ctor(shader);
        return UnityW<UnityEngine::Material>::isAlive(
            additiveMaterial_.unsafePtr());
    }

    void CinemaBloomRenderer::OnCameraPreRender(
        UnityEngine::Camera* camera) noexcept
    {
        try
        {
            std::erase_if(
                sources_,
                [](const Source& source)
                {
                    return !UnityW<UnityEngine::GameObject>::isAlive(
                        source.videoObject.unsafePtr());
                });
            if(!camera || sources_.empty())
                return;

            const std::string cameraName(camera->get_name());
            if(cameraName == "SmoothCamera" ||
               cameraName == "BurnMarksCamera" ||
               cameraName.starts_with("MirrorCam"))
                return;

            for(auto& source : sources_)
            {
                if(source.intensity <= 0.0f)
                    continue;
                auto* object = source.videoObject.unsafePtr();
                if(!UnityW<UnityEngine::GameObject>::isAlive(object) ||
                   !object->get_activeInHierarchy())
                    continue;
                RenderSource(camera, source);
            }
        }
        catch(const std::exception& exception)
        {
            ReportFailure(exception.what());
        }
        catch(...)
        {
            ReportFailure("an unknown native or IL2CPP rendering error occurred");
        }
    }

    void CinemaBloomRenderer::RenderSource(
        UnityEngine::Camera* camera,
        Source& source)
    {
        auto* object = source.videoObject.unsafePtr();
        auto* screenRenderer = object
            ? object->GetComponent<UnityEngine::MeshRenderer*>() : nullptr;
        auto* meshFilter = object
            ? object->GetComponent<UnityEngine::MeshFilter*>() : nullptr;
        auto* material = screenRenderer
            ? screenRenderer->get_sharedMaterial().ptr() : nullptr;
        auto* mesh = meshFilter ? meshFilter->get_sharedMesh().ptr() : nullptr;
        auto* transform = object ? object->get_transform().ptr() : nullptr;
        if(!screenRenderer || !screenRenderer->get_enabled() ||
           !material || !mesh || !transform)
            return;

        auto* prePass = camera->GetComponent<GlobalNamespace::BloomPrePass*>();
        if(!prePass)
            return;
        auto* prePassRenderer =
            prePass->__cordl_internal_get__bloomPrepassRenderer().ptr();
        auto* effectContainer =
            prePass->__cordl_internal_get__bloomPrePassEffectContainer().ptr();
        auto* effect = effectContainer
            ? effectContainer->get_bloomPrePassEffect().ptr() : nullptr;
        auto* renderData = prePass->__cordl_internal_get__renderData();
        auto* destination = renderData
            ? renderData->__cordl_internal_get_bloomPrePassRenderTexture().ptr()
            : nullptr;
        auto* blurRenderer = ResolveBlurRenderer();
        if(!prePassRenderer || !effect || !renderData || !destination ||
           !blurRenderer || !EnsureAdditiveMaterial(blurRenderer))
            return;

        const int width = effect->get_textureWidth();
        const int height = effect->get_textureHeight();
        if(width < 2 || height < 2)
            return;

        UnityEngine::Matrix4x4 projection{};
        UnityEngine::Matrix4x4 view{};
        float stereoEyeOffset = 0.0f;
        prePassRenderer->GetCameraParams(
            camera, byref(projection), byref(view), byref(stereoEyeOffset));

        const auto fov = effect->get_fov();
        const UnityEngine::Vector2 textureToScreenRatio{
            TextureRatio(fov.x, projection.m00),
            TextureRatio(fov.y, projection.m11)};
        projection.m00 *= textureToScreenRatio.x;
        projection.m02 *= textureToScreenRatio.x;
        projection.m11 *= textureToScreenRatio.y;
        projection.m12 *= textureToScreenRatio.y;

        RenderStateGuard renderState;
        // Cinema's Kawase implementation deliberately works in a linear HDR
        // RGB111110Float target. Cloning Beat Saber's destination descriptor
        // here changed that working format on Quest and caused the contribution
        // to saturate into a solid white rectangle. Keep these intermediates
        // identical to PC Cinema; Graphics.Blit performs the final conversion
        // into the camera-owned bloom destination.
        TemporaryTexture sourceTexture(
            UnityEngine::RenderTexture::GetTemporary(
                width,
                height,
                0,
                UnityEngine::RenderTextureFormat::RGB111110Float,
                UnityEngine::RenderTextureReadWrite::Linear));
        TemporaryTexture blurTexture(
            UnityEngine::RenderTexture::GetTemporary(
                width >> BloomDownsample,
                height >> BloomDownsample,
                0,
                UnityEngine::RenderTextureFormat::RGB111110Float,
                UnityEngine::RenderTextureReadWrite::Linear));
        if(!sourceTexture.Get() || !blurTexture.Get())
            return;

        UnityEngine::Graphics::SetRenderTarget(sourceTexture.Get());
        UnityEngine::GL::Clear(true, true, UnityEngine::Color::get_black());
        {
            MatrixGuard matrix;
            // Match Cinema's capture contract: draw the exact material the
            // player sees into a linear HDR target. Bloom strength belongs in
            // the blur boost, not a color property that an opaque shader may
            // ignore.
            UnityEngine::GL::LoadProjectionMatrix(projection);
            if(!material->SetPass(0))
                return;
            UnityEngine::Graphics::DrawMeshNow(
                mesh,
                UnityEngine::Matrix4x4::TRS(
                    transform->get_position(),
                    transform->get_rotation(),
                    transform->get_lossyScale()),
                0);
        }

        auto cameraTransform = camera->get_transform();
        const auto screenPosition = transform->get_position();
        const auto cameraPosition = cameraTransform->get_position();
        const UnityEngine::Vector3 targetDirection{
            screenPosition.x - cameraPosition.x,
            screenPosition.y - cameraPosition.y,
            screenPosition.z - cameraPosition.z};
        const float cameraAngle = UnityEngine::Vector3::Angle(
            targetDirection,
            cameraTransform->get_forward());
        const float boost = CoreLogic::CinemaBloomBoost(
            source.width,
            source.height,
            Distance(screenPosition, cameraPosition),
            camera->get_fieldOfView(),
            cameraAngle,
            source.intensity);
        if(boost <= 0.0f)
            return;

        if(source.pcStyle)
        {
            ApplyCinemaDoubleBlur(
                blurRenderer,
                sourceTexture.Get(),
                blurTexture.Get(),
                GlobalNamespace::KawaseBlurRendererSO_KernelSize::Kernel127,
                boost,
                GlobalNamespace::KawaseBlurRendererSO_KernelSize::Kernel35,
                boost,
                0.5f,
                BloomDownsample);
        }
        else
        {
            // The Quest mode uses one shorter kernel. It remains driven by
            // bright video pixels but avoids the shared-prefix double blur,
            // providing a meaningful lower-cost comparison with PC style.
            ApplyQuestSingleBlur(
                blurRenderer,
                sourceTexture.Get(),
                blurTexture.Get(),
                boost,
                BloomDownsample);
        }
        UnityEngine::Graphics::Blit(
            blurTexture.Get(), destination, additiveMaterial_.ptr());
        GlobalNamespace::BloomPrePassRendererSO::SetDataToShaders(
            stereoEyeOffset,
            textureToScreenRatio,
            destination,
            renderData->__cordl_internal_get_toneMapping());
    }

    void CinemaBloomRenderer::ReportFailure(const char* detail) noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        if(lastFailure_ != std::chrono::steady_clock::time_point{} &&
           now - lastFailure_ < std::chrono::minutes(3))
            return;
        lastFailure_ = now;
        const std::string message = detail && *detail
            ? detail : "the Beat Saber bloom renderer was unavailable";
        PaperLogger.error(
            "PC-style Cinema bloom was skipped: {}", message);
        ErrorManager::Instance().RecordError(
            "Rendering PC-style Cinema bloom",
            message + "; video playback continued without the PC-style bloom pass");
    }
}
