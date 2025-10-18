import lighting;

#define XSLICES 16
#define YSLICES 9
#define ZSLICES 24
#define MAX_LIGHTS_PER_CLUSTER 64
#define WORKGROUP_SIZE 64

struct CameraData {
  float4x4 view;
  float4x4 viewProj;
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

uint clamp_u(uint v, uint hi) { return (v >= hi) ? (hi - 1u) : v; }

float depthAtPixel(uint2 pixel)
{
    // Convert pixel coordinates to [0,1] UVs
    float2 uv = (float2(pixel) + 0.5) / float2(camera.projInfo.x, camera.projInfo.y);
    return depth.SampleLevel(uv, 0.0).r;
}

[numthreads(WORKGROUP_SIZE, 1, 1)]
[shader("compute")]
void cmpMain(uint3 dispatchThreadID : SV_DispatchThreadID, uniform uint pushLightCount)
{
    uint lightIndex = dispatchThreadID.x;
    if (lightIndex >= pushLightCount) return;

    uint xSlices = XSLICES;
    uint ySlices = YSLICES;
    uint zSlices = ZSLICES;

    Light L = lights[lightIndex];
    float4 positionVS4 = mul(camera.view, float4(L.position.xyz, 1.0)); // Calc view-space coords of L
    float3 positionVS = positionVS4.xyz;
    float zCenter = -positionVS.z;
    float radius = L.computeLightRadius(0.01);

    // 1) Compute z slice overlap (linear slicing)
    // Convert to normalized inverted-Z depth [1..0]
    float screenWidth = camera.projInfo.x;
    float screenHeight = camera.projInfo.y;
    float zNear = camera.projInfo.z;
    float zFar = camera.projInfo.w;
    float zMin = max(zCenter - radius, zNear);
    float zMax = min(zCenter + radius, zFar);
    // normalized depth
    float nzMin = (zMin - zFar) / (zNear - zFar);
    float nzMax = (zMax - zFar) / (zNear - zFar);
    // slice indices
    uint cz0 = clamp_u(uint(floor(nzMin * zSlices)), zSlices);
    uint cz1 = clamp_u(uint(floor(nzMax * zSlices)), zSlices);

    if (cz0 > cz1)
    {
      uint tmp = cz0;
      cz0 = cz1;
      cz1 = tmp;
    }

    // 2) Project sphere to screen-space to get x/y ranges
    // We need to know how many pixels it spans; easiest is to project center plus radius in view-space using projection
    // We'll reconstruct clip-space by multiply viewProj * float4(posVS,1).
    float4 clipCenter = mul(camera.viewProj, float4(L.position.xyz, 1.0));
    float invW = 1.0 / clipCenter.w;
    float2 ndcCenter = clipCenter.xy * invW; // NDC in [-1,1]

    // Project offset along view-space +X by radius
    float4 clipOffset = mul(camera.viewProj, float4(L.position.xyz + float3(radius, 0, 0), 1.0));
    float invW2 = 1.0 / clipOffset.w;
    float2 ndcEdge = clipOffset.xy * invW2;

    float2 ndcDelta = abs(ndcEdge - ndcCenter);
    float ndcRadius = max(ndcDelta.x, ndcDelta.y);

    // --- Convert to pixel space
    float2 pixelCenter = (ndcCenter * 0.5 + 0.5) * float2(camera.projInfo.x, camera.projInfo.y);
    float pixelRadius  = ndcRadius * 0.5 * max(screenWidth, screenHeight);

    float sliceW = screenWidth / float(xSlices);
    float sliceH = screenHeight / float(ySlices);

    float2 pmin = pixelCenter - float2(pixelRadius, pixelRadius);
    float2 pmax = pixelCenter + float2(pixelRadius, pixelRadius);

    int cx0i = int(floor(pmin.x / sliceW));
    int cx1i = int(floor(pmax.x / sliceW));
    int cy0i = int(floor(pmin.y / sliceH));
    int cy1i = int(floor(pmax.y / sliceH));

    uint cx0 = clamp_u((uint)max(cx0i, 0), xSlices);
    uint cx1 = clamp_u((uint)max(cx1i, 0), xSlices);
    uint cy0 = clamp_u((uint)max(cy0i, 0), ySlices);
    uint cy1 = clamp_u((uint)max(cy1i, 0), ySlices);

    for (uint cz = cz0; cz <= cz1; ++cz)
    {
        for (uint cy = cy0; cy <= cy1; ++cy)
        {
            for (uint cx = cx0; cx <= cx1; ++cx)
            {
                // Compute representative pixel of the cluster
                uint2 pixel = uint2(cx * sliceW + sliceW / 2, cy * sliceH + sliceH / 2);
                float clusterDepth = depthAtPixel(pixel);

                // Convert depth to view-space z (inverted z)
                float zView = zNear * zFar / (zFar - clusterDepth * (zFar - zNear));

                if (zCenter + radius < zView)
                  continue; // light is behind geometry

                uint clusterIdx = flattenClusterIndex(cx, cy, cz);

                uint prevCount = 0;
                InterlockedAdd(clusterCounts[clusterIdx], 1, prevCount);

                if (prevCount < MAX_LIGHTS_PER_CLUSTER)
                {
                    uint globalIndex = clusterIdx * MAX_LIGHTS_PER_CLUSTER + prevCount;
                    clusterLightIndices[globalIndex] = lightIndex;
                }
            }
        }
    }
}