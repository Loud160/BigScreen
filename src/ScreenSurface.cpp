// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/ScreenSurface.hpp"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <array>
#include <exception>
#include <span>
#include <utility>
#include <vector>

#include "BigScreen/ExperimentalFeatures.hpp"

// BLOOM EXPERIMENT DISABLED (2026-08-18): implementation is retained behind
// one named build gate, but the ordinary release surface does not register
// with it.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
#include "BigScreen/CinemaBloomRenderer.hpp"
#endif
#include "BigScreen/FrameDecoder.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/Settings.hpp"
#include "main.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshPro.hpp"
#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Graphics.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/LayerMask.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/RenderTextureFormat.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/Texture.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Vector4.hpp"
#include "System/IntPtr.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#include "beatsaber-hook/shared/utils/utils.h"

extern "C" std::uint8_t _binary_bigscreen_video_shader_start[];
extern "C" std::uint8_t _binary_bigscreen_video_shader_end[];

namespace BigScreen {
    namespace {
        constexpr float Pi = 3.14159265358979323846f;

        // Unity scene teardown leaves ordinary IL2CPP pointers non-null after
        // their managed objects have been destroyed (Unity's "fake null").
        // Every deferred cleanup path must therefore verify liveness before it
        // asks Unity to destroy an object again.
        template<class T>
        void DestroyIfAlive(T*& object)
        {
            if(UnityW<T>::isAlive(object))
                UnityEngine::Object::Destroy(object);
            object = nullptr;
        }

        struct VideoShaderResources {
            // Raw IL2CPP pointers are not GC roots. The previous static
            // Shader* survived scene changes as a non-null address after its
            // managed wrapper/native shader had been reclaimed, eventually
            // sending an invalid Shader to Material::CreateWithShader. Keep
            // both the bundle and shader behind SafePtrUnity handles for the
            // complete process lifetime instead.
            SafePtrUnity<UnityEngine::AssetBundle> bundle;
            SafePtrUnity<UnityEngine::Shader> shader;
            SafePtrUnity<UnityEngine::Shader> yuvConversionShader;
            SafePtrUnity<UnityEngine::Shader> packedYuvConversionShader;
            bool loadAttempted = false;
            // This is deliberately historical, not the outcome of the most
            // recent recovery attempt. A packaged shader that worked earlier
            // may fail to reload during one scene-transition frame and must be
            // allowed to recover on a later screen creation. A shader that has
            // never loaded still suppresses repeated permanent-package errors.
            bool everLoadedSuccessfully = false;
            std::chrono::steady_clock::time_point nextRecoveryAttempt{};
        };

        VideoShaderResources& CachedVideoShaderResources()
        {
            static VideoShaderResources resources;
            return resources;
        }

        UnityEngine::Shader* LoadVideoShaderAsset(
            UnityEngine::AssetBundle* bundle)
        {
            if(!UnityW<UnityEngine::AssetBundle>::isAlive(bundle))
                return nullptr;

            auto loaded = bundle->LoadAsset<UnityEngine::Shader*>(
                "bigscreen-video-shader");
            auto* shader = loaded;
            if(!UnityW<UnityEngine::Shader>::isAlive(shader))
            {
                // The asset path is retained as a compatibility fallback for
                // bundles regenerated by older Unity editor tooling.
                loaded = bundle->LoadAsset<UnityEngine::Shader*>(
                    "assets/bigscreenvideo.shader");
                shader = loaded;
            }
            return UnityW<UnityEngine::Shader>::isAlive(shader)
                ? shader
                : nullptr;
        }

        UnityEngine::Shader* LoadYuvConversionShaderAsset(
            UnityEngine::AssetBundle* bundle)
        {
            if(!UnityW<UnityEngine::AssetBundle>::isAlive(bundle))
                return nullptr;
            auto loaded = bundle->LoadAsset<UnityEngine::Shader*>(
                "bigscreen-yuv-conversion-shader");
            auto* shader = loaded;
            if(!UnityW<UnityEngine::Shader>::isAlive(shader))
            {
                loaded = bundle->LoadAsset<UnityEngine::Shader*>(
                    "assets/bigscreenyuvconversion.shader");
                shader = loaded;
            }
            return UnityW<UnityEngine::Shader>::isAlive(shader)
                ? shader
                : nullptr;
        }

        UnityEngine::Shader* LoadPackedYuvConversionShaderAsset(
            UnityEngine::AssetBundle* bundle)
        {
            if(!UnityW<UnityEngine::AssetBundle>::isAlive(bundle))
                return nullptr;
            auto loaded = bundle->LoadAsset<UnityEngine::Shader*>(
                "bigscreen-packed-yuv-conversion-shader");
            auto* shader = loaded;
            if(!UnityW<UnityEngine::Shader>::isAlive(shader))
            {
                loaded = bundle->LoadAsset<UnityEngine::Shader*>(
                    "assets/bigscreenpackedyuvconversion.shader");
                shader = loaded;
            }
            return UnityW<UnityEngine::Shader>::isAlive(shader)
                ? shader
                : nullptr;
        }

        bool RetainVideoShader(
            VideoShaderResources& resources,
            UnityEngine::AssetBundle* bundle,
            UnityEngine::Shader* shader)
        {
            if(!UnityW<UnityEngine::AssetBundle>::isAlive(bundle) ||
               !UnityW<UnityEngine::Shader>::isAlive(shader))
                return false;

            // Unity may run UnloadUnusedAssets between the menu and gameplay
            // scenes. Preserve this tiny process-lifetime shader asset, while
            // retaining the bundle as its explicit native owner. Recovery in
            // FindVideoShader still handles an explicit invalidation.
            const auto existingFlags =
                static_cast<std::int32_t>(shader->get_hideFlags());
            const auto retainFlag = static_cast<std::int32_t>(
                UnityEngine::HideFlags::DontUnloadUnusedAsset);
            shader->set_hideFlags(UnityEngine::HideFlags{
                existingFlags | retainFlag});
            // Recovery commonly reloads the Shader from the bundle already
            // held by resources. Do not replace an identical SafePtrUnity:
            // assignment would briefly release and recreate its GC root.
            if(!resources.bundle || resources.bundle.ptr() != bundle)
                resources.bundle = bundle;
            resources.shader = shader;
            resources.everLoadedSuccessfully = true;
            resources.nextRecoveryAttempt = {};
            return true;
        }

        UnityEngine::Shader* FindVideoShader()
        {
            // Beat Saber's bloom composite interprets render-target alpha as
            // an emission weight. Unity's stock unlit shaders overwrite that
            // channel with the video's fully opaque alpha and therefore turn
            // the complete screen white when game bloom is enabled. The
            // embedded shader uses separate RGB/alpha blend equations: RGB
            // remains fully visible while the destination alpha is preserved.
            auto& resources = CachedVideoShaderResources();
            const bool shaderAlive = static_cast<bool>(resources.shader);
            if(shaderAlive)
                return resources.shader.ptr();

            // A first-load failure is remembered so every presentation does
            // not retry. A shader that loaded successfully and was later
            // invalidated during a Unity scene transition is different: it
            // must be recovered or every later preview will fail.
            if(!CoreLogic::ShouldAttemptCachedUnityResourceLoad(
                   resources.loadAttempted,
                   resources.everLoadedSuccessfully,
                   shaderAlive))
                return nullptr;
            resources.loadAttempted = true;

            if(resources.everLoadedSuccessfully)
            {
                const auto now = std::chrono::steady_clock::now();
                if(now < resources.nextRecoveryAttempt)
                    return nullptr;
                // Several material helpers may request the shader while one
                // screen is being built. If this complete recovery attempt
                // fails, suppress only those immediate duplicate retries; a
                // later preview remains eligible after the transition settles.
                resources.nextRecoveryAttempt = now +
                    std::chrono::seconds(1);
                BigScreen::BigScreenLogger.warn(
                    "The cached Big Screen video shader was invalidated; "
                    "reloading it before creating the next screen");
                resources.shader.clear();

                // Rehydrate from the retained bundle first. If Unity also
                // invalidated that bundle, explicitly unload any surviving
                // native owner before loading the same embedded bundle bytes
                // again; SafePtrUnity::clear() alone does not unload it.
                if(resources.bundle)
                {
                    try
                    {
                        if(auto* recovered =
                               LoadVideoShaderAsset(resources.bundle.ptr());
                           recovered && RetainVideoShader(
                               resources, resources.bundle.ptr(), recovered))
                        {
                            BigScreen::BigScreenLogger.info(
                                "Recovered Big Screen's cached video shader "
                                "from its retained bundle ({})",
                                recovered->get_name());
                            return recovered;
                        }
                    }
                    catch(const std::exception& exception)
                    {
                        BigScreen::BigScreenLogger.warn(
                            "Could not recover the video shader from its "
                            "retained bundle: {}",
                            exception.what());
                    }
                    catch(...)
                    {
                        BigScreen::BigScreenLogger.warn(
                            "Could not recover the video shader from its "
                            "retained bundle");
                    }

                    try
                    {
                        resources.bundle->Unload(true);
                    }
                    catch(...)
                    {
                        // Continue with a fresh embedded-bundle load. The
                        // SafePtr liveness checks below remain authoritative.
                    }
                }
                resources.bundle.clear();
                resources.shader.clear();
                resources.yuvConversionShader.clear();
                resources.packedYuvConversionShader.clear();
            }

            UnityEngine::AssetBundle* bundle = nullptr;
            try
            {
                const auto* begin = _binary_bigscreen_video_shader_start;
                const auto* end = _binary_bigscreen_video_shader_end;
                if(end <= begin)
                {
                    BigScreen::BigScreenLogger.error("Embedded Big Screen video shader is empty");
                    return nullptr;
                }

                ArrayW<std::uint8_t> bundleBytes(std::span<const std::uint8_t>(
                    begin, static_cast<std::size_t>(end - begin)));
                using LoadFromMemory = function_ptr_t<
                    UnityEngine::AssetBundle*, ArrayW<std::uint8_t>, std::uint32_t>;
                static auto loadFromMemory = reinterpret_cast<LoadFromMemory>(
                    il2cpp_functions::resolve_icall(
                        "UnityEngine.AssetBundle::LoadFromMemory_Internal"));
                if(!loadFromMemory)
                {
                    BigScreen::BigScreenLogger.error(
                        "Unity AssetBundle LoadFromMemory entry point is unavailable");
                    return nullptr;
                }

                bundle = loadFromMemory(bundleBytes, 0);
                if(!bundle)
                {
                    BigScreen::BigScreenLogger.error(
                        "Unity rejected Big Screen's embedded Android video shader bundle");
                    return nullptr;
                }

                auto* loaded = LoadVideoShaderAsset(bundle);
                if(!loaded)
                {
                    bundle->Unload(true);
                    bundle = nullptr;
                    BigScreen::BigScreenLogger.error(
                        "Big Screen's embedded bundle did not contain its video shader");
                    return nullptr;
                }

                // Retaining this tiny, shader-only bundle is intentional.
                // Unload(false) keeps Unity's native asset but releases the
                // bundle that establishes its ownership. Combined with an
                // unrooted managed pointer, that produced the observed crash
                // after later scene/GC cycles. One process-lifetime owner is
                // cheaper and safer than repeatedly loading the same bytes.
                if(!RetainVideoShader(resources, bundle, loaded))
                {
                    bundle->Unload(true);
                    bundle = nullptr;
                    BigScreen::BigScreenLogger.error(
                        "Unity invalidated Big Screen's embedded video "
                        "shader while it was being retained");
                    return nullptr;
                }

                BigScreen::BigScreenLogger.info(
                    "Loaded Big Screen's bloom-compatible video material ({})",
                    loaded->get_name());
                return resources.shader.ptr();
            }
            catch(const std::exception& exception)
            {
                if(UnityW<UnityEngine::AssetBundle>::isAlive(bundle))
                {
                    try { bundle->Unload(true); } catch(...) {}
                }
                resources.bundle.clear();
                resources.shader.clear();
                resources.yuvConversionShader.clear();
                resources.packedYuvConversionShader.clear();
                BigScreen::BigScreenLogger.error(
                    "Could not load Big Screen's embedded video shader: {}",
                    exception.what());
                return nullptr;
            }
            catch(...)
            {
                if(UnityW<UnityEngine::AssetBundle>::isAlive(bundle))
                {
                    try { bundle->Unload(true); } catch(...) {}
                }
                resources.bundle.clear();
                resources.shader.clear();
                resources.yuvConversionShader.clear();
                resources.packedYuvConversionShader.clear();
                BigScreen::BigScreenLogger.error(
                    "Could not load Big Screen's embedded video shader");
                return nullptr;
            }
        }

        void ConfigureVideoBlend(
            UnityEngine::Material* material,
            int sourceColor,
            int destinationColor,
            int sourceAlpha,
            int destinationAlpha,
            bool writeDepth,
            int renderQueue)
        {
            if(!material)
                return;
            material->SetInt("_SrcColor", sourceColor);
            material->SetInt("_DestColor", destinationColor);
            // Beat Saber's bloom composite reads the framebuffer's ALPHA
            // channel as a per-pixel emission weight. The screen must CLEAR
            // that weight where video covers it - Cinema parity - rather
            // than preserve it: maps whose lighting writes strong emission
            // behind the screen (YY.exe and other bloom-heavy maps)
            // otherwise multiply the bright video RGB into a solid white
            // rectangle even though the video itself never writes alpha.
            // Opaque screens force the weight to zero (Zero/Zero);
            // transparent and soft-additive screens attenuate it by video
            // coverage (Zero/OneMinusSrcAlpha) so emission still shows
            // through genuinely see-through pixels. Source alpha continues
            // to drive RGB transparency through the separate color equation.
            material->SetInt("_SrcAlpha", sourceAlpha);
            material->SetInt("_DestAlpha", destinationAlpha);
            material->SetInt("_ZWrite", writeDepth ? 1 : 0);
            material->set_renderQueue(renderQueue);
        }

        // Which shader is requested for the video, in strict preference order.
        // Resolving a shader confirms the selected material path; it does not
        // replace visual verification in the Quest's stereo render pipeline.
        enum class VideoShaderFamily {
            // Experimental embedded BigScreen/Video material.
            BloomSafe,
            // Experimental Unity UI/Default picture path with RGB-only writes.
            UiMasked,
            // Stock unlit shaders as the absolute last resort when even
            // UI/Default is missing, which no shipping Unity player should
            // hit. This tier whites out under bloom and exists only so a
            // catastrophic shader environment still shows video with bloom
            // disabled instead of refusing to create a screen.
            LegacyUnlit,
        };

        struct VideoShaderChoice {
            UnityEngine::Shader* shader = nullptr;
            VideoShaderFamily family = VideoShaderFamily::LegacyUnlit;
        };

