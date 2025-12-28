import lighting;

#define XSLICES 16
#define YSLICES 9
#define ZSLICES 24
#define MAX_LIGHTS_PER_CLUSTER 64
#define WORKGROUP_SIZE 64

struct CameraData {
  float4x4 view;
  float4x4 viewProj;
  float4x4 invViewProj;
  float4 projInfo; // width, height, zNear, zFar
}
[[vk::binding(0, 0)]] ConstantBuffer<CameraData> camera;
[[vk::binding(1, 0)]] StructuredBuffer<Light> lights;
[[vk::binding(2, 0)]] Sampler2D depth;

[[vk::binding(3, 0)]]RWStructuredBuffer<uint> clusterCounts;
[[vk::binding(4, 0)]]RWStructuredBuffer<uint> clusterLightIndices;

uint flattenClusterIndex(uint cx, uint cy, uint cz)
{
  return cx + (cy * XSLICES) + (cz * XSLICES * YSLICES);
}

float depthToNDC(float depthNorm)
{
    // In inverted Z, near=0→NDC=+1, far=1→NDC=-1
    return (1.0 - depthNorm) * 2.0 - 1.0;
}

static bool sphereIntersectsAABB(float3 sphereCenter, float radius, float3 aabbMin, float3 aabbMax)
{
    // clamp center to AABB, compute squared distance
    float3 closest = clamp(sphereCenter, aabbMin, aabbMax);
    float3 d = sphereCenter - closest;
    float dist2 = dot(d, d);
    return dist2 <= radius * radius;
}

