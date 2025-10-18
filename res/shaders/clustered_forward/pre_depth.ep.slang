struct VSInput
{
    [[vk::location(0)]] float3 Pos;
}

struct VSOutput
{
    float4 Pos : SV_Position;
}

struct UBO {
  float4 viewPos;
  float4x4 viewProj;
  float4x4 invViewProj;
  float4 projInfo; // width, height, zNear, zFar
  uint32_t displayDebugTarget;
}
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;

[shader("vertex")]
float4 vertexMain(VSInput input, uniform float4x4 pushModel) : SV_Position
{
    float4 worldPos = mul(pushModel, float4(input.Pos, 1.0));
    return mul(ubo.viewProj, worldPos);
}

[shader("fragment")]
void fragmentMain()
{}