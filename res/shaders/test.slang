static float2 pos[3] = {
    float2(0.0, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5)
};

static float3 colors[3] = {
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
};

struct VSInput
{
    [[vk::location(0)]] float3 inPos;
    [[vk::location(1)]] float3 inNormal;
    [[vk::location(2)]] float2 inTexCoord;
    [[vk::location(3)]] float4 inColor;
    [[vk::location(4)]] uint inTexIdx;
    uint vertexID: SV_VertexID;
};

struct VSOutput
{
    float4 Pos : SV_Position;
    [[vk::location(0)]] float3 Color;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput out;
    out.Pos = float4(pos[input.vertexID], 0.0, 1.0);
    out.Color = colors[input.vertexID];
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(input.Color, 1.0);
}