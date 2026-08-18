// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Shader "BigScreen/Video"
{
    Properties
    {
        _MainTex ("Video", 2D) = "white" {}
        _Color ("Color", Color) = (1, 1, 1, 1)
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcColor ("Source Color", Float) = 1
        [Enum(UnityEngine.Rendering.BlendMode)] _DestColor ("Destination Color", Float) = 0
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcAlpha ("Source Alpha", Float) = 0
        [Enum(UnityEngine.Rendering.BlendMode)] _DestAlpha ("Destination Alpha", Float) = 1
        [Enum(Off, 0, On, 1)] _ZWrite ("Depth Write", Float) = 1
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2
    }

    SubShader
    {
        Tags { "RenderType"="Transparent" "Queue"="Transparent" }

        Pass
        {
            // RGB blending remains selectable for opaque, transparent, and
            // Cinema soft-additive screens. Alpha uses a separate equation:
            // Big Screen always preserves the destination alpha instead of
            // replacing it with the video's opaque texture alpha.
            Blend [_SrcColor] [_DestColor], [_SrcAlpha] [_DestAlpha]
            ColorMask RGBA
            ZWrite [_ZWrite]
            ZTest LEqual
            Cull [_Cull]

            CGPROGRAM
            #pragma target 3.0
            // Beat Saber on Quest renders single-pass multiview:
            // both eyes in one pass with per-eye matrices that only
            // exist in a stereo-compiled shader variant. Unity adds
            // stereo variants automatically only when the BUILDING
            // project has XR enabled, which this bundle project
            // deliberately does not, so they are requested here
            // explicitly. Without this line the bundle carries only
            // the mono variant and the screen mesh rasterizes
            // nothing in either eye - an invisible screen, with the
            // game's Bloom setting in any state.
            #pragma multi_compile _ STEREO_MULTIVIEW_ON STEREO_INSTANCING_ON
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_instancing
            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
                UNITY_VERTEX_INPUT_INSTANCE_ID
            };

            struct v2f
            {
                float4 vertex : SV_POSITION;
                float2 uv : TEXCOORD0;
                UNITY_VERTEX_OUTPUT_STEREO
            };

            sampler2D _MainTex;
            float4 _MainTex_ST;
            fixed4 _Color;

            v2f vert(appdata input)
            {
                v2f output;
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_INITIALIZE_VERTEX_OUTPUT_STEREO(output);
                output.vertex = UnityObjectToClipPos(input.vertex);
                output.uv = TRANSFORM_TEX(input.uv, _MainTex);
                return output;
            }

            fixed4 frag(v2f input) : SV_Target
            {
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                return tex2D(_MainTex, input.uv) * _Color;
            }
            ENDCG
        }
    }

    Fallback Off
}
