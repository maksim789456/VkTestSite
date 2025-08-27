struct VSInput
{
    [[vk::location(0)]] float3 inPos;
    [[vk::location(1)]] float3 inNormal;
    [[vk::location(2)]] float3 inTangent;
    [[vk::location(3)]] float2 inTexCoord;
    [[vk::location(4)]] float4 inColor;
    [[vk::location(5)]] uint inAlbedoIdx;
    [[vk::location(6)]] uint inNormalIdx;
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
    [[vk::location(1)]] float3 Tangent;
    [[vk::location(2)]] float2 TexCoord;
    [[vk::location(3)]] float3 FragPos;
    [[vk::location(4)]] float3 ViewPos;
    [[vk::location(5)]] float3 LightPos;
    [[vk::location(6)]] float4 Color;
    [[vk::location(7)]] uint AlbedoIdx;
    [[vk::location(8)]] uint NormalIdx;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input, uniform float4x4 pushModel)
{
    float4 worldPos = mul(pushModel, float4(input.inPos, 1.0));
    VSOutput out;
    out.Pos = mul(ubo.viewProj, worldPos);
    out.Normal = input.inNormal;
    out.Tangent = input.inTangent;
    out.TexCoord = input.inTexCoord;
    out.FragPos = worldPos.xyz;
    out.ViewPos = ubo.viewPos.xyz;
    out.LightPos = ubo.viewPos.xyz;
    out.Color = input.inColor;
    out.AlbedoIdx = input.inAlbedoIdx;
    out.NormalIdx = input.inNormalIdx;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 lightColor = float3(1.0);

    float3 albedo = textures[NonUniformResourceIndex(input.AlbedoIdx)].Sample(input.TexCoord).rgb;
    if (input.AlbedoIdx == 99) {
      albedo = input.Color.rgb;
    }

    float3 normal = textures[NonUniformResourceIndex(input.NormalIdx)].Sample(input.TexCoord).rgb;
    if (input.NormalIdx == 99) {
      normal = float3(0.5, 0.5, 1.0);
    }

    float3 N = normalize(input.Normal);
    float3 T = normalize(input.Tangent);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    float3 tnorm = mul(normalize(normal * 2.0 - float3(1.0, 1.0, 1.0)), TBN);

    float3 lightDir = normalize(input.LightPos - input.FragPos);
    float3 viewDir = normalize(input.ViewPos - input.FragPos);
    float3 halfwayDir = normalize(lightDir + viewDir);

    float3 ambient = 0.1 * albedo;

    float3 diffuse = 0.8 * max(dot(tnorm, lightDir), 0.0) * albedo;

    float shininess = 32.0;
    float3 specularColor = float3(1.0);
    float spec = 0.11 * pow(max(dot(tnorm, halfwayDir), 0.0), shininess);
    float3 specular = spec * specularColor;

    float3 result = lightColor * (ambient + diffuse + specular);

    return float4(result, 1.0);
}