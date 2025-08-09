struct VSInput
{
    [[vk::location(0)]] float3 inPos;
    [[vk::location(1)]] float3 inNormal;
    [[vk::location(2)]] float2 inTexCoord;
    [[vk::location(3)]] float4 inColor;
    [[vk::location(4)]] uint inTexIdx;
};

struct UBO {
  float4 viewPos;
  float4x4 viewProj;
}
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;
[[vk::binding(1, 0)]] Sampler2D textures[];

struct VSOutput
{
    float4 Pos : SV_Position;
    [[vk::location(0)]] float3 Normal;
    [[vk::location(1)]] float2 TexCoord;
    [[vk::location(2)]] float3 FragPos;
    [[vk::location(3)]] float3 ViewPos;
    [[vk::location(4)]] float3 LightPos;
    [[vk::location(5)]] float4 Color;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input/*, uniform float4x4 pushModel*/)
{
    float4 worldPos = float4(input.inPos, 1.0);
    //float4 diff = textures[NonUniformResourceIndex(input.inTexIdx)].Sample(input.inTexCoord);
    VSOutput out;
    out.Pos = mul(ubo.viewProj, worldPos);
    out.FragPos = worldPos.xyz;
    out.Color = float4(1.0, 0.0, 0.0, 1.0);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return input.Color;
}