// =============================================================================
// UberLit_Skinned.hlsl — GPU Skinning(Compute Cache) 경로의 Uber Shader
// =============================================================================
// VS만 다름: SkinCache StructuredBuffer(t30)에서 SV_VertexID로 정점을 읽음.
// PS와 lighting model permutation들은 UberLit.hlsl과 동일.
// =============================================================================

#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/SkinCacheAccess.hlsli"

#if !defined(LIGHTING_MODEL_UNLIT)
#include "Common/ForwardLighting.hlsli"
#endif

#if !defined(LIGHTING_MODEL_GOURAUD) && !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_PHONG) && !defined(LIGHTING_MODEL_UNLIT)
#define LIGHTING_MODEL_PHONG 1
#endif

Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);

cbuffer PerShader1 : register(b2)
{
    float4 SectionColor;
    float HasNormalMap;
    float3 _pad;
};

static const float4 g_DefaultEmissive = float4(0, 0, 0, 0);
static const float g_DefaultShininess = 32.0f;

struct UberVS_Output
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float4 tangent : TANGENT;
#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 litDiffuse  : TEXCOORD2;
    float3 litSpecular : TEXCOORD3;
#endif
};

// =============================================================================
// Vertex Shader — SkinCache에서 정점 읽음
// =============================================================================
UberVS_Output VS(uint vid : SV_VertexID)
{
    VS_Input_PNCTT input = ReadSkinnedVertex(vid);

    UberVS_Output output;

    float3x3 M = (float3x3) Model;

    float4 worldPos4 = mul(float4(input.position, 1.0f), Model);
    output.worldPos = worldPos4.xyz;
    output.position = mul(mul(worldPos4, View), Projection);
    output.normal = normalize(mul(input.normal, (float3x3) NormalMatrix));
    output.color = input.color * SectionColor;
    output.texcoord = input.texcoord;

    float3 T = normalize(mul(input.tangent.xyz, M));
    T = normalize(T - output.normal * dot(output.normal, T));
    output.tangent = float4(T, input.tangent.w);

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 N = output.normal;

    if (HasNormalMap > 0.5f)
    {
        float3 B = normalize(cross(N, T) * input.tangent.w);
        float3x3 TBN = float3x3(T, B, N);

        float3 tangentNormal = NormalTexture.SampleLevel(LinearWrapSampler, input.texcoord, 0).xyz * 2.0f - 1.0f;
        N = normalize(mul(tangentNormal, TBN));
    }

    float3 V = normalize(CameraWorldPos - output.worldPos);
    output.litDiffuse = AccumulateDiffuseVS(output.worldPos, N);
    output.litSpecular = AccumulateSpecularVS(output.worldPos, N, V, g_DefaultShininess);
#endif

    return output;
}

// =============================================================================
// Pixel Shader — UberLit.hlsl의 PS와 100% 동일
// =============================================================================
struct UberPS_Output
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 Culling : SV_TARGET2;
};

UberPS_Output PS(UberVS_Output input)
{
    UberPS_Output output;

    float4 texColor = DiffuseTexture.Sample(LinearWrapSampler, input.texcoord);
    if (texColor.a < 0.001f)
        texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    float4 baseColor = texColor * input.color;

    float3 N = normalize(input.normal);

#if !defined(LIGHTING_MODEL_GOURAUD)
    if (HasNormalMap >= 0.5)
    {
        float3 T = normalize(input.tangent.xyz);
        T = normalize(T - N * dot(N, T));

        float3 B = normalize(cross(N, T) * input.tangent.w);
        float3x3 TBN = float3x3(T, B, N);

        float3 tangentNormal = NormalTexture.Sample(LinearWrapSampler, input.texcoord).xyz * 2.0f - 1.0f;
        N = normalize(mul(tangentNormal, TBN));
    }
#endif

    float3 V = normalize(CameraWorldPos - input.worldPos);

#if defined(LIGHTING_MODEL_UNLIT) && LIGHTING_MODEL_UNLIT
    float3 finalColor = ApplyWireframe(baseColor.rgb);
    output.Culling = float4(0, 0, 0, 0);
#else
    float3 diffuse = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    diffuse  = input.litDiffuse;
    specular = input.litSpecular;
#elif defined(LIGHTING_MODEL_LAMBERT) && LIGHTING_MODEL_LAMBERT
    diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
#elif defined(LIGHTING_MODEL_PHONG) && LIGHTING_MODEL_PHONG
    diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
    specular = AccumulateSpecular(input.worldPos, N, V, g_DefaultShininess, input.position);
#endif

    output.Culling = ComputeCullingHeatmap(input.position, input.worldPos);
    float3 finalColor = baseColor.rgb * diffuse + specular + g_DefaultEmissive.rgb;
    finalColor = ApplyWireframe(finalColor);
#endif

    output.Color = float4(finalColor, baseColor.a);
    output.Normal = float4(N, 1.0f);

    return output;
}
