struct VSInput
{
    [[vk::location(0)]] float3 Pos;
    [[vk::location(1)]] float3 Normal;
    [[vk::location(3)]] float2 TexCoord;
    [[vk::location(4)]] float4 Color;
    [[vk::location(5)]] uint AlbedoIdx;
    [[vk::location(6)]] uint NormalIdx;
};

struct VSOutput
{
    float4 Pos : SV_Position;
    [[vk::location(0)]] float3 WorldPos;
    [[vk::location(1)]] float3 Normal;
    [[vk::location(3)]] float2 TexCoord;
    [[vk::location(4)]] float4 Color;
    [[vk::location(5)]] uint AlbedoIdx;
    [[vk::location(6)]] uint NormalIdx;
}

struct UBO {
  float4 viewPos;
  float4x4 viewProj;
  float4x4 invViewProj;
  uint32_t displayDebugTarget;
}
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;
[[vk::binding(1, 0)]] Sampler2D textures[];

struct FSOutput
{
	float4 Albedo;
	float4 Normal;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input, uniform float4x4 pushModel)
{
    float4 worldPos = mul(pushModel, float4(input.Pos, 1.0));
    VSOutput out;
    out.Pos = mul(ubo.viewProj, worldPos);
    out.WorldPos = worldPos.xyz;
    out.Normal = input.Normal;
    out.TexCoord = input.TexCoord;
    out.Color = input.Color;
    out.AlbedoIdx = input.AlbedoIdx;
    out.NormalIdx = input.NormalIdx;
    return out;
}

[shader("fragment")]
FSOutput fragmentMain(VSOutput input)
{
    FSOutput out;
    float3 albedo = textures[NonUniformResourceIndex(input.AlbedoIdx)].Sample(input.TexCoord).rgb;
    if (input.AlbedoIdx == 99) {
      albedo = input.Color.rgb;
    }

    out.Albedo = float4(albedo, 1.0);

    float3 N = normalize(input.Normal);

    float3 Q1  = ddx(input.WorldPos);
    float3 Q2  = ddy(input.WorldPos);
    float2 st1 = ddx(input.TexCoord);
    float2 st2 = ddy(input.TexCoord);

    float3 T = normalize(Q1 * st2.y - Q2 * st1.y);
    float3 B = normalize(cross(N, T));
    float3x3 TBN = float3x3(T, B, N);

    float3 normal = textures[NonUniformResourceIndex(input.NormalIdx)].Sample(input.TexCoord).rgb;
    if (input.NormalIdx == 99) {
      normal = float3(0.5, 0.5, 1.0);
    }

    if (ubo.displayDebugTarget >= 4) {
      float3 color;
      switch (ubo.displayDebugTarget) {
        case 4:
          color.rgb = N;
          break;
        case 5:
          color.rgb = T;
          break;
        case 6:
          color.rgb = B;
          break;
      }
      out.Normal = float4(color, 0.0);
      return out;
    }

    float3 tangentNormal = normalize(normal * 2.0 - 1.0);
    float3 tnorm = normalize(mul(TBN, tangentNormal));
    out.Normal = float4(tnorm, 0.0);
    return out;
}