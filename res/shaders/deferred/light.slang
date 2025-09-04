struct UBO {
  float4 viewPos;
  float4x4 viewProj;
  float4x4 invViewProj;
}
[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;

[[vk::binding(1, 0)]] SubpassInput depthInput;
[[vk::binding(2, 0)]] SubpassInput albedoInput;
[[vk::binding(3, 0)]] SubpassInput normalInput;

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV;
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
float4 fragmentMain(VSOutput input) : SV_Target
{
    float depth = depthInput.SubpassLoad().r;
    float4 ndc = float4(input.UV * 2.0f - 1.0f, depth * 2.0f - 1.0f);

    float4 worldPosH = mul(ubo.invViewProj, ndc);
    float3 fragPos = worldPosH.xyz / worldPosH.w;

    float3 normal = normalize(normalInput.SubpassLoad().rgb);
    float3 albedo = albedoInput.SubpassLoad().rgb;

    float3 lightDir = normalize(ubo.viewPos.xyz - fragPos); //TODO: lightPos instead view
    float3 viewDir = normalize(ubo.viewPos.xyz - fragPos);
    float3 halfwayDir = normalize(lightDir + viewDir);

    float3 ambient = 0.1 * albedo;

    float3 diffuse = 0.8 * max(dot(normal, lightDir), 0.0) * albedo;

    float shininess = 32.0;
    float3 specularColor = float3(1.0);
    float spec = 0.11 * pow(max(dot(normal, halfwayDir), 0.0), shininess);
    float3 specular = spec * specularColor;

    float3 result = albedo * (ambient + diffuse + specular);

    return float4(result, 1.0);
}