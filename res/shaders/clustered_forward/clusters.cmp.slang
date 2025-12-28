import lighting;

#define XSLICES 16
#define YSLICES 9
#define ZSLICES 24

#define MAX_LIGHTS_PER_CLUSTER 32

#define DEPTH_SLICES 24u
#define INV_DEPTH_SLICES 0.04166f
#define CLUSTER_DIMENSION 80u
#define INV_CLUSTER_DIMENSION 0.0125f

#define MIP_LEVEL 4u
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT -1

#define DEBUG_VIEW_IS_CLUSTER_VALIDATION

struct CameraData {
  float4x4 view;
  float4x4 viewProj;
  float4x4 invViewProj;
  float4x4 invProj;
  float4 projInfo; // width, height, zNear, zFar
  float4 frustumCorners; // x01, y01, xEye, yEye
}

[[vk::binding(0, 0)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 0)]] StructuredBuffer<Light> lights;
[[vk::binding(2, 0)]] Sampler2D<float2> hiZDepth;

[[vk::binding(4, 0)]] RWStructuredBuffer<uint> clusters;
[[vk::binding(5, 0)]] RWTexture2D<float4> debugOut;

uint GetClusterIdx(uint3 groupResolutions, uint3 cluster)
{
    return (groupResolutions.y * cluster.x + cluster.y) * groupResolutions.z + cluster.z;
}

float GetClusterZNear(uint zSlice)
{
    // EQ: Z = NearZ (FarZ / NearZ) ^ (slice / numSlices)
    // From: http://www.aortiz.me/2018/12/21/CG.html#part-2 and DOOM 2016 SIGGRAPH
    return camera.projInfo.z * pow((camera.projInfo.w / camera.projInfo.z), (float)zSlice * INV_DEPTH_SLICES);
}

float Depth01ToEyeDepth(float depth01)
{
    return depth01 * camera.projInfo.w;
}

uint GetClusterSlice(float linearDepth)
{
    // EQ: floor( log(Z) * numSlices / (log(FarZ/NearZ)) - numSlices * log(NearZ) / log(FarZ/NearZ) )
    // From: http://www.aortiz.me/2018/12/21/CG.html#part-2 and DOOM 2016 SIGGRAPH
    float zAtt = log(camera.projInfo.w / camera.projInfo.z);
    return (uint)floor(log(linearDepth) * ZSLICES / zAtt - ZSLICES * log(camera.projInfo.z) / zAtt);
}

uint2 GetClusterXYFromNDC(float3 positionNDC)
{
    // Full EQ: floor((positionNDC.xy * 0.5f + 0.5f) * _ScreenParams.xy * INV_CLUSTER_DIMENSION);
    return (uint2)floor((positionNDC.xy * 0.5f + 0.5f) * camera.projInfo.xy * INV_CLUSTER_DIMENSION);
}

uint3 GetClusterFromNDC(float3 positionNDC, float linearDepth)
{
    uint3 cluster;
    cluster.z = GetClusterSlice(linearDepth);
    cluster.xy = GetClusterXYFromNDC(positionNDC);
    return cluster;
}

void MapCluserZIdxAtView(uint tIdZ, out uint xMin, out uint xMax)
{
    const uint OUT = 80; // vertical resolution to fill
    const uint Z = 24;   // number of z-slices

    xMin = (tIdZ * OUT) / Z;
    xMax = ((tIdZ + 1) * OUT) / Z;
}

struct AABB
{
    float3 centerVS;
    float3 extentsVS;
};

// Compute cluster bounds in NDC for a given cluster ID
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

AABB GetFrustumAABBFromNDC(float4 planeNDC, float zNear, float zFar)
{
    AABB result;

    // For each side, select depth in order to get min/max of frustum points
    const float4 frustumPointSelection = float4(
        min(planeNDC.z * zNear, planeNDC.x * zFar), // left/right min
        max(planeNDC.w * zNear, planeNDC.y * zFar), // bottom/top max
        max(planeNDC.z * zNear, planeNDC.x * zFar), // left/right max
        min(planeNDC.w * zNear, planeNDC.y * zFar) // bottom/top min
    );

    const float3 minAABB = float3(camera.frustumCorners.xy * frustumPointSelection.xy, zNear);
    const float3 maxAABB = float3(camera.frustumCorners.xy * frustumPointSelection.zw, zFar);

    result.centerVS = (minAABB + maxAABB) * 0.5f;
    result.extentsVS = (maxAABB - minAABB) * 0.501f;
    return result;
}

bool AABBSphereIntersection(float3 spherePos, float sphereRad, float3 aabbCenter, float3 aabbExtents)
{
    float3 displ = max(0, abs(aabbCenter - spherePos) - aabbExtents);
    float sdfSqr = dot(displ, displ);
    return sdfSqr <= sphereRad * sphereRad;
}

