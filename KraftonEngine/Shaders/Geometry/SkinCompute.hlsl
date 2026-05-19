#include "Common/ConstantBuffers.hlsli"

// 입력: FVertexPNCTT_Skinned 
struct SkinSourceVertex
{
    float3 position;
    float3 normal;
    float4 color;
    float2 uv;
    float4 tangent;
    uint4 boneIDs;
    float4 boneWeights;
};

// 출력: FVertexPNCTT (스키닝 완료된 정점 - VS가 읽음)
struct SkinOutputVertex
{
    float3 position;
    float3 normal;
    float4 color;
    float2 uv;
    float4 tangent;
};

// === 바인딩 ===
StructuredBuffer<SkinSourceVertex> SourceVertices : register(t0);
RWStructuredBuffer<SkinOutputVertex> SkinCache : register(u0);

// === Compute Main === 
[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    const uint vid = DTid.x;
    // 64로 올림한 스레드 수가 정점보다 많을 수 있음 -> 범위 밖 스레드는 즉시 종료
    if (vid >= NumSkinningVertices) return;
    
    SkinSourceVertex src = SourceVertices[vid];
    
    float3 skinnedPos = float3(0, 0, 0);
    float3 skinnedNormal = float3(0, 0, 0);
    float totalW = 0.0f;
    
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float w = src.boneWeights[i];
        if (w <= 0.0f) continue;
        
        // 인덱스 가드 (잘못된 본ID로 인한 cbuffer 범위 초과 방지)
        uint boneIdx = src.boneIDs[i];
        if (boneIdx >= MAX_SKINNING_BONES) continue;
        
        float4x4 m = BoneMatrices[boneIdx];
        skinnedPos += mul(float4(src.position, 1.0f), m).xyz * w;
        skinnedNormal += mul(float4(src.normal,   0.0f), m).xyz * w;
        totalW += w;
    }
    
    SkinOutputVertex outVtx;
    if (totalW > 0.0f)
    {
        outVtx.position = skinnedPos;
        outVtx.normal   = normalize(skinnedNormal);
    }
    else
    {
        // 본 영향 없는 정점은 bind pose 그대로
        outVtx.position = src.position;
        outVtx.normal   = src.normal;
    }
    outVtx.color = src.color;
    outVtx.uv = src.uv;
    outVtx.tangent = src.tangent;
    
    SkinCache[vid] = outVtx;
}