        VideoShaderChoice ResolveVideoShader(bool pictureTransparent)
        {
            // The Misc-tab toggle selects UI/Default (OFF) or the embedded
            // BigScreen/Video bundle (ON). One verified build requirement is
            // that a bundle built from a Unity project WITHOUT XR configuration gets
            //    its STEREO_MULTIVIEW_ON variants stripped even when the
            //    shader source requests them with multi_compile. Such a
            //    shader binds without error and rasterizes nothing in either
            //    eye on the Quest's single-pass multiview renderer. The
            //    bundle project therefore carries XR Plugin Management with the
            //    Oculus loader in Multiview mode (enforced by
            //    BuildBigScreenVideoShader.EnsureAndroidMultiviewXr, which
            //    refuses to build without it). This prevents the known
            //    mono-variant packaging failure but still requires visual
            //    verification of the complete material path on the headset.
            const bool preferEmbedded =
                Settings::Instance().EmbeddedVideoShaderEnabled();
            static int announcedFamily = -1;

            // The embedded shader is not merely the optional visible-picture
            // path. UI/Default also requires it for the independent black
            // letterbox material and RGB-preserving alpha guard. Resolve that
            // packaged dependency first so a failed load cannot be described
            // as a usable UI fallback and then fail later during construction.
            auto* bloomSafe = FindVideoShader();
            if(!bloomSafe)
            {
                constexpr int MissingRequiredShader = -2;
                if(announcedFamily != MissingRequiredShader)
                {
                    announcedFamily = MissingRequiredShader;
                    BigScreen::BigScreenLogger.error(
                        "The embedded BigScreen/Video shader is unavailable; "
                        "a safe video screen cannot be created");
                    ErrorManager::Instance().RecordError(
                        "Selecting the video screen shader",
                        "The required embedded BigScreen/Video shader was "
                        "unavailable");
                }
                return {};
            }

            if(preferEmbedded)
            {
                if(announcedFamily !=
                   static_cast<int>(VideoShaderFamily::BloomSafe))
                {
                    announcedFamily =
                        static_cast<int>(VideoShaderFamily::BloomSafe);
                    BigScreen::BigScreenLogger.info(
                        "Using the embedded BigScreen/Video shader "
                        "selected in settings");
                }
                return {bloomSafe, VideoShaderFamily::BloomSafe};
            }

            if(auto uiShader = UnityEngine::Shader::Find("UI/Default"))
            {
                if(announcedFamily !=
                   static_cast<int>(VideoShaderFamily::UiMasked))
                {
                    announcedFamily =
                        static_cast<int>(VideoShaderFamily::UiMasked);
                    BigScreen::BigScreenLogger.info(
                        "Using Unity's UI/Default RGB-only video material "
                        "path (default selection)");
                }
                return {uiShader.unsafePtr(), VideoShaderFamily::UiMasked};
            }

            // UI/Default is somehow missing: the embedded shader becomes the
            // backup even when it was not selected.
            if(!preferEmbedded)
            {
                if(announcedFamily !=
                   static_cast<int>(VideoShaderFamily::BloomSafe))
                {
                    announcedFamily =
                        static_cast<int>(VideoShaderFamily::BloomSafe);
                    BigScreen::BigScreenLogger.error(
                        "UI/Default was unexpectedly missing; using the "
                        "experimental embedded video shader instead.");
                    ErrorManager::Instance().RecordError(
                        "Selecting the video screen shader",
                        "UI/Default was unexpectedly missing; using the "
                        "experimental embedded video shader");
                }
                return {bloomSafe, VideoShaderFamily::BloomSafe};
            }

            if(announcedFamily !=
               static_cast<int>(VideoShaderFamily::LegacyUnlit))
            {
                announcedFamily =
                    static_cast<int>(VideoShaderFamily::LegacyUnlit);
                BigScreen::BigScreenLogger.error(
                    "Falling back to Unity's stock unlit video shaders; the "
                    "picture will wash out while Beat Saber's Bloom setting "
                    "is enabled. UI/Default was unexpectedly missing.");
                ErrorManager::Instance().RecordError(
                    "Selecting the video screen shader",
                    "Both UI/Default and the embedded video shader were "
                    "unavailable; using stock unlit shaders that wash out "
                    "under Bloom");
            }
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
            // BLOOM EXPERIMENT DISABLED (2026-08-18): never select this stock
            // fallback while the no-bloom contract is active. It cannot clear
            // Beat Saber's framebuffer emission channel and may wash the
            // entire screen white. Preserve the old fallback for later work.
            auto fallback = UnityEngine::Shader::Find(
                pictureTransparent ? "Unlit/Transparent" : "Unlit/Texture");
            if(!fallback && pictureTransparent)
                fallback = UnityEngine::Shader::Find("Unlit/Texture");
            return {fallback ? fallback.unsafePtr() : nullptr,
                    VideoShaderFamily::LegacyUnlit};
#else
            (void)pictureTransparent;
            return {nullptr, VideoShaderFamily::LegacyUnlit};
#endif
        }

        UnityEngine::Shader* FindYuvConversionShader()
        {
            auto& resources = CachedVideoShaderResources();
            if(resources.yuvConversionShader)
                return resources.yuvConversionShader.ptr();
            if(!FindVideoShader() || !resources.bundle)
                return nullptr;

            try
            {
                auto* shader = LoadYuvConversionShaderAsset(
                    resources.bundle.ptr());
                if(!shader)
                {
                    BigScreen::BigScreenLogger.warn(
                        "The embedded shader bundle does not contain the "
                        "experimental YUV conversion shader");
                    return nullptr;
                }
                const auto existingFlags = static_cast<std::int32_t>(
                    shader->get_hideFlags());
                const auto retainFlag = static_cast<std::int32_t>(
                    UnityEngine::HideFlags::DontUnloadUnusedAsset);
                shader->set_hideFlags(UnityEngine::HideFlags{
                    existingFlags | retainFlag});
                resources.yuvConversionShader = shader;
                BigScreen::BigScreenLogger.info(
                    "Loaded Big Screen's experimental GPU YUV conversion shader ({})",
                    shader->get_name());
                return shader;
            }
            catch(const std::exception& exception)
            {
                BigScreen::BigScreenLogger.warn(
                    "Could not load the GPU YUV conversion shader: {}",
                    exception.what());
                return nullptr;
            }
            catch(...)
            {
                BigScreen::BigScreenLogger.warn(
                    "Could not load the GPU YUV conversion shader");
                return nullptr;
            }
        }

        UnityEngine::Shader* FindPackedYuvConversionShader()
        {
            auto& resources = CachedVideoShaderResources();
            if(resources.packedYuvConversionShader)
                return resources.packedYuvConversionShader.ptr();
            if(!FindVideoShader() || !resources.bundle)
                return nullptr;

            try
            {
                auto* shader = LoadPackedYuvConversionShaderAsset(
                    resources.bundle.ptr());
                if(!shader)
                {
                    BigScreen::BigScreenLogger.warn(
                        "The embedded shader bundle does not contain the "
                        "experimental packed YUV conversion shader");
                    return nullptr;
                }
                const auto existingFlags = static_cast<std::int32_t>(
                    shader->get_hideFlags());
                const auto retainFlag = static_cast<std::int32_t>(
                    UnityEngine::HideFlags::DontUnloadUnusedAsset);
                shader->set_hideFlags(UnityEngine::HideFlags{
                    existingFlags | retainFlag});
                resources.packedYuvConversionShader = shader;
                BigScreen::BigScreenLogger.info(
                    "Loaded Big Screen's experimental packed GPU YUV conversion shader ({})",
                    shader->get_name());
                return shader;
            }
            catch(const std::exception& exception)
            {
                BigScreen::BigScreenLogger.warn(
                    "Could not load the packed GPU YUV conversion shader: {}",
                    exception.what());
                return nullptr;
            }
            catch(...)
            {
                BigScreen::BigScreenLogger.warn(
                    "Could not load the packed GPU YUV conversion shader");
                return nullptr;
            }
        }

        /// One authoritative mapping from the active presentation modes to
        /// material state for every shader family a video screen can use.
        void ApplyVideoMaterialMode(
            UnityEngine::Material* material,
            VideoShaderFamily family,
            bool colorBlending,
            bool authoredAlpha,
            bool pictureTransparent,
            float opacity)
        {
            if(!material)
                return;
            // BLOOM EXPERIMENT DISABLED (2026-08-18): keep the original
            // Cinema soft-additive decision in source, but do not allow a
            // mapper bloom/color-blending request to make the video surface
            // emissive or see-through. Both shader choices now honor only
            // authored alpha and the player's video-opacity setting.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
            const bool softAdditive = colorBlending && !authoredAlpha;
#else
            (void)colorBlending;
            (void)authoredAlpha;
            const bool softAdditive = false;
#endif

            if(family == VideoShaderFamily::UiMasked)
            {
                // RGB-only writes are the entire point of this tier: the
                // material must never touch the alpha channel Beat Saber's
                // bloom composite reads as an emission weight.
                material->SetInt("_ColorMask", 14); // R | G | B
                // UI/Default (verified against the 2022.3.33f1 built-in
                // source) premultiplies inside the fragment shader
                // (color.rgb *= color.a) and blends One/OneMinusSrcAlpha, so
                // the tint ALPHA alone expresses opacity:
                //   out = video.rgb*a + dst*(1-a), where a = tex.a * tint.a.
                // Authored vignette alpha in the decoded texture therefore
                // also blends correctly with no extra state. Scaling the RGB
                // tint as well would darken the picture twice (o squared).
                // Soft-additive has no equivalent here and uses the same
                // transparent approximation.
                const bool blended = softAdditive || pictureTransparent;
                material->set_color(UnityEngine::Color{
                    1.0f, 1.0f, 1.0f, blended ? opacity : 1.0f});
                // The shader takes its ZTest from the unity_GUIZTestMode int
                // property, which uGUI normally sets per canvas. Pin it to
                // LEqual on this world-space material so scenery in front of
                // the screen still occludes it.
                material->SetInt("unity_GUIZTestMode", 4); // LEqual
                // The shader has no depth write, so even "opaque" screens
                // must draw on the transparent queue after the environment.
                material->set_renderQueue(3000);
                return;
            }

            if(family == VideoShaderFamily::BloomSafe && softAdditive)
            {
                // Cinema's default soft-additive mode lets environment
                // lighting contribute through dark video pixels. These are
                // Unity's OneMinusDstColor and One blend constants used by
                // PC Cinema.
                material->set_color(
                    UnityEngine::Color{1.0f, 1.0f, 1.0f, opacity});
                // Color: OneMinusDstColor/One. Alpha: Zero/OneMinusSrcAlpha
                // clears the bloom-emission weight by video coverage.
                ConfigureVideoBlend(material, 4, 1, 0, 10, false, 3000);
            }
            else if(pictureTransparent)
            {
                material->set_color(
                    UnityEngine::Color{1.0f, 1.0f, 1.0f, opacity});
                if(family == VideoShaderFamily::BloomSafe)
                    // Color: SrcAlpha/OneMinusSrcAlpha. Alpha:
                    // Zero/OneMinusSrcAlpha attenuates the bloom-emission
                    // weight by video coverage.
                    ConfigureVideoBlend(material, 5, 10, 0, 10, false, 3000);
                else
                {
                    material->SetInt("_SrcBlend", 5);  // SrcAlpha
                    material->SetInt("_DstBlend", 10); // OneMinusSrcAlpha
                    material->SetInt("_ZWrite", 0);
                    material->set_renderQueue(3000);
                }
            }
            else
            {
                if(family == VideoShaderFamily::BloomSafe)
                {
                    // Keep RGB fully opaque while forcing the framebuffer's
                    // bloom-emission channel to zero in the same fragment that
                    // draws the video.
                    material->set_color(UnityEngine::Color{
                        1.0f,
                        1.0f,
                        1.0f,
                        0.0f});
                    // Color: One/Zero. Alpha: One/Zero explicitly clears the
                    // bloom-emission channel without changing opaque RGB.
                    ConfigureVideoBlend(material, 1, 0, 1, 0, true, 2000);
                }
                else
                {
                    material->set_color(UnityEngine::Color::get_white());
                    material->SetInt("_SrcBlend", 1); // One
                    material->SetInt("_DstBlend", 0); // Zero
                    material->SetInt("_ZWrite", 1);
                    material->set_renderQueue(2000);
                }
            }
            const bool blendKeyword = softAdditive || pictureTransparent;
            material->DisableKeyword("_ALPHATEST_ON");
            if(blendKeyword)
                material->EnableKeyword("_ALPHABLEND_ON");
            else
                material->DisableKeyword("_ALPHABLEND_ON");
            material->DisableKeyword("_ALPHAPREMULTIPLY_ON");
        }

        bool ConfigureNonEmissiveBackground(
            UnityEngine::Material* material,
            bool transparent)
        {
            if(!material)
                return false;
            auto* shader = FindVideoShader();
            if(!shader)
                return false;

            material->set_shader(shader);
            // White texture plus black RGB/zero alpha gives an unambiguous
            // black fragment whose alpha is controlled here rather than by a
            // Unity shared texture's implementation-defined alpha.
            material->set_mainTexture(
                UnityEngine::Texture2D::get_whiteTexture());
            material->set_color(UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f});
            material->SetInt("_ColorMask", 15);
            material->SetInt("_Cull", 2);
            material->DisableKeyword("_ALPHATEST_ON");
            material->DisableKeyword("_ALPHAPREMULTIPLY_ON");
            if(transparent)
            {
                ConfigureVideoBlend(material, 5, 10, 0, 10, false, 2999);
                material->EnableKeyword("_ALPHABLEND_ON");
            }
            else
            {
                // Opaque black RGB and a fixed zero alpha make the backing
                // block geometry behind it without ever feeding native bloom.
                ConfigureVideoBlend(material, 1, 0, 1, 0, true, 1999);
                material->DisableKeyword("_ALPHABLEND_ON");
            }
            return true;
        }

        float CurveEdgeShape(float u)
        {
            const float centered = u * 2.0f - 1.0f;
            return 1.0f - std::cos(std::abs(centered) * Pi * 0.5f);
        }

        float CurvedWidthScale(float curvature, int segments)
        {
            // The screen curve is scale-invariant: for a unit-wide surface,
            // both X and Z scale linearly with the final width. Measure that
            // unit curve using the same segments as the actual mesh. Dividing
            // projected width by this path-length multiplier makes the curved
            // surface's total image width equal the original flat width.
            float pathLength = 0.0f;
            float previousX = -0.5f;
            float previousZ = -curvature * 0.12f * CurveEdgeShape(0.0f);
            for(int segment = 1; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / segments;
                const float x = u - 0.5f;
                const float z = -curvature * 0.12f * CurveEdgeShape(u);
                const float dx = x - previousX;
                const float dz = z - previousZ;
                pathLength += std::sqrt(dx * dx + dz * dz);
                previousX = x;
                previousZ = z;
            }
            return pathLength > 0.0001f ? 1.0f / pathLength : 1.0f;
        }

        std::uint32_t FractureHash(std::uint32_t seed, std::size_t shard)
        {
            std::uint32_t value = seed ^
                (static_cast<std::uint32_t>(shard) + 1U) * 0x9E3779B9U;
            value ^= value >> 16;
            value *= 0x7FEB352DU;
            value ^= value >> 15;
            value *= 0x846CA68BU;
            value ^= value >> 16;
            return value;
        }

        float HashSigned(std::uint32_t value)
        {
            return static_cast<float>((value >> 8) & 0x00FFFFFFU) /
                static_cast<float>(0x007FFFFFU) - 1.0f;
        }

        UnityEngine::Vector3 RotateFractureVertex(
            UnityEngine::Vector3 value,
            UnityEngine::Vector3 center,
            float xDegrees,
            float yDegrees,
            float zDegrees)
        {
            constexpr float DegreesToRadians = Pi / 180.0f;
            float x = value.x - center.x;
            float y = value.y - center.y;
            float z = value.z - center.z;
            const float cx = std::cos(xDegrees * DegreesToRadians);
            const float sx = std::sin(xDegrees * DegreesToRadians);
            const float cy = std::cos(yDegrees * DegreesToRadians);
            const float sy = std::sin(yDegrees * DegreesToRadians);
            const float cz = std::cos(zDegrees * DegreesToRadians);
            const float sz = std::sin(zDegrees * DegreesToRadians);
            const float firstY = y * cx - z * sx;
            const float firstZ = y * sx + z * cx;
            const float secondX = x * cy + firstZ * sy;
            const float secondZ = -x * sy + firstZ * cy;
            return {
                center.x + secondX * cz - firstY * sz,
                center.y + secondX * sz + firstY * cz,
                center.z + secondZ};
        }

