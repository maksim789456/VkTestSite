#define CLUSTER_DIMENSION 80u
#define INV_CLUSTER_DIMENSION 0.0125f

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 UV;
}

struct UBO {
  float4 viewPos;
  float4x4 viewProj;
  float4x4 invViewProj;
  float4 projInfo; // width, height, zNear, zFar
  int4 debugInfo; //debug target, xSlice, ySlice, tmpLightCount
}

[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;

[shader("vertex")]
VSOutput vertexMain(uint id: SV_VertexID)
{
    VSOutput out;
    out.UV = float2((id << 1) & 2, id & 2);
    out.Pos = float4(out.UV * 2.0f - 1.0f, 0.0f, 1.0f);
    return out;
}

float4 ComputeClusterPlaneNDC(uint2 clusterID, float2 screenWH1)
{
    // Compute X bounds
    float xLeft = clusterID.x * CLUSTER_DIMENSION * screenWH1.x * 2.0f - 1.0f;
    float xRight = (clusterID.x * CLUSTER_DIMENSION + CLUSTER_DIMENSION) * screenWH1.x * 2.0f - 1.0f;

    // Compute Y bounds (flip Y for top-left origin)
    float yTop = 1.0f - clusterID.y * CLUSTER_DIMENSION * screenWH1.y * 2.0f;
    float yBottom = 1.0f - (clusterID.y * CLUSTER_DIMENSION + CLUSTER_DIMENSION) * screenWH1.y * 2.0f;

    // Return as left, bottom, right, top in planeNDC convention (x/z, y/w)
    return float4(xRight, yTop, xLeft, yBottom);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    if (ubo.debugInfo.y == -1 || ubo.debugInfo.z == -1)
        return float4(.0f);

    // Convert pixel coord → NDC
    float2 ndc = float2(
        (input.Pos.x / ubo.projInfo.x) * 2.0 - 1.0,
        1.0 - (input.Pos.y / ubo.projInfo.y) * 2.0
    );

    uint2 tId = uint2(ubo.debugInfo.y, ubo.debugInfo.z);
    float2 screenWH1 = 1.0f / ubo.projInfo.xy;
    float4 planeNDC = ComputeClusterPlaneNDC(tId.xy, screenWH1);

    // Check if current pixel in cluster
    bool inCluster = ndc.x >= planeNDC.z && ndc.x <= planeNDC.x     // X: left..right
                     && ndc.y >= planeNDC.w && ndc.y <= planeNDC.y; // Y: bottom..top

    if (inCluster)
        return float4(1, 0, 0, 1); // red cluster
    else
        return float4(0, 0, 0, 1); // black background
}