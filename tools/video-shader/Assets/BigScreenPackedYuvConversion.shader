// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Shader "BigScreen/PackedYuvConversion"
{
    Properties
    {
        _MainTex ("Packed YUV Atlas", 2D) = "black" {}
    }

    SubShader
    {
        Cull Off
        ZWrite Off
        ZTest Always

        Pass
        {
            CGPROGRAM
            #pragma target 3.0
            #pragma vertex vert_img
            #pragma fragment frag
            #include "UnityCG.cginc"

            sampler2D _MainTex;
            // xy = atlas width/height; zw = luma width/height.
            float4 _PackedLayout;
            // xy = chroma width/height.
            float4 _PackedChromaSize;
            float4 _YuvOffset;
            float4 _YuvRow0;
            float4 _YuvRow1;
            float4 _YuvRow2;
            float4 _ColorRow0;
            float4 _ColorRow1;
            float4 _ColorRow2;
            float4 _ColorBias;
            float _ColorCorrectionEnabled;
            float _InverseGamma;
            float _QuarterTurns;
            float _VignetteEnabled;
            float _VignetteElliptical;
            float _VignetteRadius;
            float _VignetteSoftness;

            float3 SrgbToLinearExact(float3 value)
            {
                value = saturate(value);
                float3 low = value / 12.92;
                float3 high = pow((value + 0.055) / 1.055, 2.4);
                return lerp(low, high, step(0.04045, value));
            }

            float2 SourceUv(float2 outputUv)
            {
                int turns = ((int)round(_QuarterTurns)) & 3;
                if (turns == 1)
                    return float2(1.0 - outputUv.y, outputUv.x);
                if (turns == 2)
                    return 1.0 - outputUv;
                if (turns == 3)
                    return float2(outputUv.y, 1.0 - outputUv.x);
                return outputUv;
            }

            // Map source UVs between texel centers inside one atlas region.
            // Keeping both endpoints a half texel inside the region prevents
            // bilinear sampling from bleeding U into V or chroma into luma.
            float2 RegionUv(float2 uv, float2 origin, float2 size)
            {
                float2 pixel = saturate(uv) * max(size - 1.0, 0.0) + 0.5;
                return (origin + pixel) / _PackedLayout.xy;
            }

            float VignetteMask(float2 uv)
            {
                float2 normalized = abs(uv * 2.0 - 1.0);
                float rectangleEdge = max(normalized.x, normalized.y);
                float radius = saturate(_VignetteRadius);
                float edge = rectangleEdge;
                float outer = radius;
                if (_VignetteElliptical > 0.5)
                {
                    float ellipseEdge = length(normalized);
                    edge = lerp(ellipseEdge, rectangleEdge, radius);
                    outer = 1.0;
                }
                float inner = max(0.0, outer - max(_VignetteSoftness, 0.00001));
                return 1.0 - smoothstep(inner, outer, edge);
            }

            fixed4 frag(v2f_img input) : SV_Target
            {
                float2 sourceUv = SourceUv(input.uv);
                float2 chromaSize = _PackedChromaSize.xy;
                float y = tex2D(_MainTex, RegionUv(
                    sourceUv, float2(0.0, 0.0), _PackedLayout.zw)).r;
                float u = tex2D(_MainTex, RegionUv(
                    sourceUv, float2(0.0, _PackedLayout.w), chromaSize)).r;
                float v = tex2D(_MainTex, RegionUv(
                    sourceUv,
                    float2(chromaSize.x, _PackedLayout.w),
                    chromaSize)).r;
                float3 yuv = float3(y, u, v) + _YuvOffset.xyz;
                float3 rgb = float3(
                    dot(_YuvRow0.xyz, yuv),
                    dot(_YuvRow1.xyz, yuv),
                    dot(_YuvRow2.xyz, yuv));
                rgb = saturate(rgb);
                if (_ColorCorrectionEnabled > 0.5)
                {
                    rgb = saturate(float3(
                        dot(_ColorRow0.xyz, rgb),
                        dot(_ColorRow1.xyz, rgb),
                        dot(_ColorRow2.xyz, rgb)) + _ColorBias.xyz);
                    rgb = pow(rgb, max(_InverseGamma, 0.00001));
                }
                rgb = SrgbToLinearExact(rgb);
                float alpha = _VignetteEnabled > 0.5
                    ? VignetteMask(input.uv)
                    : 1.0;
                return fixed4(rgb, alpha);
            }
            ENDCG
        }
    }
    Fallback Off
}