        struct ContentVertex {
            float x;
            float y;
            float z;
            float u;
            float v;
        };

        ContentVertex Interpolate(
            const ContentVertex& left,
            const ContentVertex& right,
            float amount)
        {
            return {
                left.x + (right.x - left.x) * amount,
                left.y + (right.y - left.y) * amount,
                left.z + (right.z - left.z) * amount,
                left.u + (right.u - left.u) * amount,
                left.v + (right.v - left.v) * amount};
        }

        template<typename Inside, typename Intersection>
        std::vector<ContentVertex> ClipEdge(
            const std::vector<ContentVertex>& input,
            Inside inside,
            Intersection intersection)
        {
            std::vector<ContentVertex> output;
            if(input.empty())
                return output;
            ContentVertex previous = input.back();
            bool previousInside = inside(previous);
            for(const auto& current : input)
            {
                const bool currentInside = inside(current);
                if(currentInside != previousInside)
                    output.push_back(intersection(previous, current));
                if(currentInside)
                    output.push_back(current);
                previous = current;
                previousInside = currentInside;
            }
            return output;
        }

        std::vector<ContentVertex> ClipToFrame(
            std::vector<ContentVertex> polygon,
            float halfWidth,
            float halfHeight)
        {
            const auto verticalIntersection = [](float boundary)
            {
                return [boundary](const ContentVertex& a, const ContentVertex& b)
                {
                    const float denominator = b.x - a.x;
                    const float amount = std::abs(denominator) < 0.000001f
                        ? 0.0f : (boundary - a.x) / denominator;
                    auto result = Interpolate(a, b, amount);
                    result.x = boundary;
                    return result;
                };
            };
            const auto horizontalIntersection = [](float boundary)
            {
                return [boundary](const ContentVertex& a, const ContentVertex& b)
                {
                    const float denominator = b.y - a.y;
                    const float amount = std::abs(denominator) < 0.000001f
                        ? 0.0f : (boundary - a.y) / denominator;
                    auto result = Interpolate(a, b, amount);
                    result.y = boundary;
                    return result;
                };
            };
            polygon = ClipEdge(polygon,
                [halfWidth](const ContentVertex& v) { return v.x >= -halfWidth; },
                verticalIntersection(-halfWidth));
            polygon = ClipEdge(polygon,
                [halfWidth](const ContentVertex& v) { return v.x <= halfWidth; },
                verticalIntersection(halfWidth));
            polygon = ClipEdge(polygon,
                [halfHeight](const ContentVertex& v) { return v.y >= -halfHeight; },
                horizontalIntersection(-halfHeight));
            return ClipEdge(polygon,
                [halfHeight](const ContentVertex& v) { return v.y <= halfHeight; },
                horizontalIntersection(halfHeight));
        }
    }

    void ScreenSurface::LogVideoShaderTierOnce() noexcept
    {
        static bool logged = false;
        if(logged)
            return;
        logged = true;
        try
        {
            const auto choice = ResolveVideoShader(false);
            const char* tier = "none - no video shader could be resolved";
            if(choice.shader)
            {
                switch(choice.family)
                {
                    case VideoShaderFamily::BloomSafe:
                        tier = "experimental embedded shader";
                        break;
                    case VideoShaderFamily::UiMasked:
                        tier = "experimental UI/Default shader";
                        break;
                    case VideoShaderFamily::LegacyUnlit:
                        tier = "stock unlit fallback (washes out under Bloom)";
                        break;
                }
            }
            // This exact prefix is matched by the deploy pipeline's post-
            // install logcat check; keep it stable.
            BigScreen::BigScreenLogger.info("Video shader tier: {}", tier);
        }
        catch(const std::exception& exception)
        {
            BigScreen::BigScreenLogger.error(
                "Video shader tier: resolution failed: {}",
                exception.what());
        }
        catch(...)
        {
            BigScreen::BigScreenLogger.error("Video shader tier: resolution failed");
        }
    }

    bool ScreenSurface::Create(
        const MapVideoConfig& config,
        int videoWidth,
        int videoHeight,
        bool preferGpuConversion)
    {
        return CreateInternal(
            config,
            videoWidth,
            videoHeight,
            nullptr,
            "CinemaScreen",
            false,
            preferGpuConversion);
    }

    bool ScreenSurface::CreateShared(
        const MapVideoConfig& config,
        int videoWidth,
        int videoHeight,
        UnityEngine::Texture* sharedTexture,
        const char* rootName)
    {
        if(!sharedTexture)
            return false;
        return CreateInternal(
            config,
            videoWidth,
            videoHeight,
            sharedTexture,
            rootName,
            true,
            false);
    }

    bool ScreenSurface::CreateInternal(
        const MapVideoConfig& config,
        int videoWidth,
        int videoHeight,
        UnityEngine::Texture* sharedTexture,
        const char* rootName,
        bool prepareDeformation,
        bool preferGpuConversion)
    {
        Destroy();
        if(videoWidth <= 0 || videoHeight <= 0 || !rootName)
            return false;
        prepareDeformation_ = prepareDeformation;

        // PC Cinema and Chroma maps conventionally target this exact root name
        // (often with the regex CinemaScreen$). Keeping the compatible name is
        // harmless for ordinary maps and lets later scene integrations find
        // Big Screen's surface without a Quest-specific map variant.
        gameObject_ = UnityEngine::GameObject::New_ctor(rootName);
        if(!gameObject_)
            return false;

        // Quest Chroma intentionally indexes only objects belonging to an
        // environment scene. A newly constructed object otherwise lands in
        // GameCore and is invisible to Chroma's CinemaScreen$ lookup. Move the
        // root into the loaded environment scene before Chroma's delayed scan;
        // no parenting is needed, so the mapper's world coordinates stay exact.
        if(auto environmentRoot = UnityEngine::GameObject::Find("/Environment"))
        {
            UnityEngine::SceneManagement::SceneManager::MoveGameObjectToScene(
                gameObject_, environmentRoot->get_scene());
        }

        // Match Beat Saber's environment geometry so the normal VR cameras see
        // the screen. NameToLayer avoids assuming Unity's numeric layer layout.
        const int environmentLayer = UnityEngine::LayerMask::NameToLayer("Environment");
        if(environmentLayer >= 0)
            gameObject_->set_layer(environmentLayer);

        auto transform = gameObject_->get_transform();
        transform->set_position({
            config.screenPosition.x,
            config.screenPosition.y,
            config.screenPosition.z
        });
        transform->set_eulerAngles({
            config.screenRotation.x,
            config.screenRotation.y,
            config.screenRotation.z
        });

        const float aspectRatio = static_cast<float>(videoWidth) / videoHeight;
        geometryConfig_ = config;
        geometryAspectRatio_ = aspectRatio;
        const float flatWidth = config.screenWidthOverride.value_or(
            config.screenHeight * aspectRatio);
        screenWidth_ = config.maintainAspectRatioWhenCurved &&
                       std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(config.screenCurvature, config.screenSegments)
            : flatWidth;
        screenHeight_ = config.screenHeight;
        if(!CreateMesh(config, aspectRatio) ||
           !CreateVideoMesh(config, aspectRatio) ||
           !CreateMaterialAndTexture(
               videoWidth,
               videoHeight,
               config,
               sharedTexture,
               preferGpuConversion) ||
           !CreateBackgroundMaterial(
               config.opaqueScreenBody ? false : config.letterboxTransparent))
        {
            Destroy();
            return false;
        }

        auto* filter = gameObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* renderer = gameObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!filter || !renderer)
        {
            Destroy();
            return false;
        }

        filter->set_sharedMesh(mesh_);
        renderer->set_material(backgroundMaterial_);
        // Do not rely on a zero-alpha black texture to represent an empty
        // letterbox. Some Unity shader variants still draw it as black on
        // Quest. Removing the background renderer makes the unused part of a
        // non-16:9 frame genuinely transparent.
        renderer->set_enabled(CoreLogic::ScreenBackgroundVisible(
                config.opaqueScreenBody,
                config.letterboxTransparent,
                false,
                videoCoversFrame_,
                textureHasAuthoredAlpha_));

        videoObject_ = UnityEngine::GameObject::New_ctor("Big Screen Video Content");
        if(!videoObject_)
        {
            Destroy();
            return false;
        }
        videoObject_->set_layer(gameObject_->get_layer());
        videoObject_->get_transform()->SetParent(gameObject_->get_transform(), false);
        auto* videoFilter = videoObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* videoRenderer = videoObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!videoFilter || !videoRenderer)
        {
            Destroy();
            return false;
        }
        videoFilter->set_sharedMesh(videoMesh_);
        videoRenderer->set_material(material_);

        if(!CreateUiAlphaGuard())
        {
            Destroy();
            return false;
        }

        // Cinema-style frame glow: register the primary video surface with
        // the bloom pre-pass so the picture can glow around its frame. This
        // is deliberate extra rendering, mirroring PC Cinema. The visible
        // embedded material, or the UI path's separate alpha guard, prevents
        // the main framebuffer from treating the picture itself as a bloom
        // emitter; this registered capture supplies the intended glow. Shared
        // showcase clones are not registered: dozens of animated panels
        // each running two Kawase blurs per camera per frame is not a
        // sustainable Quest cost, and the showcase supplies its own effects.
        // BLOOM EXPERIMENT DISABLED (2026-08-18): preserve this implementation
        // for later investigation, but maps must not inject a bloom pre-pass
        // into either visible video-material path right now.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
        mapperBloom_ = config.bloomIntensity;
        if(!sharedTexture)
        {
            CinemaBloomRenderer::Instance().RegisterSource(
                videoObject_,
                mapperBloom_,
                screenWidth_,
                screenHeight_,
                true);
            bloomRegistered_ = true;
        }
#else
        (void)sharedTexture;
        mapperBloom_ = 0.0f;
        bloomRegistered_ = false;
