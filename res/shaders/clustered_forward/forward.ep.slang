import lighting;

#define XSLICES 16
#define YSLICES 9
#define ZSLICES 24
#define MAX_LIGHTS_PER_CLUSTER 64

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
  float4 projInfo; // width, height, zNear, zFar
  uint32_t displayDebugTarget;
}
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;
[[vk::binding(1, 0)]] StructuredBuffer<Light> lights;
[[vk::binding(2, 0)]] Sampler2D textures[];

[[vk::binding(3, 0)]] StructuredBuffer<uint> clusterLightCounts;
[[vk::binding(4, 0)]] StructuredBuffer<uint> clusterLightIndices;

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

float computeZSlice(float viewZ)
{
    // Inverted Z: smaller viewZ = farther
    float zLinear = -viewZ;
    float nz = (zLinear - ubo.projInfo.w) / (ubo.projInfo.z - ubo.projInfo.w);
    nz = saturate(nz);
    return nz * ZSLICES;
}

uint computeClusterIndex(float4 fragCoord)
{
    uint cx = uint(fragCoord.x / (ubo.projInfo.x / XSLICES));
    uint cy = uint(fragCoord.y / (ubo.projInfo.y / YSLICES));
    float zSliceF = computeZSlice(fragCoord.z);
    uint cz = uint(clamp(floor(zSliceF), 0, ZSLICES - 1));

    return cz * (XSLICES * YSLICES) + cy * XSLICES + cx;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    uint clusterIdx = computeClusterIndex(input.Pos);
    uint lightCount = clusterLightCounts[clusterIdx];

    float3 albedo = textures[NonUniformResourceIndex(input.AlbedoIdx)].Sample(input.TexCoord).rgb;
    if (input.AlbedoIdx == 99) {
      albedo = input.Color.rgb;
    }

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
    float3 tangentNormal = normalize(normal * 2.0 - 1.0);
    float3 tnorm = normalize(mul(TBN, tangentNormal));

    float3 L = float3(0.0);
    for (uint i = 0; i < lightCount; ++i)
    {
        uint lightIndex = clusterLightIndices[clusterIdx * ZSLICES + i];
        Light light = lights[lightIndex];
        L += light.apply(input.WorldPos, tnorm);
    }

    float3 ambient = 0.1 * albedo.rgb;
    float3 result = ambient + L * albedo.rgb;

#ifdef DEBUG
    if (ubo.displayDebugTarget > 0) {
      float3 color;
      switch (ubo.displayDebugTarget) {
        case 1:
          color.rgb = L;
          break;
        case 2:
          color.rgb = input.Color.xyz;
          break;
        case 3:
          color.rgb = tnorm;
          break;
        case 4:
        if (lightCount > 0) {
        float intensity = clamp(float(lightCount) / float(MAX_LIGHTS_PER_CLUSTER), 0.0, 1.0);
                if (intensity < 0.25)
                  color = lerp(float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 1.0), intensity * 4.0);
                else if (intensity < 0.5)
                  color = lerp(float3(0.0, 1.0, 1.0), float3(0.0, 1.0, 0.0), (intensity - 0.25) * 4.0);
                else if (intensity < 0.75)
                  color = lerp(float3(0.0, 1.0, 0.0), float3(1.0, 1.0, 0.0), (intensity - 0.5) * 4.0);
                else
                  color = lerp(float3(1.0, 1.0, 0.0), float3(1.0, 0.0, 0.0), (intensity - 0.75) * 4.0);
        }

        color = lerp(result, color, 0.2);
          break;
      }
      return float4(color, 1.0);
    }
#endif

    return float4(result, 1.0);
}