[numthreads(4, 4, 1)]
[shader("compute")]
void cmpMain(uint3 tId : SV_DispatchThreadID, uniform uint lightCount)
{
    uint4 clusterGroupResolutions = uint4(XSLICES, YSLICES, ZSLICES, 0); //clusterData.clusterGroupResolutions;
    if (any(tId.xyz >= clusterGroupResolutions.xyz))
        return;

    uint zSlice = tId.z;
    float zNear = GetClusterZNear(zSlice);
    float zFar = GetClusterZNear(zSlice + 1u);

    uint2 hiZUV = tId.xy * (MIP_LEVEL + 1u);
    float tileMaxDepth = Depth01ToEyeDepth(hiZDepth.Load(int3(hiZUV, MIP_LEVEL)).g);
    // bool isOccluded = tileMaxDepth == 0.0f ? false : (tileMaxDepth < zNear);
    bool isOccluded = (tileMaxDepth < zNear);

    uint clusterIdx = GetClusterIdx(clusterGroupResolutions.xyz, tId);
    uint clusterDataIdx = (MAX_LIGHTS_PER_CLUSTER + 1u) * clusterIdx;

    uint nextLightIdx = 0u;

    float2 screenWH1 = 1.0f / camera.projInfo.xy;
    float4 planeNDC = ComputeClusterPlaneNDC(tId.xy, screenWH1);

    AABB aabb = GetFrustumAABBFromNDC(planeNDC, zNear, zFar);

    // Derive frustum planes
    // #0 faces to the right, and the rest continue in CCW fashion
    float3 frustumPlanes[4];

    // Get 4 corners of cluster at z=1 (view-space tangent plane)
    float3 cornerTL = float3(planeNDC.z * camera.frustumCorners.x, planeNDC.y * camera.frustumCorners.y, -1.f); // top-left
    float3 cornerTR = float3(planeNDC.x * camera.frustumCorners.x, planeNDC.y * camera.frustumCorners.y, -1.f); // top-right
    float3 cornerBL = float3(planeNDC.z * camera.frustumCorners.x, planeNDC.w * camera.frustumCorners.y, -1.f); // bottom-left
    float3 cornerBR = float3(planeNDC.x * camera.frustumCorners.x, planeNDC.w * camera.frustumCorners.y, -1.f); // bottom-right

    // Planes pointing inward
    frustumPlanes[0] = normalize(cross(cornerTR, cornerBR)); // right
    frustumPlanes[1] = normalize(cross(cornerBL, cornerTL)); // top
    frustumPlanes[2] = normalize(cross(cornerBL, cornerBR)); // left
    frustumPlanes[3] = normalize(cross(cornerTR, cornerTL)); // bottom

    [branch]
    if (!isOccluded)
    {
        [loop]
        for (uint i = 0u; i < lightCount; ++i)
        {
            if (nextLightIdx >= MAX_LIGHTS_PER_CLUSTER)
                break;
            Light light = lights[i];
            float3 pos = float3(0.f);
            float radius = 0.f;
            if (light.position.w == LIGHT_POINT)
            {
                pos = light.position.xyz;
                radius = light.computeLightRadius(0.01f);
            }
            else if (light.position.w != LIGHT_SPOT)
            {
                pos = light.position.xyz;
                radius = light.computeLightRadius(0.01f) * 0.5f;
            }

            bool inFrustum = true;

            [unroll]
            for (uint j = 0u; j < 4u; ++j)
            {
                float d = dot(frustumPlanes[j], pos);
                inFrustum = inFrustum && (d < radius);
            }

            inFrustum = inFrustum && AABBSphereIntersection(pos, radius, aabb.centerVS, aabb.extentsVS);

            inFrustum = inFrustum || (light.position.w == LIGHT_DIRECTIONAL); // always show directional lights

            [branch]
            if (inFrustum)
            {
                clusters[clusterDataIdx + nextLightIdx + 1u] = i;
                nextLightIdx++;
            }
        }
    }

    clusters[clusterDataIdx] = nextLightIdx;

#if defined(DEBUG_VIEW_IDX)
    
    
#elif defined(DEBUG_VIEW_OCCLUSION)
    // Show occlustion per every z cluster
    for (uint y = 0; y < CLUSTER_DIMENSION; ++y)
    {
        uint y2 = CLUSTER_DIMENSION - y;
        uint xMin, xMax;
        MapCluserZIdxAtView(tId.z, xMin, xMax);
        for (uint px = xMin; px < xMax; ++px)
        {
            uint2 outId = tId.xy * CLUSTER_DIMENSION + uint2(px, y2);
            debugOut[outId] = !isOccluded ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
            //  Draw grid
            if (y == 0u || px == 0u)
            {
                debugOut[outId] = 1.f;
            }
        }
    }
#elif defined(DEBUG_VIEW_IS_CLUSTER_VALIDATION)
    // Validate
    float2 screenUV = (tId.xy * CLUSTER_DIMENSION + CLUSTER_DIMENSION * 0.5f) * (1.0f / camera.projInfo.xy);
    float3 positionNDC = float3(screenUV * 2.f - 1.f, 0.f);
    float linearDepth = lerp(zNear, zFar, 0.01f);
    uint3 cluster = GetClusterFromNDC(positionNDC, linearDepth);

    bool isValid = all(cluster.xyz == tId.xyz);

    for (uint y = 0; y < CLUSTER_DIMENSION; ++y)
    {
        uint y2 = CLUSTER_DIMENSION - y;
        uint xMin, xMax;
        MapCluserZIdxAtView(tId.z, xMin, xMax);
        for (uint px = xMin; px < xMax; ++px)
        {
            uint2 outId = tId.xy * CLUSTER_DIMENSION + uint2(px, y2);
            debugOut[outId] = isValid ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
            // debugOut[outId] = saturate(positionNDC.y);
            //  Draw grid
            if (y == 0u || px == 0u)
            {
                debugOut[outId] = 1.f;
            }
        }
    }
#endif
}