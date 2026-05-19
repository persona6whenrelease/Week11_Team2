// =============================================================================
// BoneWeightHeatmap_Skinned.hlsl — GPU Skinning(Compute Cache) 경로 Heatmap
// =============================================================================
// VS만 다름: SkinCache StructuredBuffer(t30)에서 SV_VertexID로 정점을 읽음.
// Compute 단계에서 미리 Heatmap 색을 vertex color에 박아둠 → PS는 그대로 사용.
// =============================================================================

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SkinCacheAccess.hlsli"

struct VSOut
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

VSOut VS(uint vid : SV_VertexID)
{
    VS_Input_PNCTT input = ReadSkinnedVertex(vid);

    VSOut output;
    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.color = input.color;
    return output;
}

float4 PS(VSOut input) : SV_TARGET
{
#if defined(BONE_WEIGHT_HEATMAP_WIRE) && BONE_WEIGHT_HEATMAP_WIRE
    return float4(1.0f, 1.0f, 1.0f, 0.45f);
#else
    return saturate(input.color);
#endif
}
