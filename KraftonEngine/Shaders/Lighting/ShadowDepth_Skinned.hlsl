// =============================================================================
// ShadowDepth_Skinned.hlsl — GPU Skinning(Compute Cache) 경로의 Shadow Depth
// =============================================================================
// VS만 다름: SkinCache StructuredBuffer(t30)에서 SV_VertexID로 정점을 읽음.
// PS는 ShadowDepth.hlsl과 100% 동일.
// =============================================================================

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SkinCacheAccess.hlsli"

cbuffer ShadowLightBuffer : register(b2)
{
    float4x4 LightViewProj;
};

PS_Input_Shadow VS(uint vid : SV_VertexID)
{
    VS_Input_PNCTT input = ReadSkinnedVertex(vid);

    PS_Input_Shadow output;
    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    float4 clipPos  = mul(worldPos, LightViewProj);

    output.position = clipPos;
    output.depth    = clipPos.z / clipPos.w;
    return output;
}

float2 PS(PS_Input_Shadow input) : SV_TARGET
{
    float d = input.depth;
    float e = exp(EVSM_EXPONENT * d);
    float dx = ddx(e);
    float dy = ddy(e);
    float moment2 = e * e + 0.25f * (dx * dx + dy * dy);
    return float2(e, moment2);
}