[numthreads(WORKGROUP_SIZE, 1, 1)]
[shader("compute")]
void cmpMain(uint3 dispatchThreadID : SV_DispatchThreadID, uniform uint pushLightCount)
{
    uint globalIndex = dispatchThreadID.x;
    uint totalClusters = XSLICES * YSLICES * ZSLICES;
    if (globalIndex >= totalClusters) return;

    // compute 3D cluster coords
    uint clusterZ = globalIndex / (XSLICES * YSLICES);
    uint tmp = globalIndex % (XSLICES * YSLICES);
    uint clusterY = tmp / XSLICES;
    uint clusterX = tmp % XSLICES;

    float screenW = camera.projInfo.x;
    float screenH = camera.projInfo.y;
    float zNear = camera.projInfo.z;
    float zFar = camera.projInfo.w;
    float logRatio = log(zFar / zNear);

    float sliceW = screenW / float(XSLICES);
    float sliceH = screenH / float(YSLICES);

    // pixel rectangle for this cluster (we sample at integer pixel centers clamped)
    // choose 4 sampling points (corners) to find min/max depth for the cluster's z-slice
    float2 px0 = float2(clusterX * sliceW, clusterY * sliceH); // top-left
    float2 px1 = float2((clusterX + 1) * sliceW, (clusterY + 1) * sliceH); // bottom-right

    // clamp to valid pixel coords (subtract .5 to sample pixel center)
    // but sampling uses uv in [0,1] where uv = (x + 0.5)/width
    float2 cornersPx[4];
    cornersPx[0] = float2(px0.x, px0.y);
    cornersPx[1] = float2(px1.x, px0.y);
    cornersPx[2] = float2(px0.x, px1.y);
    cornersPx[3] = float2(px1.x, px1.y);

    // We'll sample depth at the four corners and build z-range for this cluster Z index by
    // taking min and max of sampled depth (converted from stored depth with inverted z).
    float minDepthSample = 1.0;
    float maxDepthSample = 0.0;
    for (int i = 0; i < 4; ++i) {
        // map to uv (sample at pixel corner center)
        float2 uv = (cornersPx[i] + float(0.5).xx) / float2(screenW, screenH);
        float depth = depth.SampleLevel(uv, 0.0).r;
        minDepthSample = min(minDepthSample, depth);
        maxDepthSample = max(maxDepthSample, depth);
    }

    // Now we have cluster's sampled depth interval [minDepthSample, maxDepthSample] in normalized device depth (0..1, AFTER flipping inverted)
    // But we also want to restrict to the discrete Z-slice range (clusterZ). Compute normalized slice boundaries:
    float zSliceSize = 1.0 / float(ZSLICES);
    // For Z-slices we need consistent mapping between normalized depth and z-slice index.
    // We will compute the cluster slice's normalized depth bounds:
    float zSliceMinNorm = float(clusterZ) * zSliceSize;
    float zSliceMaxNorm = float(clusterZ + 1) * zSliceSize;

    // compute depth range for this Z slice in log-space
    float sliceMinZ = exp(log(zFar / zNear) * zSliceMinNorm) * zNear;
    float sliceMaxZ = exp(log(zFar / zNear) * zSliceMaxNorm) * zNear;

    // find actual cluster depth bounds in normalized form (from depth prepass)
    float minDepthLin = exp(log(zFar / zNear) * minDepthSample) * zNear;
    float maxDepthLin = exp(log(zFar / zNear) * maxDepthSample) * zNear;

    // Intersect sampled depth interval with slice interval
    float sliceMinNorm = max(sliceMinZ, minDepthLin);
    float sliceMaxNorm = min(sliceMaxZ, maxDepthLin);
    if (sliceMaxNorm < sliceMinNorm) {
        // no pixels in this cluster (depth range empty) -> early out with zero count
        clusterCounts[globalIndex] = 0;
        return;
    }

    // compute world-space positions for the 8 points, then view-space
    float3 aabbMin = float3( 1e9,  1e9,  1e9);
    float3 aabbMax = float3(-1e9, -1e9, -1e9);

    for (int d = 0; d < 2; ++d)
    {
        float zDepth = (d == 0) ? sliceMinNorm : sliceMaxNorm;
        // convert zLinear back to normalized logZ in [0,1]
        float logNorm = log(zDepth / zNear) / log(zFar / zNear);
        float depthNorm = saturate(logNorm);

        for (int i = 0; i < 4; ++i)
        {
            float2 uv = (cornersPx[i] + float(0.5).xx) / float2(screenW, screenH);
            float ndcX = uv.x * 2.0 - 1.0;
            float ndcY = 1.0 - uv.y * 2.0;
            float ndcZ = depthToNDC(depthNorm);
            float4 world = mul(camera.invViewProj, float4(ndcX, ndcY, ndcZ, 1.0));
            world.xyz /= world.w;
            float3 viewPos = mul(camera.view, world).xyz;
            aabbMin = min(aabbMin, viewPos);
            aabbMax = max(aabbMax, viewPos);
        }
    }

    // Now test lights against this view-space AABB
    uint count = 0;
    // threshold for light influence -- when light contribution falls below this factor we stop
    const float LIGHT_INTENSITY_THRESHOLD = 0.01; // you can tune this

    for (uint li = 0; li < pushLightCount; ++li) {
        Light L = lights[li];
        // light position in view space
        float4 lp_world = float4(L.position.xyz, 1.0);
        float4 lp_view4 = mul(camera.view, lp_world);
        float3 lp_view = lp_view4.xyz;

        // compute radius (positive)
        float radius = L.computeLightRadius_LS(LIGHT_INTENSITY_THRESHOLD);
        if (radius <= 0.0) continue;

        // quick Z slab rejection (optional but cheap)
        // if the sphere is completely behind far plane of cluster AABB
        // (AABB z is in view space; note forward direction likely -z; but intersection uses coordinates as-is)
        // sphere center distance to aabb considered via full sphere-AABB test below, so we can skip quick test if uncertain

        // sphere-AABB intersection in view space
        if (sphereIntersectsAABB(lp_view, radius, aabbMin, aabbMax)) {
            if (count < MAX_LIGHTS_PER_CLUSTER) {
              clusterLightIndices[globalIndex * MAX_LIGHTS_PER_CLUSTER + count] = li;
              count++;
            }
        }
    }
    clusterCounts[globalIndex] = count;
}