#endif

        // A screen starts hidden so an uninitialized gray/black texture is not
        // exposed during a negative mapper offset or while FFmpeg seeks to the
        // opening frame. Do this directly: visible_ already starts false, so
        // routing the initial transition through SetVisible(false) would be
        // treated as a no-op even though a new GameObject is active by default.
        gameObject_->SetActive(false);
        visible_ = false;
        return true;
    }

    bool ScreenSurface::CreateMesh(const MapVideoConfig& config, float aspectRatio)
    {
        const int columns = config.screenSegments + 1;
        const int vertexCount = columns * 2;
        const int indexCount = config.screenSegments * 6;

        ArrayW<UnityEngine::Vector3> vertices(vertexCount);
        ArrayW<UnityEngine::Vector2> uvs(vertexCount);
        ArrayW<std::int32_t> triangles(indexCount);

        const float flatWidth = config.screenWidthOverride.value_or(
            config.screenHeight * aspectRatio);
        const float width = config.maintainAspectRatioWhenCurved &&
                            std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(
                config.screenCurvature,
                config.screenSegments)
            : flatWidth;
        const float halfWidth = width * 0.5f;
        const float halfHeight = config.screenHeight * 0.5f;

        const bool cinemaCurve = config.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *config.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        const bool cinemaCurveIsFlat =
            !cinemaCurve || std::abs(cinemaArcRadians) < 0.000001f;
        const float cinemaCurveLength = config.cinemaCurveYAxis
            ? config.screenHeight : width;
        const float cinemaRadius = cinemaCurveIsFlat
            ? 0.0f : cinemaCurveLength / cinemaArcRadians;

        for(int column = 0; column < columns; ++column)
        {
            const float u = static_cast<float>(column) / config.screenSegments;
            const int bottom = column * 2;
            const int top = bottom + 1;

            if(cinemaCurve && !cinemaCurveIsFlat)
            {
                // Cinema defines curvature as an arc angle, not a bow-depth
                // multiplier. Reproduce its circular surface mathematically so
                // mapper values have the same physical size on PC and Quest.
                const float theta = (u - 0.5f) * cinemaArcRadians;
                const float curvedAxis = std::sin(theta) * cinemaRadius;
                const float z = std::cos(theta) * cinemaRadius - cinemaRadius;
                if(config.cinemaCurveYAxis)
                {
                    // Reverse left/right storage to retain the same clockwise
                    // front-face winding used by the horizontal mesh.
                    vertices[bottom] = {halfWidth, curvedAxis, z};
                    vertices[top] = {-halfWidth, curvedAxis, z};
                    uvs[bottom] = {1.0f, 1.0f - u};
                    uvs[top] = {0.0f, 1.0f - u};
                }
                else
                {
                    vertices[bottom] = {curvedAxis, -halfHeight, z};
                    vertices[top] = {curvedAxis, halfHeight, z};
                    uvs[bottom] = {u, 1.0f};
                    uvs[top] = {u, 0.0f};
                }
            }
            else
            {
                const float x = (u - 0.5f) * width;
                // Big Screen layouts retain their signed bow model. An
                // explicit Cinema curvature of zero reaches this path with a
                // flat surface and is never replaced by the user's curve.
                const float edgeShape = CurveEdgeShape(u);
                const float z = cinemaCurve
                    ? 0.0f
                    : -config.screenCurvature * width * 0.12f * edgeShape;
                vertices[bottom] = {x, -halfHeight, z};
                vertices[top] = {x, halfHeight, z};
                // swscale writes rows from the top of the decoded image while
                // Unity's texture UV origin is at the bottom, so V is inverted.
                uvs[bottom] = {u, 1.0f};
                uvs[top] = {u, 0.0f};
            }
        }

        for(int segment = 0; segment < config.screenSegments; ++segment)
        {
            const int leftBottom = segment * 2;
            const int leftTop = leftBottom + 1;
            const int rightBottom = leftBottom + 2;
            const int rightTop = leftBottom + 3;
            const int output = segment * 6;

            // Clockwise winding faces the player when the screen is positioned
            // in front of the gameplay origin and uses its default rotation.
            triangles[output + 0] = leftBottom;
            triangles[output + 1] = leftTop;
            triangles[output + 2] = rightTop;
            triangles[output + 3] = leftBottom;
            triangles[output + 4] = rightTop;
            triangles[output + 5] = rightBottom;
        }

        mesh_ = UnityEngine::Mesh::New_ctor();
        if(!mesh_)
            return false;
        mesh_->set_vertices(vertices);
        mesh_->set_uv(uvs);
        mesh_->set_triangles(triangles);
        mesh_->RecalculateNormals();
        mesh_->RecalculateBounds();
        return true;
    }

    bool ScreenSurface::CreateVideoMesh(
        const MapVideoConfig& config,
        float sourceAspectRatio)
    {
        const float flatFrameWidth = config.screenWidthOverride.value_or(
            config.screenHeight * sourceAspectRatio);
        const float frameWidth = config.maintainAspectRatioWhenCurved &&
                                 std::abs(config.screenCurvature) > 0.0001f
            ? flatFrameWidth * CurvedWidthScale(
                config.screenCurvature, config.screenSegments)
            : flatFrameWidth;
        const float frameHeight = config.screenHeight;
        const auto content = CoreLogic::FitVideoContent(
            frameWidth, frameHeight, sourceAspectRatio,
            config.stretchVideoToFit, config.videoZoom);
        const float contentWidth = content.width;
        const float contentHeight = content.height;

        const float rotation = config.videoRotation * Pi / 180.0f;
        const float tilt = config.videoTilt * Pi / 180.0f;
        const float rotationCos = std::cos(rotation);
        const float rotationSin = std::sin(rotation);
        const float tiltCos = std::cos(tilt);
        const float tiltSin = std::sin(tilt);
        const float panX = config.videoOffsetX * frameWidth * 0.5f;
        const float panY = config.videoOffsetY * frameHeight * 0.5f;

        // Do not render an opaque backing surface when this simple picture
        // already covers every point of the canvas. Besides avoiding needless
        // overdraw, this prevents large and distant menu previews from making
        // the black backing and picture fight for the same depth-buffer value.
        // Rotation, tilt, or pan can expose a corner, so those presentations
        // deliberately retain their configured letterbox background.
        constexpr float CoverageEpsilon = 0.0005f;
        const bool untransformed =
            std::abs(config.videoRotation) <= CoverageEpsilon &&
            std::abs(config.videoTilt) <= CoverageEpsilon &&
            std::abs(panX) <= CoverageEpsilon &&
            std::abs(panY) <= CoverageEpsilon;
        videoCoversFrame_ = untransformed &&
            contentWidth >= frameWidth - CoverageEpsilon &&
            contentHeight >= frameHeight - CoverageEpsilon;

        const int columns = std::max(8, config.screenSegments);
        const int rows = 8;
        const auto transformed = [&](float u, float v)
        {
            const float originalX = (u - 0.5f) * contentWidth;
            const float originalY = (v - 0.5f) * contentHeight;
            const float tiltedY = originalY * tiltCos;
            // Pivot around the farther horizontal edge instead of the center.
            // Both edges therefore remain on or in front of the opaque frame
            // background. A center-pivoted plane placed half its vertices
            // behind that depth-writing layer, which made one half of a
            // strongly tilted video disappear even though the mesh was valid.
            const float normalizedY = std::clamp(
                originalY / std::max(contentHeight, 0.0001f) + 0.5f,
                0.0f, 1.0f);
            const float tiltDepth = std::abs(tiltSin) * contentHeight;
            const float tiltedZ = tiltSin >= 0.0f
                ? -normalizedY * tiltDepth
                : -(1.0f - normalizedY) * tiltDepth;
            return ContentVertex{
                originalX * rotationCos - tiltedY * rotationSin + panX,
                originalX * rotationSin + tiltedY * rotationCos + panY,
                tiltedZ,
                u,
                1.0f - v};
        };

        std::vector<UnityEngine::Vector3> vertices;
        std::vector<UnityEngine::Vector2> uvs;
        std::vector<std::int32_t> triangles;
        std::vector<DeformationBaseVertex> deformationVertices;
        vertices.reserve(columns * rows * 12);
        uvs.reserve(columns * rows * 12);
        triangles.reserve(columns * rows * 12);
        if(prepareDeformation_)
            deformationVertices.reserve(columns * rows * 12);

        const bool cinemaCurve = config.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *config.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        const bool cinemaCurveIsFlat =
            !cinemaCurve || std::abs(cinemaArcRadians) < 0.000001f;
        const float cinemaCurveLength = config.cinemaCurveYAxis
            ? frameHeight : frameWidth;
        const float cinemaRadius = cinemaCurveIsFlat
            ? 0.0f : cinemaCurveLength / cinemaArcRadians;

        const auto mapToSurface = [&](const ContentVertex& value)
        {
            const float videoLayerOffset =
                CoreLogic::VideoLayerOffset(frameWidth, frameHeight);
            if(cinemaCurve && !cinemaCurveIsFlat)
            {
                if(config.cinemaCurveYAxis)
                {
                    const float theta = value.y / frameHeight * cinemaArcRadians;
                    return UnityEngine::Vector3{
                        value.x,
                        std::sin(theta) * cinemaRadius,
                        std::cos(theta) * cinemaRadius - cinemaRadius +
                            value.z + videoLayerOffset};
                }
                const float theta = value.x / frameWidth * cinemaArcRadians;
                return UnityEngine::Vector3{
                    std::sin(theta) * cinemaRadius,
                    value.y,
                    std::cos(theta) * cinemaRadius - cinemaRadius +
                        value.z + videoLayerOffset};
            }

            const float normalizedX = std::clamp(
                value.x / frameWidth + 0.5f, 0.0f, 1.0f);
            const float curveZ = cinemaCurve
                ? 0.0f
                : -config.screenCurvature * frameWidth * 0.12f *
                    CurveEdgeShape(normalizedX);
            return UnityEngine::Vector3{
                value.x,
                value.y,
                curveZ + value.z + videoLayerOffset};
        };

        const auto appendTriangle = [&](const ContentVertex& a,
                                        const ContentVertex& b,
                                        const ContentVertex& c)
        {
            auto polygon = ClipToFrame(
                {a, b, c}, frameWidth * 0.5f, frameHeight * 0.5f);
            if(polygon.size() < 3)
                return;
            const auto base = static_cast<std::int32_t>(vertices.size());
            for(const auto& value : polygon)
            {
                vertices.push_back(mapToSurface(value));
                uvs.push_back({value.u, value.v});
                if(prepareDeformation_)
                {
                    deformationVertices.push_back({
                        value.x,
                        value.y,
                        value.z,
                        std::clamp(value.x / frameWidth + 0.5f, 0.0f, 1.0f),
                        std::clamp(value.y / frameHeight + 0.5f, 0.0f, 1.0f)});
                }
            }
            for(std::size_t index = 1; index + 1 < polygon.size(); ++index)
            {
                triangles.push_back(base);
                triangles.push_back(base + static_cast<std::int32_t>(index));
                triangles.push_back(base + static_cast<std::int32_t>(index + 1));
            }
        };

        for(int row = 0; row < rows; ++row)
        {
            const float v0 = static_cast<float>(row) / rows;
            const float v1 = static_cast<float>(row + 1) / rows;
            for(int column = 0; column < columns; ++column)
            {
                const float u0 = static_cast<float>(column) / columns;
                const float u1 = static_cast<float>(column + 1) / columns;
                const auto bottomLeft = transformed(u0, v0);
                const auto topLeft = transformed(u0, v1);
                const auto topRight = transformed(u1, v1);
                const auto bottomRight = transformed(u1, v0);
                appendTriangle(bottomLeft, topLeft, topRight);
                appendTriangle(bottomLeft, topRight, bottomRight);
            }
        }

        if(vertices.empty())
            return false;
        ArrayW<UnityEngine::Vector3> unityVertices(vertices.size());
        ArrayW<UnityEngine::Vector2> unityUvs(uvs.size());
        ArrayW<std::int32_t> unityTriangles(triangles.size());
        std::copy(vertices.begin(), vertices.end(), unityVertices.begin());
        std::copy(uvs.begin(), uvs.end(), unityUvs.begin());
        std::copy(triangles.begin(), triangles.end(), unityTriangles.begin());

        videoMesh_ = UnityEngine::Mesh::New_ctor();
        if(!videoMesh_)
            return false;
        videoMesh_->set_vertices(unityVertices);
        videoMesh_->set_uv(unityUvs);
        videoMesh_->set_triangles(unityTriangles);
        videoMesh_->RecalculateNormals();
        videoMesh_->RecalculateBounds();

        // Only showcase clones retain managed vertex/UV arrays plus planar
        // coordinates. They are allocated with the mesh and reused for every
        // song-time or real-time sample; normal map screens pay no extra cost.
        if(prepareDeformation_)
        {
            deformationBaseVertices_ = std::move(deformationVertices);
            undeformedVideoVertices_ = unityVertices;
            dynamicVideoVertices_ = ArrayW<UnityEngine::Vector3>(vertices.size());
            std::copy(
                vertices.begin(), vertices.end(), dynamicVideoVertices_.begin());
            undeformedVideoUvs_ = unityUvs;
            dynamicVideoUvs_ = ArrayW<UnityEngine::Vector2>(uvs.size());
            std::copy(uvs.begin(), uvs.end(), dynamicVideoUvs_.begin());
        }
        deformationWasApplied_ = false;
        return true;
    }

    bool ScreenSurface::UpdateGeometry(const MapVideoConfig& config)
    {
        if(!gameObject_ || !videoObject_ ||
           textureWidth_ <= 0 || textureHeight_ <= 0)
            return false;

        auto* filter = gameObject_->GetComponent<UnityEngine::MeshFilter*>();
        auto* videoFilter = videoObject_->GetComponent<UnityEngine::MeshFilter*>();
        auto* alphaGuardFilter = alphaGuardObject_
            ? alphaGuardObject_->GetComponent<UnityEngine::MeshFilter*>()
            : nullptr;
        if(!filter || !videoFilter ||
           (alphaGuardObject_ && !alphaGuardFilter))
            return false;

        const float aspectRatio =
            static_cast<float>(textureWidth_) / textureHeight_;
        auto* previousMesh = mesh_;
        auto* previousVideoMesh = videoMesh_;
        const bool previousVideoCoversFrame = videoCoversFrame_;
        const bool previousOpaqueScreenBody = opaqueScreenBody_;
        mesh_ = nullptr;
        videoMesh_ = nullptr;
        if(!CreateMesh(config, aspectRatio) || !mesh_ ||
           !CreateVideoMesh(config, aspectRatio) || !videoMesh_)
        {
            if(mesh_)
                UnityEngine::Object::Destroy(mesh_);
            if(videoMesh_)
                UnityEngine::Object::Destroy(videoMesh_);
            mesh_ = previousMesh;
            videoMesh_ = previousVideoMesh;
            videoCoversFrame_ = previousVideoCoversFrame;
            opaqueScreenBody_ = previousOpaqueScreenBody;
            return false;
        }

        if((config.letterboxTransparent != letterboxTransparent_ ||
            std::abs(config.videoOpacity - opacity_) > 0.0001f) &&
           !ApplyPresentation(
               config.letterboxTransparent,
               config.videoOpacity))
        {
            UnityEngine::Object::Destroy(mesh_);
            UnityEngine::Object::Destroy(videoMesh_);
            mesh_ = previousMesh;
            videoMesh_ = previousVideoMesh;
            videoCoversFrame_ = previousVideoCoversFrame;
            opaqueScreenBody_ = previousOpaqueScreenBody;
            return false;
        }

        opaqueScreenBody_ = config.opaqueScreenBody;
        if(UnityW<UnityEngine::Material>::isAlive(backgroundMaterial_) &&
           !(leadInActive_ && leadInBlack_))
        {
            backgroundMaterial_->set_color(
                UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f});
        }

        // Publish the complete replacement only after mesh creation succeeds.
        // The previous surface and decoded frame remain visible throughout the
        // operation, preventing the gray flash caused by recreating a screen.
        filter->set_sharedMesh(mesh_);
        videoFilter->set_sharedMesh(videoMesh_);
        if(alphaGuardFilter)
            alphaGuardFilter->set_sharedMesh(videoMesh_);
        if(auto* backgroundRenderer =
               gameObject_->GetComponent<UnityEngine::MeshRenderer*>())
        {
            backgroundRenderer->set_enabled(CoreLogic::ScreenBackgroundVisible(
                    opaqueScreenBody_,
                    letterboxTransparent_,
                    leadInActive_ && leadInBlack_,
                    videoCoversFrame_,
                    textureHasAuthoredAlpha_));
        }
        fractureMeshActive_ = false;
        fractureShapeCaptured_ = false;
        fractureSnapshotActive_ = false;
        if(material_)
            material_->set_mainTexture(texture_);
        if(previousMesh)
            UnityEngine::Object::Destroy(previousMesh);
        if(previousVideoMesh)
            UnityEngine::Object::Destroy(previousVideoMesh);

        auto transform = gameObject_->get_transform();
        transform->set_position({
            config.screenPosition.x,
            config.screenPosition.y,
            config.screenPosition.z});
        transform->set_eulerAngles({
            config.screenRotation.x,
            config.screenRotation.y,
            config.screenRotation.z});

        const float flatWidth = config.screenWidthOverride.value_or(
            config.screenHeight * aspectRatio);
        screenWidth_ = config.maintainAspectRatioWhenCurved &&
                       std::abs(config.screenCurvature) > 0.0001f
            ? flatWidth * CurvedWidthScale(
                config.screenCurvature,
                config.screenSegments)
            : flatWidth;
        screenHeight_ = config.screenHeight;
        geometryConfig_ = config;
        geometryAspectRatio_ = aspectRatio;
        // Live layout edits change the glow footprint without recreating the
        // surface; keep the bloom pre-pass boost in sync with the new size.
        // BLOOM EXPERIMENT DISABLED (2026-08-18): keep the live glow update
        // code available in source without executing it.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
        mapperBloom_ = config.bloomIntensity;
        if(bloomRegistered_)
        {
            CinemaBloomRenderer::Instance().UpdateSource(
                videoObject_,
                mapperBloom_,
                screenWidth_,
                screenHeight_,
                true);
        }
