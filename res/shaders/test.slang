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
    [[vk::location(6)]] uint TexIdx;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input, uniform float4x4 pushModel)
{
    float4 worldPos = mul(pushModel, float4(input.inPos, 1.0));
    VSOutput out;
    out.Pos = mul(ubo.viewProj, worldPos);
    out.Normal = input.inNormal;
    out.TexCoord = input.inTexCoord;
    out.FragPos = worldPos.xyz;
    out.ViewPos = ubo.viewPos.xyz;
    out.LightPos = ubo.viewPos.xyz;
    out.Color = input.inColor;
    out.TexIdx = input.inTexIdx;
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 lightColor = float3(1.0);

    float3 albedo = textures[NonUniformResourceIndex(input.TexIdx)].Sample(input.TexCoord).rgb;
    if (input.TexIdx == 99) {
      albedo = input.Color.rgb;
    }

    float3 normal = normalize(input.Normal);
    float3 lightDir = normalize(input.LightPos - input.FragPos);
    float3 viewDir = normalize(input.ViewPos - input.FragPos);
    float3 halfwayDir = normalize(lightDir + viewDir);

    float3 ambient = 0.1 * albedo;

    float3 diffuse = 0.8 * max(dot(normal, lightDir), 0.0) * albedo;

    float shininess = 32.0;
    float3 specularColor = float3(1.0);
    float spec = 0.11 * pow(max(dot(normal, halfwayDir), 0.0), shininess);
    float3 specular = spec * specularColor;

    float3 result = lightColor * (ambient + diffuse + specular);
    float gamma = 1.0 / 2.2;
    result.rgb = pow(result.rgb, float3(gamma, gamma, gamma));

    return float4(result, 1.0);
}