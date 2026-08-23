// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.XR.Management;
using UnityEditor.XR.Management.Metadata;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.XR.Management;
using Unity.XR.Oculus;

internal static class BuildBigScreenVideoShader
{
    private const string OutputVariable = "BIGSCREEN_VIDEO_SHADER_OUTPUT";
    private const string BundleName = "bigscreen_video_shader";
    private const string ShaderAsset = "Assets/BigScreenVideo.shader";
    private const string YuvConversionShaderAsset =
        "Assets/BigScreenYuvConversion.shader";
    private const string AddressableName = "bigscreen-video-shader";
    private const string YuvConversionAddressableName =
        "bigscreen-yuv-conversion-shader";
    private const string OculusSettingsKey = "Unity.XR.Oculus.Settings";

    /// Shader variant compilation for AssetBundles follows the BUILDING
    /// project's XR configuration, not the consuming game's. Without an
    /// active Android XR loader, Unity strips the stereo variants out of the
    /// bundle even though the shader source requests them with multi_compile.
    /// Decompiling an affected bundle (2026-08-18) showed the stripped result
    /// keeps the keyword names but contains only the mono base variant: it
    /// binds at runtime and rasterizes nothing in either eye on the Quest's
    /// single-pass multiview renderer. Assign the Oculus loader with
    /// Multiview before every build and fail loudly when that is impossible,
    /// because a bundle built without it is unusable.
    private static void EnsureAndroidMultiviewXr()
    {
        XRGeneralSettingsPerBuildTarget perBuildTarget;
        EditorBuildSettings.TryGetConfigObject(
            XRGeneralSettings.k_SettingsKey, out perBuildTarget);
        if (perBuildTarget == null)
        {
            perBuildTarget = ScriptableObject
                .CreateInstance<XRGeneralSettingsPerBuildTarget>();
            if (!AssetDatabase.IsValidFolder("Assets/XR"))
                AssetDatabase.CreateFolder("Assets", "XR");
            AssetDatabase.CreateAsset(
                perBuildTarget, "Assets/XR/XRGeneralSettings.asset");
            EditorBuildSettings.AddConfigObject(
                XRGeneralSettings.k_SettingsKey, perBuildTarget, true);
        }

        XRGeneralSettings androidSettings =
            perBuildTarget.SettingsForBuildTarget(BuildTargetGroup.Android);
        if (androidSettings == null)
        {
            androidSettings =
                ScriptableObject.CreateInstance<XRGeneralSettings>();
            perBuildTarget.SetSettingsForBuildTarget(
                BuildTargetGroup.Android, androidSettings);
            AssetDatabase.AddObjectToAsset(androidSettings, perBuildTarget);
        }
        if (androidSettings.Manager == null)
        {
            XRManagerSettings manager =
                ScriptableObject.CreateInstance<XRManagerSettings>();
            androidSettings.Manager = manager;
            AssetDatabase.AddObjectToAsset(manager, perBuildTarget);
        }

        bool oculusActive = androidSettings.Manager.activeLoaders != null &&
            androidSettings.Manager.activeLoaders.Any(
                loader => loader is OculusLoader);
        if (!oculusActive && !XRPackageMetadataStore.AssignLoader(
                androidSettings.Manager,
                typeof(OculusLoader).FullName,
                BuildTargetGroup.Android))
            throw new InvalidOperationException(
                "Could not assign the Oculus XR loader for Android. The " +
                "bundle would be built without stereo variants and would " +
                "render nothing on the Quest; refusing to build it.");

        OculusSettings oculusSettings;
        EditorBuildSettings.TryGetConfigObject(
            OculusSettingsKey, out oculusSettings);
        if (oculusSettings == null)
        {
            oculusSettings = ScriptableObject.CreateInstance<OculusSettings>();
            if (!AssetDatabase.IsValidFolder("Assets/XR"))
                AssetDatabase.CreateFolder("Assets", "XR");
            AssetDatabase.CreateAsset(
                oculusSettings, "Assets/XR/OculusSettings.asset");
            EditorBuildSettings.AddConfigObject(
                OculusSettingsKey, oculusSettings, true);
        }
        // Beat Saber renders single-pass multiview on the Quest; the bundle's
        // stereo variants must match or the shader has nothing to bind.
        oculusSettings.m_StereoRenderingModeAndroid =
            OculusSettings.StereoRenderingModeAndroid.Multiview;
        EditorUtility.SetDirty(oculusSettings);
        EditorUtility.SetDirty(perBuildTarget);
        AssetDatabase.SaveAssets();
        Debug.Log(
            "Big Screen shader build: Oculus XR loader active for Android " +
            "with multiview stereo.");
    }

    public static void BuildAndroid()
    {
        string output = Environment.GetEnvironmentVariable(OutputVariable);
        if (string.IsNullOrWhiteSpace(output))
            throw new InvalidOperationException(
                OutputVariable + " must identify the bundle output directory.");

        EnsureAndroidMultiviewXr();

        // Beat Saber 1.40.8 runs through Vulkan on Quest. Include GLES3 as a
        // compatibility variant as well so this asset remains usable if Meta
        // or a testing build selects the alternate Android graphics backend.
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
        PlayerSettings.SetGraphicsAPIs(
            BuildTarget.Android,
            new[] { GraphicsDeviceType.Vulkan, GraphicsDeviceType.OpenGLES3 });

        Directory.CreateDirectory(output);
        var build = new AssetBundleBuild
        {
            assetBundleName = BundleName,
            assetNames = new[] { ShaderAsset, YuvConversionShaderAsset },
            addressableNames = new[] {
                AddressableName,
                YuvConversionAddressableName
            }
        };

        AssetBundleManifest manifest = BuildPipeline.BuildAssetBundles(
            output,
            new[] { build },
            BuildAssetBundleOptions.ChunkBasedCompression |
                BuildAssetBundleOptions.DeterministicAssetBundle |
                BuildAssetBundleOptions.ForceRebuildAssetBundle |
                BuildAssetBundleOptions.StrictMode,
            BuildTarget.Android);

        string bundle = Path.Combine(output, BundleName);
        if (manifest == null || !File.Exists(bundle))
            throw new InvalidOperationException(
                "Unity did not produce the Android Big Screen video shader bundle.");

        Debug.Log("Built Big Screen Android video shader bundle: " + bundle);
    }
}