#endif

        // Diagnostics is an overlay inside the lower part of the screen. Its
        // RectTransform is centered on X, so placing the object at the old
        // lower-left coordinate shifted half of the text rectangle outside
        // the canvas. Keep the entire rectangle within the visible mesh.
        if(diagnosticsObject_)
        {
            diagnosticsObject_->get_transform()->set_localPosition({
                0.0f,
                -screenHeight_ * 0.36f,
                -0.08f});
            if(diagnosticsText_)
                diagnosticsText_->get_rectTransform()->set_sizeDelta({
                    screenWidth_ * 11.5f,
                    screenHeight_ * 3.0f});
        }
        return true;
    }

    bool ScreenSurface::ApplyPresentation(
        bool letterboxTransparent,
        float videoOpacity)
    {
        if(!gameObject_ || !videoObject_ || !material_ ||
           !backgroundMaterial_ || !texture_)
            return false;

        const float nextOpacity = std::clamp(videoOpacity, 0.0f, 1.0f);
        const bool pictureTransparent = nextOpacity < 0.999f ||
            textureHasAuthoredAlpha_ || colorBlending_;
        const auto videoShader = ResolveVideoShader(pictureTransparent);
        auto* backgroundShader = FindVideoShader();
        if(!videoShader.shader || !backgroundShader)
            return false;

        material_->set_shader(videoShader.shader);
        backgroundMaterial_->set_shader(backgroundShader);
        videoMaterialUiMasked_ =
            videoShader.family == VideoShaderFamily::UiMasked;
        material_->set_mainTexture(texture_);
        backgroundMaterial_->set_mainTexture(
            UnityEngine::Texture2D::get_whiteTexture());

        ApplyVideoMaterialMode(
            material_,
            videoShader.family,
            colorBlending_,
            textureHasAuthoredAlpha_,
            pictureTransparent,
            nextOpacity);

        // UI/Default cannot write the bloom-weight channel itself. Keep its
        // fixed alpha-clearing companion synchronized with the same texture
        // and user-selected opacity; there is deliberately no bloom slider.
        if(UnityW<UnityEngine::Material>::isAlive(alphaGuardMaterial_))
        {
            alphaGuardMaterial_->set_mainTexture(texture_);
            alphaGuardMaterial_->set_color(UnityEngine::Color{
                1.0f, 1.0f, 1.0f, nextOpacity});
        }

        if(!ConfigureNonEmissiveBackground(
               backgroundMaterial_, letterboxTransparent))
            return false;

        // Changing layouts during a negative offset must not expose the first
        // decoded frame early. Preserve an explicitly requested black lead-in
        // until Upload transitions back to the configured presentation.
        if(leadInActive_ && leadInBlack_)
        {
            if(videoMaterialUiMasked_)
            {
                // See videoMaterialUiMasked_: black tint over the white
                // texture, because the shared black texture's transparent
                // alpha premultiplies UI/Default to an invisible quad.
                material_->set_mainTexture(
                    UnityEngine::Texture2D::get_whiteTexture());
                material_->set_color(
                    UnityEngine::Color{0.0f, 0.0f, 0.0f, 1.0f});
            }
            else
            {
                material_->set_mainTexture(
                    UnityEngine::Texture2D::get_whiteTexture());
                material_->set_color(
                    UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f});
            }
            if(!ConfigureNonEmissiveBackground(backgroundMaterial_, false))
                return false;
            if(UnityW<UnityEngine::Material>::isAlive(alphaGuardMaterial_))
            {
                alphaGuardMaterial_->set_mainTexture(
                    UnityEngine::Texture2D::get_whiteTexture());
                alphaGuardMaterial_->set_color(
                    UnityEngine::Color::get_white());
            }
        }

        letterboxTransparent_ = letterboxTransparent;
        opacity_ = nextOpacity;
        if(auto* backgroundRenderer =
               gameObject_->GetComponent<UnityEngine::MeshRenderer*>())
        {
            backgroundRenderer->set_enabled(CoreLogic::ScreenBackgroundVisible(
                    opaqueScreenBody_,
                    letterboxTransparent_,
                    leadInActive_ && leadInBlack_,
                    videoCoversFrame_,
                    textureHasAuthoredAlpha_));
        }
        return true;
    }

    bool ScreenSurface::CreateBackgroundMaterial(bool letterboxTransparent)
    {
        // The background is an independent surface rather than blank pixels in
        // the decoded image. This lets rotation, zoom, and pan expose either a
        // solid black letterbox or a genuinely transparent opening without
        // modifying a frame on the CPU for every presentation.
        auto* shader = FindVideoShader();
        if(!shader)
            return false;

        backgroundMaterial_ = UnityEngine::Material::New_ctor(shader);
        if(!backgroundMaterial_)
            return false;
        letterboxTransparent_ = letterboxTransparent;
        return ConfigureNonEmissiveBackground(
            backgroundMaterial_, letterboxTransparent);
    }

    bool ScreenSurface::CreateUiAlphaGuard()
    {
        if(!videoMaterialUiMasked_)
            return true;

        auto* shader = FindVideoShader();
        if(!shader)
        {
            // Do not create a screen that can bloom white. The embedded bundle
            // is a packaged runtime requirement even when UI/Default draws the
            // visible picture because this guard enforces zero emission.
            BigScreen::BigScreenLogger.error(
                "UI/Default video alpha guard is unavailable because the "
                "embedded BigScreen/Video shader did not load");
            return false;
        }

        alphaGuardMaterial_ = UnityEngine::Material::New_ctor(shader);
        alphaGuardObject_ = UnityEngine::GameObject::New_ctor(
            "Big Screen Video Alpha Guard");
        if(!alphaGuardMaterial_ || !alphaGuardObject_)
            return false;

        alphaGuardMaterial_->set_mainTexture(texture_);
        alphaGuardMaterial_->set_color(UnityEngine::Color{
            1.0f,
            1.0f,
            1.0f,
            opacity_});
        // Preserve RGB and attenuate framebuffer alpha only where the video
        // is visible. Opaque video pixels clear bloom emission; transparent
        // opacity/vignette pixels retain the corresponding amount behind the
        // screen. This is fixed behavior and is no longer slider-controlled.
        ConfigureVideoBlend(
            alphaGuardMaterial_, 0, 1, 0, 10, false, 3001);
        alphaGuardMaterial_->SetInt("_ColorMask", 8); // Alpha only.

        alphaGuardObject_->set_layer(videoObject_->get_layer());
        alphaGuardObject_->get_transform()->SetParent(
            videoObject_->get_transform(), false);
        auto* filter =
            alphaGuardObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* renderer =
            alphaGuardObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!filter || !renderer)
            return false;
        filter->set_sharedMesh(videoMesh_);
        renderer->set_material(alphaGuardMaterial_);
        return true;
    }

    bool ScreenSurface::CreateMaterialAndTexture(
        int width,
        int height,
        const MapVideoConfig& config,
        UnityEngine::Texture* sharedTexture,
        bool preferGpuConversion)
    {
        // Unity's transparent unlit shader performs normal alpha blending, so
        // Beat Saber lights and background geometry remain visible through the
        // video without changing the decoded pixels.
        //
        // Configure both modes completely rather than relying on shader
        // defaults. In particular, the opaque mode must write to the depth
        // buffer so scenery and light geometry physically behind the screen
        // cannot be drawn through it.
        const float nextOpacity = std::clamp(config.videoOpacity, 0.0f, 1.0f);
        opaqueScreenBody_ = config.opaqueScreenBody;
        textureHasAuthoredAlpha_ = config.vignette.has_value();
        // Cinema's soft-additive blending is applied ONLY when the map file
        // explicitly sets "colorBlending": true. It must never be inferred
        // from other mapper presentation fields: any map that merely places
        // the screen (including Big Screen's own showcase) would otherwise
        // get see-through additive screens with no solid body, overriding
        // the player's own opacity settings. When the map does not set the
        // field, the mod's configured presentation wins.
        // BLOOM EXPERIMENT DISABLED (2026-08-18): retain the parsed Cinema
        // value for future compatibility work, but do not let it change the
        // visible material into a soft-additive surface.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
        colorBlending_ = config.colorBlending.value_or(false);
#else
        colorBlending_ = false;
#endif
        const bool pictureTransparent = nextOpacity < 0.999f ||
            textureHasAuthoredAlpha_ || colorBlending_;
        const auto videoShader = ResolveVideoShader(pictureTransparent);
        if(!videoShader.shader)
            return false;

        material_ = UnityEngine::Material::New_ctor(videoShader.shader);
        texture_ = sharedTexture;
        ownsTexture_ = sharedTexture == nullptr;
        gpuConversionRequested_ = ownsTexture_ && preferGpuConversion;
        gpuConversionActive_ = false;
        if(ownsTexture_)
        {
            if(gpuConversionRequested_)
                gpuConversionActive_ = CreateGpuConversionResources(width, height);
            if(!gpuConversionActive_)
            {
                rgbaTexture_ = UnityEngine::Texture2D::New_ctor(
                    width,
                    height,
                    UnityEngine::TextureFormat::RGBA32,
                    false,
                    false);
                texture_ = rgbaTexture_;
            }
        }
        if(!material_ || !texture_)
            return false;

        textureWidth_ = width;
        textureHeight_ = height;
        opacity_ = nextOpacity;
        videoMaterialUiMasked_ =
            videoShader.family == VideoShaderFamily::UiMasked;
        material_->set_mainTexture(texture_);
        ApplyVideoMaterialMode(
            material_,
            videoShader.family,
            colorBlending_,
            textureHasAuthoredAlpha_,
            pictureTransparent,
            opacity_);
        return true;
    }

    bool ScreenSurface::CreateGpuConversionResources(int width, int height)
    {
        gpuTexture_ = UnityEngine::RenderTexture::New_ctor(
            width,
            height,
            0,
            UnityEngine::RenderTextureFormat::ARGB32);
        if(!gpuTexture_ || !gpuTexture_->Create())
        {
            BigScreen::BigScreenLogger.warn(
                "Unity could not create the GPU video conversion resources; using CPU RGBA conversion");
            if(UnityW<UnityEngine::RenderTexture>::isAlive(gpuTexture_))
            {
                if(gpuTexture_->IsCreated())
                    gpuTexture_->Release();
                DestroyIfAlive(gpuTexture_);
            }
            return false;
        }
        gpuTexture_->set_filterMode(UnityEngine::FilterMode::Bilinear);
        gpuTexture_->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);

        const auto requestedLayout =
            Settings::Instance().ConsolidatedYuvUploadEnabled()
                ? GpuYuvUploadLayout::PackedAtlas
                : GpuYuvUploadLayout::ThreePlane;
        gpuYuvUploadLayout_ = requestedLayout;
        if(!CreateGpuConversionMaterial(requestedLayout))
        {
            if(requestedLayout != GpuYuvUploadLayout::PackedAtlas ||
               !FallbackToThreePlaneYuv(
                   "the packed YUV conversion shader was unavailable"))
            {
                BigScreen::BigScreenLogger.warn(
                    "GPU video conversion was requested, but its shader was unavailable; using CPU RGBA conversion");
                if(gpuTexture_->IsCreated())
                    gpuTexture_->Release();
                DestroyIfAlive(gpuTexture_);
                return false;
            }
        }
        texture_ = gpuTexture_;
        BigScreen::BigScreenLogger.info(
            "Experimental GPU video conversion prepared at {}x{} using {}",
            width,
            height,
            gpuYuvUploadLayout_ == GpuYuvUploadLayout::PackedAtlas
                ? "one packed YUV upload"
                : "three YUV plane uploads");
        return true;
    }

    bool ScreenSurface::CreateGpuConversionMaterial(
        GpuYuvUploadLayout layout)
    {
        auto* shader = layout == GpuYuvUploadLayout::PackedAtlas
            ? FindPackedYuvConversionShader()
            : FindYuvConversionShader();
        if(!shader)
            return false;
        auto* material = UnityEngine::Material::New_ctor(shader);
        if(!material)
            return false;
        DestroyIfAlive(gpuConversionMaterial_);
        gpuConversionMaterial_ = material;
        gpuYuvUploadLayout_ = layout;
        return true;
    }

    bool ScreenSurface::FallbackToThreePlaneYuv(std::string reason)
    {
        if(gpuYuvUploadLayout_ != GpuYuvUploadLayout::PackedAtlas)
            return false;
        DestroyIfAlive(packedYuvTexture_);
        if(!CreateGpuConversionMaterial(GpuYuvUploadLayout::ThreePlane))
            return false;
        gpuYuvUploadFallback_ = std::move(reason);
        BigScreen::BigScreenLogger.warn(
            "Packed GPU YUV upload fell back to the 3-plane method: {}",
            *gpuYuvUploadFallback_);
        return true;
    }

    std::optional<std::string> ScreenSurface::TakeGpuYuvUploadFallback()
    {
        auto result = std::move(gpuYuvUploadFallback_);
        gpuYuvUploadFallback_.reset();
        return result;
    }

    bool ScreenSurface::Upload(const VideoFrame& frame)
    {
        // Restarting a Beat Saber level destroys GameCore's Unity objects
        // without necessarily reaching Big Screen's normal Finish hook. A raw
        // Texture2D pointer can consequently remain non-null while referring
        // to an already-destroyed object. Never call LoadRawTextureData through
        // such a pointer: on IL2CPP that can jump through stale class metadata
        // and crash UnityMain rather than producing a managed exception.
        if(!UnityW<UnityEngine::GameObject>::isAlive(gameObject_) ||
           !UnityW<UnityEngine::Material>::isAlive(material_) ||
           !UnityW<UnityEngine::Texture>::isAlive(texture_) ||
           frame.width != textureWidth_ ||
           frame.height != textureHeight_)
        {
            return false;
        }

        // Only a lead-in changes these invariant material properties. Avoid
        // sending identical texture/color updates through IL2CPP every frame.
        if(leadInActive_)
        {
            // Clear the lead-in state before rebuilding presentation. The old
            // code wrote {1,1,1,opacity} directly here, which replaced the
            // embedded shader's fixed zero bloom-emission alpha on the first
            // decoded frame. Bloom-heavy maps consequently turned solid white
            // until moving a diagnostic slider forced another rebuild.
            leadInActive_ = false;
            leadInBlack_ = false;
            if(!ApplyPresentation(letterboxTransparent_, opacity_))
                return false;
            material_->set_mainTexture(texture_);
        }

        if(frame.storage == VideoFrameStorage::Yuv420PackedAtlas)
            return UploadPackedYuv420(frame);
        if(frame.storage == VideoFrameStorage::Yuv420Planar)
            return UploadYuv420(frame);
        return UploadRgba(frame);
    }

    bool ScreenSurface::UploadRgba(const VideoFrame& frame)
    {
        if(frame.rgba.empty())
            return false;
        if(!UnityW<UnityEngine::Texture2D>::isAlive(rgbaTexture_))
        {
            rgbaTexture_ = UnityEngine::Texture2D::New_ctor(
                frame.width,
                frame.height,
                UnityEngine::TextureFormat::RGBA32,
                false,
                false);
        }
        if(!UnityW<UnityEngine::Texture2D>::isAlive(rgbaTexture_))
            return false;

        rgbaTexture_->LoadRawTextureData(
            System::IntPtr(const_cast<std::uint8_t*>(frame.rgba.data())),
            static_cast<std::int32_t>(frame.rgba.size()));
        rgbaTexture_->Apply(false, false);
        if(gpuConversionActive_)
        {
            if(!UnityW<UnityEngine::RenderTexture>::isAlive(gpuTexture_))
                return false;
            // A decoder can permanently fall back to RGBA when MediaCodec
            // changes to a pixel layout outside the supported 8-bit 4:2:0
            // transport. Preserve the shared RenderTexture identity so every
            // showcase/Cinema clone continues referencing the same picture.
            UnityEngine::Graphics::Blit(rgbaTexture_, gpuTexture_);
        }
        return true;
    }

    bool ScreenSurface::UploadYuv420(const VideoFrame& frame)
    {
        if(gpuYuvUploadLayout_ == GpuYuvUploadLayout::PackedAtlas &&
           !FallbackToThreePlaneYuv(
               "the decoder selected the compatible planar layout"))
            return false;
        if(!gpuConversionActive_ ||
           !UnityW<UnityEngine::RenderTexture>::isAlive(gpuTexture_) ||
           !UnityW<UnityEngine::Material>::isAlive(gpuConversionMaterial_) ||
           frame.sourceWidth <= 0 || frame.sourceHeight <= 0)
            return false;

        const int chromaWidth = (frame.sourceWidth + 1) / 2;
        const int chromaHeight = (frame.sourceHeight + 1) / 2;
        const auto yBytes = static_cast<std::size_t>(
            frame.sourceWidth) * frame.sourceHeight;
        const auto chromaBytes = static_cast<std::size_t>(
            chromaWidth) * chromaHeight;
        if(frame.y.size() != yBytes || frame.u.size() != chromaBytes ||
           frame.v.size() != chromaBytes)
            return false;

        const auto ensurePlane = [](UnityEngine::Texture2D*& texture,
                                    int width,
                                    int height)
        {
            if(UnityW<UnityEngine::Texture2D>::isAlive(texture) &&
               texture->get_width() == width &&
               texture->get_height() == height)
                return true;
            DestroyIfAlive(texture);
            texture = UnityEngine::Texture2D::New_ctor(
                width,
                height,
                UnityEngine::TextureFormat::R8,
                false,
                true);
            if(!texture)
                return false;
            texture->set_filterMode(UnityEngine::FilterMode::Bilinear);
            texture->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
            return true;
        };
        if(!ensurePlane(yTexture_, frame.sourceWidth, frame.sourceHeight) ||
           !ensurePlane(uTexture_, chromaWidth, chromaHeight) ||
           !ensurePlane(vTexture_, chromaWidth, chromaHeight))
            return false;

        const auto uploadPlane = [](UnityEngine::Texture2D* texture,
                                    const std::vector<std::uint8_t>& bytes)
        {
            texture->LoadRawTextureData(
                System::IntPtr(const_cast<std::uint8_t*>(bytes.data())),
                static_cast<std::int32_t>(bytes.size()));
            texture->Apply(false, false);
        };
        uploadPlane(yTexture_, frame.y);
        uploadPlane(uTexture_, frame.u);
        uploadPlane(vTexture_, frame.v);
        gpuConversionMaterial_->SetTexture("_PlaneU", uTexture_);
        gpuConversionMaterial_->SetTexture("_PlaneV", vTexture_);
        ConfigureGpuConversionMaterial(frame);
        UnityEngine::Graphics::Blit(
            yTexture_, gpuTexture_, gpuConversionMaterial_);
        return true;
    }

    bool ScreenSurface::UploadPackedYuv420(const VideoFrame& frame)
    {
        if(!gpuConversionActive_ ||
           !UnityW<UnityEngine::RenderTexture>::isAlive(gpuTexture_) ||
           !UnityW<UnityEngine::Material>::isAlive(gpuConversionMaterial_) ||
           gpuYuvUploadLayout_ != GpuYuvUploadLayout::PackedAtlas ||
           frame.sourceWidth <= 0 || frame.sourceHeight <= 0)
            return false;

        const int chromaWidth = (frame.sourceWidth + 1) / 2;
        const int chromaHeight = (frame.sourceHeight + 1) / 2;
        const int atlasWidth = std::max(frame.sourceWidth, chromaWidth * 2);
        const int atlasHeight = frame.sourceHeight + chromaHeight;
        const auto expectedBytes = static_cast<std::size_t>(atlasWidth) *
            atlasHeight;
        if(frame.packedYuv.size() != expectedBytes)
        {
            FallbackToThreePlaneYuv(
                "the decoder returned an invalid packed-atlas size");
            return false;
        }

        if(!UnityW<UnityEngine::Texture2D>::isAlive(packedYuvTexture_) ||
           packedYuvTexture_->get_width() != atlasWidth ||
           packedYuvTexture_->get_height() != atlasHeight)
        {
            DestroyIfAlive(packedYuvTexture_);
            packedYuvTexture_ = UnityEngine::Texture2D::New_ctor(
                atlasWidth,
                atlasHeight,
                UnityEngine::TextureFormat::R8,
                false,
                true);
            if(!packedYuvTexture_)
            {
                FallbackToThreePlaneYuv(
                    "Unity could not allocate the packed YUV texture");
                return false;
            }
            packedYuvTexture_->set_filterMode(
                UnityEngine::FilterMode::Bilinear);
            packedYuvTexture_->set_wrapMode(
                UnityEngine::TextureWrapMode::Clamp);
        }

        packedYuvTexture_->LoadRawTextureData(
            System::IntPtr(
                const_cast<std::uint8_t*>(frame.packedYuv.data())),
            static_cast<std::int32_t>(frame.packedYuv.size()));
        packedYuvTexture_->Apply(false, false);
        gpuConversionMaterial_->SetVector(
            "_PackedLayout",
            {static_cast<float>(atlasWidth),
             static_cast<float>(atlasHeight),
             static_cast<float>(frame.sourceWidth),
             static_cast<float>(frame.sourceHeight)});
        gpuConversionMaterial_->SetVector(
            "_PackedChromaSize",
            {static_cast<float>(chromaWidth),
             static_cast<float>(chromaHeight),
             0.0f,
             0.0f});
        ConfigureGpuConversionMaterial(frame);
        UnityEngine::Graphics::Blit(
            packedYuvTexture_, gpuTexture_, gpuConversionMaterial_);
        return true;
    }

    void ScreenSurface::ConfigureGpuConversionMaterial(
        const VideoFrame& frame)
    {
        float kr = 0.299f;
        float kb = 0.114f;
        switch(frame.colorMatrix)
        {
            case VideoColorMatrix::Bt709: kr = 0.2126f; kb = 0.0722f; break;
            case VideoColorMatrix::Fcc: kr = 0.30f; kb = 0.11f; break;
            case VideoColorMatrix::Smpte240: kr = 0.212f; kb = 0.087f; break;
            case VideoColorMatrix::Bt2020: kr = 0.2627f; kb = 0.0593f; break;
            case VideoColorMatrix::Bt601: break;
        }
        const float kg = 1.0f - kr - kb;
        const float yScale = frame.fullRange ? 1.0f : 255.0f / 219.0f;
        const float cScale = frame.fullRange ? 1.0f : 255.0f / 224.0f;
        gpuConversionMaterial_->SetVector(
            "_YuvOffset",
            {frame.fullRange ? 0.0f : -16.0f / 255.0f,
             -128.0f / 255.0f,
             -128.0f / 255.0f,
             0.0f});
        gpuConversionMaterial_->SetVector(
            "_YuvRow0",
            {yScale, 0.0f, 2.0f * (1.0f - kr) * cScale, 0.0f});
        gpuConversionMaterial_->SetVector(
            "_YuvRow1",
            {yScale,
             -2.0f * kb * (1.0f - kb) / kg * cScale,
             -2.0f * kr * (1.0f - kr) / kg * cScale,
             0.0f});
        gpuConversionMaterial_->SetVector(
            "_YuvRow2",
            {yScale, 2.0f * (1.0f - kb) * cScale, 0.0f, 0.0f});
        gpuConversionMaterial_->SetFloat(
            "_QuarterTurns",
            static_cast<float>(frame.displayQuarterTurns));

        const auto& effects = frame.visualEffects;
        constexpr float EffectEpsilon = 0.0001f;
        const bool colorCorrection = effects.enabled && (
            std::abs(effects.brightness - 1.0f) > EffectEpsilon ||
            std::abs(effects.contrast - 1.0f) > EffectEpsilon ||
            std::abs(effects.saturation - 1.0f) > EffectEpsilon ||
            std::abs(effects.hue) > EffectEpsilon ||
            std::abs(effects.exposure - 1.0f) > EffectEpsilon ||
            std::abs(effects.gamma - 1.0f) > EffectEpsilon);
        constexpr float Luma[3] = {0.299f, 0.587f, 0.114f};
        const float hueRadians = effects.hue * Pi / 180.0f;
        const float cosine = std::cos(hueRadians);
        const float sine = std::sin(hueRadians);
        const float hue[3][3] = {
            {Luma[0] + (1-Luma[0])*cosine - Luma[0]*sine,
             Luma[1] - Luma[1]*cosine - Luma[1]*sine,
             Luma[2] - Luma[2]*cosine + (1-Luma[2])*sine},
            {Luma[0] - Luma[0]*cosine + 0.143f*sine,
             Luma[1] + (1-Luma[1])*cosine + 0.140f*sine,
             Luma[2] - Luma[2]*cosine - 0.283f*sine},
            {Luma[0] - Luma[0]*cosine - (1-Luma[0])*sine,
             Luma[1] - Luma[1]*cosine + Luma[1]*sine,
             Luma[2] + (1-Luma[2])*cosine + Luma[2]*sine}};
        float combined[3][3]{};
        for(int output = 0; output < 3; ++output)
        {
            for(int input = 0; input < 3; ++input)
            {
                for(int intermediate = 0; intermediate < 3; ++intermediate)
                {
                    const float saturation =
                        (output == intermediate ? effects.saturation : 0.0f) +
                        (1.0f - effects.saturation) * Luma[intermediate];
                    combined[output][input] +=
                        saturation * hue[intermediate][input];
                }
            }
        }
        const float gain = effects.brightness * effects.exposure *
            effects.contrast;
        const float contrastBias = 0.5f * (1.0f - effects.contrast);
        float biases[3]{};
        for(int output = 0; output < 3; ++output)
        {
            for(int input = 0; input < 3; ++input)
            {
                biases[output] += combined[output][input] * contrastBias;
                combined[output][input] *= gain;
            }
            const auto property = output == 0 ? "_ColorRow0" :
                output == 1 ? "_ColorRow1" : "_ColorRow2";
            gpuConversionMaterial_->SetVector(
                property,
                {combined[output][0], combined[output][1],
                 combined[output][2], 0.0f});
        }
        gpuConversionMaterial_->SetVector(
            "_ColorBias", {biases[0], biases[1], biases[2], 0.0f});
        gpuConversionMaterial_->SetFloat(
            "_ColorCorrectionEnabled", colorCorrection ? 1.0f : 0.0f);
        gpuConversionMaterial_->SetFloat(
            "_InverseGamma",
            1.0f / std::max(effects.gamma, 0.00001f));
        gpuConversionMaterial_->SetFloat(
            "_VignetteEnabled",
            effects.enabled && effects.vignetteEnabled ? 1.0f : 0.0f);
        gpuConversionMaterial_->SetFloat(
            "_VignetteElliptical",
            effects.vignetteElliptical ? 1.0f : 0.0f);
        gpuConversionMaterial_->SetFloat(
            "_VignetteRadius", effects.vignetteRadius);
        gpuConversionMaterial_->SetFloat(
            "_VignetteSoftness", effects.vignetteSoftness);
    }

    void ScreenSurface::ShowLeadIn(bool black)
    {
        if(!black)
        {
            leadInActive_ = true;
            leadInBlack_ = false;
            SetVisible(false);
            return;
        }
        if(!UnityW<UnityEngine::GameObject>::isAlive(gameObject_) ||
           !UnityW<UnityEngine::Material>::isAlive(material_))
            return;

        if(leadInActive_ && leadInBlack_)
        {
            SetVisible(true);
            return;
        }

        // Unity owns these shared small textures, so they cost no per-video
        // upload or allocation and must not be destroyed with the surface.
        if(videoMaterialUiMasked_)
        {
            // See videoMaterialUiMasked_: black tint over the white texture,
            // because the shared black texture's transparent alpha
            // premultiplies UI/Default to an invisible quad.
            material_->set_mainTexture(
                UnityEngine::Texture2D::get_whiteTexture());
            material_->set_color(UnityEngine::Color{0.0f, 0.0f, 0.0f, 1.0f});
        }
        else
        {
            material_->set_mainTexture(
                UnityEngine::Texture2D::get_whiteTexture());
            material_->set_color(
                UnityEngine::Color{0.0f, 0.0f, 0.0f, 0.0f});
        }
        // Lead-In Background describes the complete frame, not just the
        // transformed/cropped video polygon. Make the independent letterbox
        // layer opaque until Upload restores its configured transparency.
        if(UnityW<UnityEngine::Material>::isAlive(backgroundMaterial_) &&
           !ConfigureNonEmissiveBackground(backgroundMaterial_, false))
            return;
        if(UnityW<UnityEngine::Material>::isAlive(alphaGuardMaterial_))
        {
            alphaGuardMaterial_->set_mainTexture(
                UnityEngine::Texture2D::get_whiteTexture());
            alphaGuardMaterial_->set_color(UnityEngine::Color::get_white());
        }
        if(auto* backgroundRenderer =
               gameObject_->GetComponent<UnityEngine::MeshRenderer*>())
            backgroundRenderer->set_enabled(true);
        leadInActive_ = true;
        leadInBlack_ = true;
        SetVisible(true);
    }

    void ScreenSurface::SetVisible(bool visible)
    {
        if(!UnityW<UnityEngine::GameObject>::isAlive(gameObject_))
        {
            visible_ = false;
            return;
        }
        if(visible_ == visible)
            return;
        gameObject_->SetActive(visible);
        visible_ = visible;
    }

    void ScreenSurface::SetWorldTransform(
        UnityEngine::Vector3 position,
        UnityEngine::Quaternion rotation)
    {
        if(gameObject_)
            gameObject_->get_transform()->SetPositionAndRotation(position, rotation);
    }

    void ScreenSurface::SetWorldScale(UnityEngine::Vector3 scale)
    {
        if(gameObject_)
            gameObject_->get_transform()->set_localScale(scale);
    }

    void ScreenSurface::SetVideoLocalRoll(float degrees)
    {
        if(videoObject_)
            videoObject_->get_transform()->set_localEulerAngles({0.0f, 0.0f, degrees});
    }

    bool ScreenSurface::SetOpacity(float opacity)
    {
        if(!material_)
            return false;
        const float nextOpacity = std::clamp(opacity, 0.0f, 1.0f);
        // Most cues hold opacity steady for many seconds. Avoid repeating the
        // same IL2CPP material write on every Unity update for every panel.
        if(std::abs(nextOpacity - opacity_) < 0.0001f)
            return true;
        // Showcase cues can animate through the opaque boundary. Reconfigure
        // the shader and blend/depth state, not only its color alpha, so the
        // transition remains correct with both Unity unlit shader variants.
        return ApplyPresentation(letterboxTransparent_, nextOpacity);
    }

    void ScreenSurface::SetDoubleSided(bool enabled)
    {
        // Unity's built-in unlit shaders honor the conventional _Cull
        // material property: 0=None and 2=Back. This changes rasterization
        // only; it does not duplicate geometry, textures, or decoder work.
        const int cullMode = enabled ? 0 : 2;
        if(material_)
            material_->SetInt("_Cull", cullMode);
        if(backgroundMaterial_)
            backgroundMaterial_->SetInt("_Cull", cullMode);
        if(alphaGuardMaterial_)
            alphaGuardMaterial_->SetInt("_Cull", cullMode);
    }

    bool ScreenSurface::SetDeformation(
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!videoMesh_ || deformationBaseVertices_.empty() ||
           !undeformedVideoVertices_ || !dynamicVideoVertices_ ||
           !undeformedVideoUvs_ || !dynamicVideoUvs_)
            return false;

        if(!deformation.enabled)
        {
            if(!deformationWasApplied_)
                return true;
            std::copy(
                undeformedVideoVertices_.begin(),
                undeformedVideoVertices_.end(),
                dynamicVideoVertices_.begin());
            std::copy(
                undeformedVideoUvs_.begin(),
                undeformedVideoUvs_.end(),
                dynamicVideoUvs_.begin());
            videoMesh_->set_vertices(dynamicVideoVertices_);
            videoMesh_->set_uv(dynamicVideoUvs_);
            videoMesh_->RecalculateBounds();
            deformationWasApplied_ = false;
            return true;
        }

        const float frameWidth = std::max(screenWidth_, 0.0001f);
        const float frameHeight = std::max(screenHeight_, 0.0001f);
        const bool cinemaCurve = geometryConfig_.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *geometryConfig_.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        const bool cinemaCurveIsFlat =
            !cinemaCurve || std::abs(cinemaArcRadians) < 0.000001f;
        const float cinemaCurveLength = geometryConfig_.cinemaCurveYAxis
            ? frameHeight : frameWidth;
        const float cinemaRadius = cinemaCurveIsFlat
            ? 0.0f : cinemaCurveLength / cinemaArcRadians;
        const double waveClock = deformation.wave.clock ==
                CoreLogic::DeformationClock::RealTime
            ? realTimeSeconds : songTimeSeconds;

        const float videoLayerOffset =
            CoreLogic::VideoLayerOffset(frameWidth, frameHeight);
        for(std::size_t index = 0; index < deformationBaseVertices_.size(); ++index)
        {
            const auto& base = deformationBaseVertices_[index];
            const auto corner = CoreLogic::BilinearCornerOffset(
                deformation.cornerWarp, base.normalizedU, base.normalizedV);
            float x = base.x + corner.x;
            float y = base.y + corner.y;
            float z = base.z + corner.z;

            // Composition order is deliberate: normalized grid, corner warp,
            // the layout's existing curvature, and finally the flag wave.
            if(cinemaCurve && !cinemaCurveIsFlat)
            {
                if(geometryConfig_.cinemaCurveYAxis)
                {
                    const float theta = y / frameHeight * cinemaArcRadians;
                    y = std::sin(theta) * cinemaRadius;
                    z += std::cos(theta) * cinemaRadius - cinemaRadius;
                }
                else
                {
                    const float theta = x / frameWidth * cinemaArcRadians;
                    x = std::sin(theta) * cinemaRadius;
                    z += std::cos(theta) * cinemaRadius - cinemaRadius;
                }
            }
            else if(!cinemaCurve)
            {
                const float normalizedX = std::clamp(
                    x / frameWidth + 0.5f, 0.0f, 1.0f);
                z += -geometryConfig_.screenCurvature * frameWidth * 0.12f *
                    CurveEdgeShape(normalizedX);
            }

            const auto wave = CoreLogic::FlagWaveOffset(
                deformation.wave, base.normalizedU, waveClock);
            dynamicVideoVertices_[index] = {
                x + wave.x,
                y + wave.y,
                z + wave.z + videoLayerOffset};
        }

        const float cover = CoreLogic::DeformationAutoCoverScale(
            frameWidth, frameHeight, deformation);
        for(std::size_t index = 0; index < undeformedVideoUvs_.size(); ++index)
        {
            const auto uv = undeformedVideoUvs_[index];
            dynamicVideoUvs_[index] = {
                std::clamp(0.5f + (uv.x - 0.5f) / cover, 0.0f, 1.0f),
                std::clamp(0.5f + (uv.y - 0.5f) / cover, 0.0f, 1.0f)};
        }
        videoMesh_->set_vertices(dynamicVideoVertices_);
        videoMesh_->set_uv(dynamicVideoUvs_);
        // Normals are irrelevant to the unlit video material. Recalculating
        // only bounds keeps Unity culling correct without avoidable CPU work.
        videoMesh_->RecalculateBounds();
        deformationWasApplied_ = true;
        return true;
    }

    UnityEngine::Vector3 ScreenSurface::MapFracturePoint(
        CoreLogic::FracturePoint point,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds) const
    {
        const float frameWidth = std::max(screenWidth_, 0.0001f);
        const float frameHeight = std::max(screenHeight_, 0.0001f);
        const auto corner = CoreLogic::BilinearCornerOffset(
            deformation.cornerWarp, point.x, point.y);
        float x = (point.x - 0.5f) * frameWidth + corner.x;
        float y = (point.y - 0.5f) * frameHeight + corner.y;
        float z = corner.z;

        const bool cinemaCurve = geometryConfig_.cinemaCurvatureDegrees.has_value();
        const float cinemaArcRadians = cinemaCurve
            ? *geometryConfig_.cinemaCurvatureDegrees * Pi / 180.0f : 0.0f;
        if(cinemaCurve && std::abs(cinemaArcRadians) >= 0.000001f)
        {
            const float curveLength = geometryConfig_.cinemaCurveYAxis
                ? frameHeight : frameWidth;
            const float radius = curveLength / cinemaArcRadians;
            if(geometryConfig_.cinemaCurveYAxis)
            {
                const float theta = y / frameHeight * cinemaArcRadians;
                y = std::sin(theta) * radius;
                z += std::cos(theta) * radius - radius;
            }
            else
            {
                const float theta = x / frameWidth * cinemaArcRadians;
                x = std::sin(theta) * radius;
                z += std::cos(theta) * radius - radius;
            }
        }
        else if(!cinemaCurve)
        {
            z += -geometryConfig_.screenCurvature * frameWidth * 0.12f *
                CurveEdgeShape(point.x);
        }

        const double waveClock = deformation.wave.clock ==
                CoreLogic::DeformationClock::RealTime
            ? realTimeSeconds : songTimeSeconds;
        const auto wave = CoreLogic::FlagWaveOffset(
            deformation.wave, point.x, waveClock);
        return {
            x + wave.x,
            y + wave.y,
            z + wave.z + CoreLogic::VideoLayerOffset(frameWidth, frameHeight)};
    }

    void ScreenSurface::RestoreWholeVideoMesh()
    {
        // Unity destroys gameplay-scene objects before every Big Screen owner
        // necessarily receives its final Stop()/Destroy() call. Raw IL2CPP
        // pointers remain non-null after that native destruction, so testing
        // only the pointer value is unsafe: calling GetComponent on that
        // "fake-null" object raises a StackTraceException which a BSML event
        // delegate turns into a process abort. Treat an already-destroyed
        // surface as already restored and only touch live Unity objects.
        if(UnityW<UnityEngine::GameObject>::isAlive(videoObject_) &&
           UnityW<UnityEngine::Mesh>::isAlive(videoMesh_))
        {
            if(auto* filter = videoObject_->GetComponent<UnityEngine::MeshFilter*>())
                filter->set_sharedMesh(videoMesh_);
            if(UnityW<UnityEngine::GameObject>::isAlive(alphaGuardObject_))
            {
                if(auto* guardFilter = alphaGuardObject_->GetComponent<
                       UnityEngine::MeshFilter*>())
                    guardFilter->set_sharedMesh(videoMesh_);
            }
        }
        if(UnityW<UnityEngine::Material>::isAlive(material_) &&
           UnityW<UnityEngine::Texture>::isAlive(texture_))
            material_->set_mainTexture(texture_);
        if(UnityW<UnityEngine::GameObject>::isAlive(crackObject_))
            crackObject_->SetActive(false);
        fractureMeshActive_ = false;
        fractureShapeCaptured_ = false;
        fractureSnapshotActive_ = false;
    }

    void ScreenSurface::DestroyFractureResources()
    {
        RestoreWholeVideoMesh();
        DestroyIfAlive(crackObject_);
        DestroyIfAlive(crackMesh_);
        DestroyIfAlive(crackMaterial_);
        DestroyIfAlive(crackTexture_);
        DestroyIfAlive(fractureMesh_);
        DestroyIfAlive(fractureSnapshot_);
        fracturePattern_ = {};
        fractureRevealGroups_.clear();
        fractureVertexMetadata_.clear();
        fractureShardCenters_.clear();
        fractureShardBaseCenters_.clear();
        fractureShardTranslations_.clear();
        fractureShardRotations_.clear();
        fractureShardScales_.clear();
        fractureBaseVertices_ = nullptr;
        dynamicFractureVertices_ = nullptr;
        fractureUvs_ = nullptr;
        dynamicCrackVertices_ = nullptr;
        fracturePrepared_ = false;
        preparedFractureImpactCount_ = 0;
    }

    bool ScreenSurface::PrepareFracture(
        const CoreLogic::FractureEffectSettings& fracture)
    {
        const auto& requested = fracture.pattern;
        // The public programmatic struct intentionally uses a fixed impact
        // array so a future mapper/API wrapper cannot make setup unbounded.
        // Clamp the accompanying count at this final consumer boundary as
        // well. Include both source and cache capacities even though their
        // types currently match, so a future data-model change cannot turn
        // this copy back into an out-of-bounds UnityMain write.
        const std::size_t impactCount = std::min({
            fracture.impactCount,
            fracture.impacts.size(),
            preparedFractureImpacts_.size()});
        bool sameConfiguration = fracturePrepared_ &&
            requested.seed == preparedFractureSettings_.seed &&
            requested.pieceCount == preparedFractureSettings_.pieceCount &&
            requested.spokeCount == preparedFractureSettings_.spokeCount &&
            requested.ringCount == preparedFractureSettings_.ringCount &&
            requested.jitter == preparedFractureSettings_.jitter &&
            requested.impactPoint.x == preparedFractureSettings_.impactPoint.x &&
            requested.impactPoint.y == preparedFractureSettings_.impactPoint.y &&
            impactCount == preparedFractureImpactCount_;
        for(std::size_t index = 0;
            sameConfiguration && index < impactCount; ++index)
        {
            sameConfiguration = CoreLogic::SameFracturePoint(
                fracture.impacts[index], preparedFractureImpacts_[index],
                0.000001f);
        }
        if(sameConfiguration)
            return true;

        DestroyFractureResources();
        if(!prepareDeformation_ || !videoObject_ || !material_ || !texture_)
            return false;

        fracturePattern_ = CoreLogic::GenerateFracturePattern(requested);
        if(fracturePattern_.cells.empty() || fracturePattern_.edges.empty())
            return false;
        std::vector<CoreLogic::FracturePoint> impacts;
        impacts.reserve(impactCount);
        for(std::size_t index = 0; index < impactCount; ++index)
            impacts.push_back(fracture.impacts[index]);
        if(impacts.empty())
            impacts.push_back(requested.impactPoint);
        fractureRevealGroups_ = CoreLogic::PartitionFractureRevealGroups(
            fracturePattern_.edges, impacts);

        std::size_t vertexCount = 0;
        for(const auto& cell : fracturePattern_.cells)
            if(cell.vertices.size() >= 3)
                vertexCount += (cell.vertices.size() - 2) * 3;
        if(vertexCount == 0)
            return false;

        fractureVertexMetadata_.reserve(vertexCount);
        fractureShardCenters_.reserve(fracturePattern_.cells.size());
        fractureShardBaseCenters_.resize(fracturePattern_.cells.size());
        fractureShardTranslations_.resize(fracturePattern_.cells.size());
        fractureShardRotations_.resize(fracturePattern_.cells.size());
        fractureShardScales_.resize(fracturePattern_.cells.size(), 1.0f);
        std::vector<UnityEngine::Vector2> uvs;
        std::vector<std::int32_t> triangles;
        uvs.reserve(vertexCount);
        triangles.reserve(vertexCount);
        for(std::size_t shard = 0; shard < fracturePattern_.cells.size(); ++shard)
        {
            const auto& cell = fracturePattern_.cells[shard];
            fractureShardCenters_.push_back(cell.site);
            const auto fan = CoreLogic::TriangulateFractureCell(cell);
            for(const auto& triangle : fan)
            {
                const std::array<CoreLogic::FracturePoint, 3> points{
                    triangle.a, triangle.b, triangle.c};
                for(const auto point : points)
                {
                    fractureVertexMetadata_.push_back({point, shard});
                    uvs.push_back({point.x, 1.0f - point.y});
                    triangles.push_back(
                        static_cast<std::int32_t>(triangles.size()));
                }
            }
        }
        fractureBaseVertices_ = ArrayW<UnityEngine::Vector3>(vertexCount);
        dynamicFractureVertices_ = ArrayW<UnityEngine::Vector3>(vertexCount);
        fractureUvs_ = ArrayW<UnityEngine::Vector2>(vertexCount);
        ArrayW<std::int32_t> unityTriangles(triangles.size());
        std::copy(uvs.begin(), uvs.end(), fractureUvs_.begin());
        std::copy(triangles.begin(), triangles.end(), unityTriangles.begin());
        fractureMesh_ = UnityEngine::Mesh::New_ctor();
        if(!fractureMesh_)
            return false;
        fractureMesh_->set_vertices(fractureBaseVertices_);
        fractureMesh_->set_uv(fractureUvs_);
        fractureMesh_->set_triangles(unityTriangles);

        const std::size_t edgeCount = fracturePattern_.edges.size();
        dynamicCrackVertices_ = ArrayW<UnityEngine::Vector3>(edgeCount * 4);
        ArrayW<UnityEngine::Vector2> crackUvs(edgeCount * 4);
        ArrayW<std::int32_t> crackTriangles(edgeCount * 6);
        for(std::size_t edge = 0; edge < edgeCount; ++edge)
        {
            const std::size_t vertex = edge * 4;
            crackUvs[vertex] = {0.0f, 0.0f};
            crackUvs[vertex + 1] = {1.0f, 0.0f};
            crackUvs[vertex + 2] = {1.0f, 1.0f};
            crackUvs[vertex + 3] = {0.0f, 1.0f};
            const std::size_t triangle = edge * 6;
            crackTriangles[triangle] = static_cast<std::int32_t>(vertex);
            crackTriangles[triangle + 1] = static_cast<std::int32_t>(vertex + 1);
            crackTriangles[triangle + 2] = static_cast<std::int32_t>(vertex + 2);
            crackTriangles[triangle + 3] = static_cast<std::int32_t>(vertex);
            crackTriangles[triangle + 4] = static_cast<std::int32_t>(vertex + 2);
            crackTriangles[triangle + 5] = static_cast<std::int32_t>(vertex + 3);
        }
        crackMesh_ = UnityEngine::Mesh::New_ctor();
        auto* shader = FindVideoShader();
        if(!crackMesh_ || !shader)
            return false;
        crackMesh_->set_vertices(dynamicCrackVertices_);
        crackMesh_->set_uv(crackUvs);
        crackMesh_->set_triangles(crackTriangles);

        crackTexture_ = UnityEngine::Texture2D::New_ctor(
            4, 1, UnityEngine::TextureFormat::RGBA32, false, false);
        crackMaterial_ = UnityEngine::Material::New_ctor(shader);
        crackObject_ = UnityEngine::GameObject::New_ctor("Big Screen Glass Cracks");
        if(!crackTexture_ || !crackMaterial_ || !crackObject_)
            return false;
        ArrayW<UnityEngine::Color> crackColors(4);
        crackColors[0] = {0.82f, 0.90f, 1.0f, 0.92f};
        crackColors[1] = {0.015f, 0.02f, 0.03f, 0.86f};
        crackColors[2] = {0.015f, 0.02f, 0.03f, 0.86f};
        crackColors[3] = {0.82f, 0.90f, 1.0f, 0.92f};
        crackTexture_->SetPixels(crackColors);
        crackTexture_->Apply(false, false);
        crackMaterial_->set_mainTexture(crackTexture_);
        crackMaterial_->set_color(UnityEngine::Color::get_white());
        // Preserve crack-texture alpha for RGB blending, but force the
        // framebuffer alpha channel to zero so the overlay cannot bloom.
        ConfigureVideoBlend(crackMaterial_, 5, 10, 0, 0, false, 3100);
        crackMaterial_->SetInt("_ColorMask", 15);
        crackMaterial_->SetInt("_Cull", 0);
        crackMaterial_->EnableKeyword("_ALPHABLEND_ON");
        crackMaterial_->set_renderQueue(3100);
        crackObject_->set_layer(videoObject_->get_layer());
        crackObject_->get_transform()->SetParent(videoObject_->get_transform(), false);
        auto* crackFilter = crackObject_->AddComponent<UnityEngine::MeshFilter*>();
        auto* crackRenderer = crackObject_->AddComponent<UnityEngine::MeshRenderer*>();
        if(!crackFilter || !crackRenderer)
            return false;
        crackFilter->set_sharedMesh(crackMesh_);
        crackRenderer->set_sharedMaterial(crackMaterial_);
        crackObject_->SetActive(false);

        fractureSnapshot_ = UnityEngine::Texture2D::New_ctor(
            textureWidth_, textureHeight_, UnityEngine::TextureFormat::RGBA32,
            false, false);
        if(!fractureSnapshot_)
            return false;

        preparedFractureSettings_ = requested;
        preparedFractureImpactCount_ = impactCount;
        for(std::size_t index = 0; index < preparedFractureImpactCount_; ++index)
            preparedFractureImpacts_[index] = fracture.impacts[index];
        fracturePrepared_ = true;
        fractureShapeCaptured_ = false;
        return true;
    }

    bool ScreenSurface::UpdateCrackOverlay(
        const CoreLogic::FractureEffectSettings& fracture,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!crackMesh_ || !crackObject_ || !dynamicCrackVertices_)
            return false;
        constexpr float CrackHalfWidth = 0.045f;
        // Every Voronoi seam is an independent quad. Butt-ended quads leave
        // tiny visible gaps where several short seams meet, especially near
        // the outer screen edges, and the resulting crack looks like a dotted
        // line in the headset. Extend both ends by slightly more than half the
        // line width so neighboring segments overlap without making the main
        // fracture branches materially thicker.
        constexpr float CrackEndOverlap = CrackHalfWidth * 0.65f;
        for(std::size_t edge = 0; edge < fracturePattern_.edges.size(); ++edge)
        {
            const auto& source = fracturePattern_.edges[edge];
            const bool revealed = edge < fractureRevealGroups_.size() &&
                fractureRevealGroups_[edge] < fracture.revealedGroupCount;
            const auto from = MapFracturePoint(
                source.from, deformation, songTimeSeconds, realTimeSeconds);
            const auto to = MapFracturePoint(
                source.to, deformation, songTimeSeconds, realTimeSeconds);
            const std::size_t vertex = edge * 4;
            if(!revealed)
            {
                dynamicCrackVertices_[vertex] = from;
                dynamicCrackVertices_[vertex + 1] = from;
                dynamicCrackVertices_[vertex + 2] = from;
                dynamicCrackVertices_[vertex + 3] = from;
                continue;
            }
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float inverseLength = 1.0f /
                std::max(0.0001f, std::sqrt(dx * dx + dy * dy));
            const float tangentX = dx * inverseLength;
            const float tangentY = dy * inverseLength;
            const float offsetX = -dy * inverseLength * CrackHalfWidth;
            const float offsetY = dx * inverseLength * CrackHalfWidth;
            const float fromX = from.x - tangentX * CrackEndOverlap;
            const float fromY = from.y - tangentY * CrackEndOverlap;
            const float toX = to.x + tangentX * CrackEndOverlap;
            const float toY = to.y + tangentY * CrackEndOverlap;
            dynamicCrackVertices_[vertex] =
                {fromX - offsetX, fromY - offsetY, from.z - 0.04f};
            dynamicCrackVertices_[vertex + 1] =
                {fromX + offsetX, fromY + offsetY, from.z - 0.04f};
            dynamicCrackVertices_[vertex + 2] =
                {toX + offsetX, toY + offsetY, to.z - 0.04f};
            dynamicCrackVertices_[vertex + 3] =
                {toX - offsetX, toY - offsetY, to.z - 0.04f};
        }
        crackMesh_->set_vertices(dynamicCrackVertices_);
        crackMesh_->RecalculateBounds();
        if(crackMaterial_)
            crackMaterial_->set_color({1.0f, 1.0f, 1.0f,
                std::clamp(fracture.crackOpacity, 0.0f, 1.0f)});
        crackObject_->SetActive(true);
        return true;
    }

    bool ScreenSurface::CaptureFractureShape(
        const CoreLogic::FractureEffectSettings& fracture,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!fractureMesh_ || !fractureBaseVertices_ || !fractureUvs_)
            return false;
        const float cover = CoreLogic::DeformationAutoCoverScale(
            screenWidth_, screenHeight_, deformation);
        for(std::size_t index = 0; index < fractureVertexMetadata_.size(); ++index)
        {
            const auto point = fractureVertexMetadata_[index].point;
            fractureBaseVertices_[index] = MapFracturePoint(
                point, deformation, songTimeSeconds, realTimeSeconds);
            dynamicFractureVertices_[index] = fractureBaseVertices_[index];
            fractureUvs_[index] = {
                std::clamp(0.5f + (point.x - 0.5f) / cover, 0.0f, 1.0f),
                std::clamp(0.5f + ((1.0f - point.y) - 0.5f) / cover,
                           0.0f, 1.0f)};
        }
        for(std::size_t shard = 0; shard < fractureShardCenters_.size(); ++shard)
            fractureShardBaseCenters_[shard] = MapFracturePoint(
                fractureShardCenters_[shard], deformation,
                songTimeSeconds, realTimeSeconds);
        fractureMesh_->set_vertices(fractureBaseVertices_);
        fractureMesh_->set_uv(fractureUvs_);
        fractureMesh_->RecalculateBounds();
        fractureShapeCaptured_ = true;
        if(fracture.freezeOnShatter)
        {
            UnityEngine::Graphics::CopyTexture(texture_, fractureSnapshot_);
            material_->set_mainTexture(fractureSnapshot_);
            fractureSnapshotActive_ = true;
        }
        else
        {
            material_->set_mainTexture(texture_);
            fractureSnapshotActive_ = false;
        }
        return true;
    }

    bool ScreenSurface::UpdateFractureVertices(
        const CoreLogic::FractureEffectSettings& fracture)
    {
        if(!fractureMesh_ || !fractureBaseVertices_ ||
           !dynamicFractureVertices_)
            return false;
        const float separation = std::clamp(fracture.separation, 0.0f, 1.0f);
        const auto impact = fracture.pattern.impactPoint;
        const std::size_t overrideCount = std::min(
            fracture.shardTransformCount, fracture.shardTransforms.size());
        for(std::size_t shard = 0; shard < fractureShardCenters_.size(); ++shard)
        {
            const auto centerPoint = fractureShardCenters_[shard];
            float directionX =
                (centerPoint.x - impact.x) * std::max(screenWidth_, 0.001f);
            float directionY =
                (centerPoint.y - impact.y) * std::max(screenHeight_, 0.001f);
            const float radius = std::sqrt(
                directionX * directionX + directionY * directionY);
            if(radius > 0.0001f)
            {
                directionX /= radius;
                directionY /= radius;
            }
            const float normalizedRadius = std::clamp(
                std::sqrt(
                    (centerPoint.x - impact.x) * (centerPoint.x - impact.x) +
                    (centerPoint.y - impact.y) * (centerPoint.y - impact.y)) *
                    1.6f,
                0.0f, 1.0f);
            const float delay = (1.0f - normalizedRadius) *
                std::clamp(fracture.stagger, 0.0f, 0.8f);
            const float local = std::clamp(
                (separation - delay) / std::max(0.001f, 1.0f - delay),
                0.0f, 1.0f);
            const float eased = local * local * (3.0f - 2.0f * local);
            const std::uint32_t hash = FractureHash(fracture.pattern.seed, shard);
            const float randomX = HashSigned(hash);
            const float randomY = HashSigned(hash ^ 0xA511E9B3U);
            const float randomZ = HashSigned(hash ^ 0x63D83595U);
            CoreLogic::FractureShardTransform authored{};
            authored.shardIndex = shard;
            for(std::size_t index = 0; index < overrideCount; ++index)
            {
                if(fracture.shardTransforms[index].shardIndex == shard)
                {
                    authored = fracture.shardTransforms[index];
                    break;
                }
            }
            const float rotation = fracture.tumbleDegrees * eased;
            fractureShardRotations_[shard] = {
                rotation * randomX + authored.rotationDegrees.x * eased,
                rotation * randomY + authored.rotationDegrees.y * eased,
                rotation * randomZ + authored.rotationDegrees.z * eased};
            UnityEngine::Vector3 value{};
            const float kick = fracture.outwardDistance * eased *
                (0.72f + std::abs(randomX) * 0.36f);
            value.x = directionX * kick + randomX * kick * 0.18f +
                authored.translation.x * eased;
            value.y = directionY * kick + randomY * kick * 0.14f -
                fracture.gravityDistance * local * local +
                authored.translation.y * eased;
            const float randomizedForwardAmount =
                0.25f + (randomZ * 0.5f + 0.5f) * 0.75f;
            const float forwardTravel = fracture.forwardScatterDistance > 0.0f
                ? fracture.forwardScatterDistance * eased *
                    randomizedForwardAmount
                : kick * (0.35f + std::abs(randomZ) * 0.35f);
            // Negative local Z is toward the player for the back-wall screen.
            // A separately authored depth range lets shards fall forward as a
            // three-dimensional cloud instead of remaining a thin glass row.
            value.z = -forwardTravel + authored.translation.z * eased;
            fractureShardTranslations_[shard] = value;
            fractureShardScales_[shard] = std::max(
                0.01f, 1.0f + (authored.scale - 1.0f) * eased);
        }
        for(std::size_t index = 0; index < fractureVertexMetadata_.size(); ++index)
        {
            const std::size_t shard = fractureVertexMetadata_[index].shard;
            const auto center = fractureShardBaseCenters_[shard];
            const auto rotation = fractureShardRotations_[shard];
            auto value = RotateFractureVertex(
                fractureBaseVertices_[index], center,
                rotation.x, rotation.y, rotation.z);
            const float scale = fractureShardScales_[shard];
            value.x = center.x + (value.x - center.x) * scale;
            value.y = center.y + (value.y - center.y) * scale;
            value.z = center.z + (value.z - center.z) * scale;
            const auto translation = fractureShardTranslations_[shard];
            value.x += translation.x;
            value.y += translation.y;
            value.z += translation.z;
            dynamicFractureVertices_[index] = value;
        }
        fractureMesh_->set_vertices(dynamicFractureVertices_);
        fractureMesh_->RecalculateBounds();
        return true;
    }

    bool ScreenSurface::SetFractureEffect(
        const CoreLogic::FractureEffectSettings& fracture,
        const CoreLogic::SurfaceDeformationSettings& deformation,
        double songTimeSeconds,
        double realTimeSeconds)
    {
        if(!fracture.enabled ||
           fracture.phase == CoreLogic::FracturePhase::Inactive)
        {
            if(fractureMeshActive_ || fractureSnapshotActive_ ||
               (crackObject_ && crackObject_->get_activeSelf()))
                RestoreWholeVideoMesh();
            return true;
        }
        if(!PrepareFracture(fracture))
            return false;

        if(fracture.phase == CoreLogic::FracturePhase::Prepared)
        {
            if(fractureMeshActive_ || fractureSnapshotActive_ ||
               (crackObject_ && crackObject_->get_activeSelf()))
                RestoreWholeVideoMesh();
            return true;
        }
        if(fracture.phase == CoreLogic::FracturePhase::CrackOnly)
        {
            if(fractureMeshActive_ || fractureSnapshotActive_)
                RestoreWholeVideoMesh();
            return UpdateCrackOverlay(
                fracture, deformation, songTimeSeconds, realTimeSeconds);
        }

        if(!fractureShapeCaptured_ &&
           !CaptureFractureShape(
               fracture, deformation, songTimeSeconds, realTimeSeconds))
            return false;
        if(fracture.freezeOnShatter)
        {
            if(!fractureSnapshotActive_)
                UnityEngine::Graphics::CopyTexture(texture_, fractureSnapshot_);
            material_->set_mainTexture(fractureSnapshot_);
            fractureSnapshotActive_ = true;
        }
        else if(!fracture.freezeOnShatter && fractureSnapshotActive_)
        {
            material_->set_mainTexture(texture_);
            fractureSnapshotActive_ = false;
        }
        if(crackObject_)
            crackObject_->SetActive(false);
        if(!fractureMeshActive_)
        {
            auto* filter = videoObject_->GetComponent<UnityEngine::MeshFilter*>();
            auto* guardFilter = alphaGuardObject_
                ? alphaGuardObject_->GetComponent<UnityEngine::MeshFilter*>()
                : nullptr;
            if(!filter || (alphaGuardObject_ && !guardFilter))
                return false;
            filter->set_sharedMesh(fractureMesh_);
            if(guardFilter)
                guardFilter->set_sharedMesh(fractureMesh_);
            fractureMeshActive_ = true;
        }
        if(!UpdateFractureVertices(fracture))
            return false;

        if(fracture.phase == CoreLogic::FracturePhase::Rejoining &&
           fracture.separation <= 0.0001f)
        {
            RestoreWholeVideoMesh();
            if(fracture.retainCracksAfterRejoin)
                return UpdateCrackOverlay(
                    fracture, deformation, songTimeSeconds, realTimeSeconds);
        }
        return true;
    }

    void ScreenSurface::Destroy()
    {
        // Unregister BEFORE the video object is destroyed so the bloom
        // pre-pass never draws a dying surface during the same frame.
        // BLOOM EXPERIMENT DISABLED (2026-08-18): registration is compiled
        // out above, but retain the matching cleanup code for restoration.
#if BIGSCREEN_ENABLE_EXPERIMENTAL_CINEMA_BLOOM
        if(bloomRegistered_)
        {
            CinemaBloomRenderer::Instance().UnregisterSource(videoObject_);
            bloomRegistered_ = false;
        }
#endif
        bloomRegistered_ = false;
        mapperBloom_ = 1.0f;
        DestroyFractureResources();
        DestroyIfAlive(diagnosticsObject_);
        diagnosticsText_ = nullptr;
        // Destroying the GameObject also destroys its MeshFilter and Renderer.
        // Mesh, material, and texture were created as standalone Unity objects,
        // so they are released explicitly when gameplay ends or a level changes.
        DestroyIfAlive(alphaGuardObject_);
        DestroyIfAlive(videoObject_);
        DestroyIfAlive(gameObject_);
        if(ownsTexture_)
        {
            DestroyIfAlive(rgbaTexture_);
            DestroyIfAlive(yTexture_);
            DestroyIfAlive(uTexture_);
            DestroyIfAlive(vTexture_);
            DestroyIfAlive(packedYuvTexture_);
            DestroyIfAlive(gpuConversionMaterial_);
            if(UnityW<UnityEngine::RenderTexture>::isAlive(gpuTexture_))
            {
                if(gpuTexture_->IsCreated())
                    gpuTexture_->Release();
                DestroyIfAlive(gpuTexture_);
            }
        }
        else
        {
            rgbaTexture_ = nullptr;
            yTexture_ = nullptr;
            uTexture_ = nullptr;
            vTexture_ = nullptr;
            packedYuvTexture_ = nullptr;
            gpuConversionMaterial_ = nullptr;
            gpuTexture_ = nullptr;
        }
        texture_ = nullptr;
        DestroyIfAlive(material_);
        DestroyIfAlive(alphaGuardMaterial_);
        DestroyIfAlive(mesh_);
        DestroyIfAlive(videoMesh_);
        DestroyIfAlive(backgroundMaterial_);
        textureWidth_ = 0;
        textureHeight_ = 0;
        ownsTexture_ = false;
        gpuConversionRequested_ = false;
        gpuConversionActive_ = false;
        gpuYuvUploadLayout_ = GpuYuvUploadLayout::ThreePlane;
        gpuYuvUploadFallback_.reset();
        screenWidth_ = 0.0f;
        screenHeight_ = 0.0f;
        letterboxTransparent_ = false;
        textureHasAuthoredAlpha_ = false;
        colorBlending_ = false;
        opaqueScreenBody_ = false;
        videoCoversFrame_ = false;
        opacity_ = 1.0f;
        visible_ = false;
        leadInActive_ = false;
        leadInBlack_ = false;
        geometryConfig_ = {};
        geometryAspectRatio_ = 1.0f;
        deformationBaseVertices_.clear();
        undeformedVideoVertices_ = nullptr;
        dynamicVideoVertices_ = nullptr;
        undeformedVideoUvs_ = nullptr;
        dynamicVideoUvs_ = nullptr;
        prepareDeformation_ = false;
        deformationWasApplied_ = false;
    }

    void ScreenSurface::SetDiagnosticsText(const std::string& text)
    {
        if(text.empty())
        {
            if(diagnosticsObject_)
                UnityEngine::Object::Destroy(diagnosticsObject_);
            diagnosticsObject_ = nullptr;
            diagnosticsText_ = nullptr;
            return;
        }
        if(!gameObject_)
            return;
        if(!diagnosticsObject_)
        {
            diagnosticsObject_ = UnityEngine::GameObject::New_ctor(
                "Big Screen Performance Information");
            diagnosticsObject_->set_layer(gameObject_->get_layer());
            auto transform = diagnosticsObject_->get_transform();
            transform->SetParent(gameObject_->get_transform(), false);
            // The text rectangle spans almost the full canvas width and is
            // centered at this transform. Center X and keep its lower edge
            // just inside the video frame so flat, curved, enlarged, and
            // undocked layouts all show the same readable overlay.
            transform->set_localPosition({
                0.0f,
                -screenHeight_ * 0.36f,
                -0.08f});
            transform->set_localEulerAngles({0.0f, 0.0f, 0.0f});
            transform->set_localScale({0.08f, 0.08f, 0.08f});
            diagnosticsText_ = diagnosticsObject_->AddComponent<TMPro::TextMeshPro*>();
            if(!diagnosticsText_)
            {
                UnityEngine::Object::Destroy(diagnosticsObject_);
                diagnosticsObject_ = nullptr;
                return;
            }
            diagnosticsText_->set_font(BSML::Helpers::GetMainTextFont());
            diagnosticsText_->set_fontSize(4.0f);
            diagnosticsText_->set_alignment(TMPro::TextAlignmentOptions::BottomLeft);
            diagnosticsText_->set_color(UnityEngine::Color::get_white());
            diagnosticsText_->get_rectTransform()->set_sizeDelta({
                screenWidth_ * 11.5f,
                screenHeight_ * 3.0f});
        }
        diagnosticsText_->set_text(text);
    }
}
