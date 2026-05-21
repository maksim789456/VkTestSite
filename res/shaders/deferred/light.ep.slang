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

float3 TurboColormap(float x)
{
    // Src: https://research.google/blog/turbo-an-improved-rainbow-colormap-for-visualization/

    const float4 kRed4 = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const float4 kGreen4 = float4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    const float4 kBlue4 = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const float2 kRed2 = float2(-152.94239396, 59.28637943);
    const float2 kGreen2 = float2(4.27729857, 2.82956604);
    const float2 kBlue2 = float2(-89.90310912, 27.34824973);

    x = clamp(x, 0, 1);
    float4 v4 = float4( 1.0, x, x * x, x * x * x);
    float2 v2 = v4.zw * v4.z;
    return float3(
        dot(v4, kRed4)   + dot(v2, kRed2),
        dot(v4, kGreen4) + dot(v2, kGreen2),
        dot(v4, kBlue4)  + dot(v2, kBlue2)
    );
}

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
          color.rgb = TurboColormap(length(L));
          break;
        case 2:
          color.rgb = abs(fragPos) * 0.1;
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