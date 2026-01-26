import lighting;

#define XSLICES 16
#define YSLICES 9
#define ZSLICES 24
#define MAX_LIGHTS_PER_CLUSTER 32
#define INV_CLUSTER_DIMENSION 0.0125f

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
    [[vk::location(1)]] float3 ViewPos;
    [[vk::location(2)]] float4 posNDC;
    [[vk::location(3)]] float3 Normal;
    [[vk::location(4)]] float2 TexCoord;
    [[vk::location(5)]] float4 Color;
    [[vk::location(6)]] uint AlbedoIdx;
    [[vk::location(7)]] uint NormalIdx;
}

struct UBO {
  float4 viewPos;
  float4x4 viewProj;
  float4x4 invViewProj;
  float4 projInfo; // width, height, zNear, zFar
  int4 debugInfo; //debug target, xSlice, ySlice, tmpLightCount
}
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;
[[vk::binding(1, 0)]] StructuredBuffer<Light> lights;
[[vk::binding(2, 0)]] Sampler2D textures[];

//[[vk::binding(3, 0)]] StructuredBuffer<uint> clusterLightCounts;
[[vk::binding(4, 0)]] StructuredBuffer<uint> clusterLightIndices;

[shader("vertex")]
VSOutput vertexMain(VSInput input, uniform float4x4 pushModel)
{
    float4 worldPos = mul(pushModel, float4(input.Pos, 1.0));
    VSOutput out;
    out.WorldPos = worldPos.xyz;
    out.ViewPos = mul(ubo.viewProj, worldPos).xyz;
    out.Pos = mul(ubo.viewProj, worldPos);
    out.posNDC = out.Pos;
    out.Normal = input.Normal;
    out.TexCoord = input.TexCoord;
    out.Color = input.Color;
    out.AlbedoIdx = input.AlbedoIdx;
    out.NormalIdx = input.NormalIdx;
    return out;
}

uint GetClusterIdx(uint3 cluster)
{
    return (YSLICES * cluster.x + cluster.y) * ZSLICES + cluster.z;
}

float LinearEyeDepth(float depth01)
{
    float invNear = 1.0 / ubo.projInfo.z;
    float invFar = 1.0 / ubo.projInfo.w;

    // 1/z = depth01 * (1/near - 1/far) + 1/far
    float invZ = depth01 * (invNear - invFar) + invFar;

    // avoid division by zero
    return 1.0 / max(invZ, 1e-6);
}

uint GetClusterSlice(float linearDepth)
{
    // EQ: floor( log(Z) * numSlices / (log(FarZ/NearZ)) - numSlices * log(NearZ) / log(FarZ/NearZ) )
    // From: http://www.aortiz.me/2018/12/21/CG.html#part-2 and DOOM 2016 SIGGRAPH
    float zAtt = log(ubo.projInfo.w / ubo.projInfo.z);
    return (uint)floor(log(linearDepth) * ZSLICES / zAtt - ZSLICES * log(ubo.projInfo.z) / zAtt);
}

uint2 GetClusterXYFromNDC(float3 positionNDC)
{
    // Full EQ: floor((positionNDC.xy * 0.5f + 0.5f) * _ScreenParams.xy * INV_CLUSTER_DIMENSION);
    return (uint2)floor((positionNDC.xy * 0.5f + 0.5f) * ubo.projInfo.xy * INV_CLUSTER_DIMENSION);
}

uint3 GetClusterFromNDC(float3 positionNDC, float linearDepth)
{
    uint3 cluster;
    cluster.z = GetClusterSlice(linearDepth);
    cluster.xy = GetClusterXYFromNDC(positionNDC);
    return cluster;
}

float3 SliceToColor(uint slice)
{
    // Simple hash into HSV
    float h = fract(float(slice) * 0.04166666666f);  // repeat every ~14 slices
    float s = 1.0;
    float v = 1.0;

    // HSV → RGB
    float i = floor(h * 6.0);
    float f = h * 6.0 - i;
    float p = v * (1.0 - s);
    float q = v * (1.0 - f * s);
    float t = v * (1.0 - (1.0 - f) * s);

    if (i == 0.0) return float3(v, t, p);
    if (i == 1.0) return float3(q, v, p);
    if (i == 2.0) return float3(p, v, t);
    if (i == 3.0) return float3(p, q, v);
    if (i == 4.0) return float3(t, p, v);
    return float3(v, p, q);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
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

    float3 positionNDC = input.posNDC.xyz / input.posNDC.w;
    float linearDepth = LinearEyeDepth(input.ViewPos.z);
    uint clusterZ = GetClusterSlice(linearDepth);
    uint2 clusterXY = GetClusterXYFromNDC(positionNDC);

    uint3 cluster = uint3(clusterXY, clusterZ);
    uint clusterIdx = GetClusterIdx(cluster);
    uint clusterDataIdx = (MAX_LIGHTS_PER_CLUSTER + 1u) * clusterIdx;;
    uint lightCount = clusterLightIndices[clusterDataIdx];
    float3 L = float3(0.0);
    for (uint i = 0; i < lightCount; ++i)
    {
        uint lightIndex = clusterDataIdx + i;
        Light light = lights[i];
        L += light.apply(input.WorldPos, tnorm);
    }

    float3 ambient = 0.1 * albedo.rgb;
    float3 result = ambient + L * albedo.rgb;

#ifdef DEBUG
    if (ubo.debugInfo.x > 0) {
      float3 color;
      switch (ubo.debugInfo.x) {
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

                color = lerp(result, color, 0.2);
        }
          break;
        case 5:
            color = SliceToColor(clusterZ);
            color = lerp(ambient, color, 0.2);
            break;
      }
      return float4(color, 1.0);
    }
#endif

    return float4(result, 1.0);
}