#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"

struct VSOut
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VSOut VS(VS_Input_PNCTT input)
{
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
