[[vk::binding(0, 0)]] Texture2D<float> srcTex;
[[vk::binding(1, 0)]] RWTexture2D<float> dstTex;

[numthreads(8, 8, 1)]
[shader("compute")]
void cmpMain(uint3 dispatchThreadID : SV_DispatchThreadID,
  uniform int4 srcDstWH) // x: srcW, y: srcH, z: dstW, w: dstH
{
    uint2 dstCoord = dispatchThreadID.xy;
    if (dstCoord.x >= srcDstWH.z || dstCoord.y >= srcDstWH.w) return;

    // If src == dst size a same, it's src depth buff -> just copy
    if (srcDstWH.x == srcDstWH.z && srcDstWH.y == srcDstWH.w) {
        if (dstCoord.x < srcDstWH.z && dstCoord.y < srcDstWH.w)
            dstTex[dstCoord] = srcTex.Load(int3(dstCoord, 0));
        return;
    }

    // compute corresponding 2x2 source coords
    uint2 srcBase = dstCoord * 2;
    uint2 srcSize = uint2(srcDstWH.x, srcDstWH.y);

    float v0 = srcTex.Load(int3(srcBase + uint2(0,0), 0));
    float v1 = srcTex.Load(int3(min(srcBase + uint2(1,0), srcSize - 1), 0));
    float v2 = srcTex.Load(int3(min(srcBase + uint2(0,1), srcSize - 1), 0));
    float v3 = srcTex.Load(int3(min(srcBase + uint2(1,1), srcSize - 1), 0));

    float outV = max(max(v0, v1), max(v2, v3));
    dstTex[dstCoord] = outV;
}