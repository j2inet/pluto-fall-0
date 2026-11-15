// ShaderSource.h - HLSL source for model rendering. Compiled at runtime with D3DCompile so that
// pixel shader permutations can be generated per material feature set.
#pragma once

namespace pf
{
    inline constexpr char ModelShaderSource[] = R"HLSL(
cbuffer FrameConstants : register(b0)
{
    float4x4 ViewProjection;
    float4 LightDirection;   // xyz: direction light travels
    float4 LightColor;
    float4 AmbientColor;
    float4 CameraPosition;
};

cbuffer ObjectConstants : register(b1)
{
    float4x4 World;
    float4x4 WorldInverse;   // used as the transpose of the inverse for normals
    float4 Tint;
    float4 Flash;
};

cbuffer MaterialConstants : register(b2)
{
    float4 BaseColorFactor;
    float4 EmissiveFactor;
    float Metallic;
    float Roughness;
    float AlphaCutoff;
    float MaterialPad;
};

Texture2D BaseColorMap : register(t0);
Texture2D EmissiveMap  : register(t1);
SamplerState LinearSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct PSInput
{
    float4 position : SV_Position;
    float3 worldPos : POSITION1;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 world = mul(float4(input.position, 1.0f), World);
    output.worldPos = world.xyz;
    output.position = mul(world, ViewProjection);
    output.normal = mul(WorldInverse, float4(input.normal, 0.0f)).xyz;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 baseColor = BaseColorFactor * input.color;
#if HAS_BASE_COLOR_TEXTURE
    baseColor *= BaseColorMap.Sample(LinearSampler, input.uv);
#endif
    baseColor *= Tint;

#if ALPHA_MASK
    clip(baseColor.a - AlphaCutoff);
#endif

    float3 color;
#if UNLIT
    color = baseColor.rgb;
#else
    float3 n = normalize(input.normal);
    if (!isFrontFace)
        n = -n;
    float3 l = normalize(-LightDirection.xyz);
    float3 v = normalize(CameraPosition.xyz - input.worldPos);
    float3 h = normalize(l + v);

    float roughness = clamp(Roughness, 0.04f, 1.0f);
    float shininess = lerp(512.0f, 4.0f, sqrt(roughness));
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor.rgb, Metallic);
    float3 diffuse = baseColor.rgb * (1.0f - Metallic);

    float nDotL = saturate(dot(n, l));
    float specular = pow(saturate(dot(n, h)), shininess) * (shininess + 8.0f) / 25.13f;
    float3 fresnel = f0 + (1.0f - f0) * pow(1.0f - saturate(dot(h, v)), 5.0f);

    color = AmbientColor.rgb * (diffuse + f0 * 0.5f);
    color += LightColor.rgb * nDotL * (diffuse + fresnel * specular);
    // Cool rim light from behind so silhouettes read against dark space.
    float rim = pow(1.0f - saturate(dot(n, v)), 3.0f);
    color += rim * float3(0.25f, 0.35f, 0.6f) * (0.3f + diffuse);
#endif

    float3 emissive = EmissiveFactor.rgb;
#if HAS_EMISSIVE_TEXTURE
    emissive *= EmissiveMap.Sample(LinearSampler, input.uv).rgb;
#endif
    color += emissive + Flash.rgb;

    // Linear -> display gamma (the swap chain is UNORM, not sRGB).
    color = pow(saturate(color), 1.0f / 2.2f);
    return float4(color, baseColor.a);
}
)HLSL";
}
