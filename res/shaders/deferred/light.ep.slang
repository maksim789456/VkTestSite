import lighting;

struct UBO {
  float4 viewPos;
  float4x4 viewProj;
  float4x4 invViewProj;
  uint32_t displayDebugTarget;
}
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;

[[vk::binding(1, 0)]] StructuredBuffer<Light> lights;

[[vk::binding(2, 0)]] SubpassInput depthInput;
[[vk::binding(3, 0)]] SubpassInput albedoInput;
[[vk::binding(4, 0)]] SubpassInput normalInput;

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV;
};

[shader("vertex")]
VSOutput vertexMain(uint VertexIndex: SV_VertexID)
{
    VSOutput out;
    out.UV = float2((VertexIndex << 1) & 2, VertexIndex & 2);
    out.Pos = float4(out.UV * 2.0f - 1.0f, 0.0f, 1.0f);
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, uniform uint pushLightCount) : SV_Target
{
    float depth = depthInput.SubpassLoad().r;
    float4 ndc = float4(input.UV * 2.0f - 1.0f, depth, 1.0);

    float4 worldPosH = mul(ubo.invViewProj, ndc);
    float3 fragPos = worldPosH.xyz / worldPosH.w;

    float3 normal = normalize(normalInput.SubpassLoad().rgb);
    float4 albedo = albedoInput.SubpassLoad();

    float3 L = float3(0.0);
    for (uint i = 0; i < pushLightCount; i++) {
      Light light = lights[i];
      L += light.apply(fragPos, normal);
    }

#ifdef DEBUG
    if (ubo.displayDebugTarget > 0) {
      float3 color;
      switch (ubo.displayDebugTarget) {
        case 1:
          color.rgb = L;
          break;
        case 2:
          color.rgb = albedo.rgb;
          break;
        case 3:
        case 4:
        case 5:
        case 6:
          color.rgb = normal;
          break;
      }
      return float4(color, 1.0);
    }
#endif

    float3 ambient = 0.1 * albedo.rgb; // TODO: extract ambient coefficient to ubo
    float3 result = ambient + L * albedo.rgb;

    return float4(result, 1.0);
}