#ifndef SKIN_CACHE_ACCESS_HLSL
#define SKIN_CACHE_ACCESS_HLSL

// =============================================================================
// SkinCacheAccess.hlsli
// =============================================================================
// Compute Shader(SkinCompute.hlsl)가 채워놓은 스키닝 결과를
// Vertex Shader 단에서 SV_VertexID로 인덱싱해 읽기 위한 공용 헬퍼.
//
// 슬롯: t30 (material t0~t7, shadow t21~t23, lighting t24~t25와 충돌 없음)
// =============================================================================

#include "Common/VertexLayouts.hlsli"

// SkinCompute.hlsl의 SkinOutputVertex와 1:1 일치
struct SkinnedVertex
{
    float3 position;
    float3 normal;
    float4 color;
    float2 uv;
    float4 tangent;
};

StructuredBuffer<SkinnedVertex> SkinCache : register(t30);

// 기존 VS_Input_PNCTT 모양으로 변환해서 반환 → 기존 VS 본문 재사용 가능
VS_Input_PNCTT ReadSkinnedVertex(uint vid)
{
    SkinnedVertex v = SkinCache[vid];

    VS_Input_PNCTT input;
    input.position = v.position;
    input.normal   = v.normal;
    input.color    = v.color;
    input.texcoord = v.uv;
    input.tangent  = v.tangent;
    return input;
}

#endif // SKIN_CACHE_ACCESS_HLSL
