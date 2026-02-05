// Downsample and linearize depth buffer

struct UBO
{
    float4x4 invProj;
    float4 projInfo; // viewport width, viewport height, zNear, zFar
    int4 info; // downscale, ...
};

[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;
[[vk::binding(1, 0)]] Texture2D<float> depthTex;
[[vk::binding(2, 0)]] RWTexture2D<float2> depthMomentsTex;

float3 Unproject(in float2 screenCoords, float nolinearDepth)
{
    float4 projectedPos;
    projectedPos.x = screenCoords.x * 2.0f - 1.0f;
    projectedPos.y = (1.0f - screenCoords.y) * 2.0f - 1.0f;
    projectedPos.z = nolinearDepth;
    projectedPos.w = 1.0f;

    float4 worldPos = mul(projectedPos, ubo.invProj);
    worldPos /= worldPos.w;
    return worldPos.xyz;
}

float2 GetPixelTexSize(int mipLevel, int downscale)
{
    int mipMult = pow(2, downscale);
    return float2(1.0f, 1.0f)
        / float2(ubo.projInfo.xy / (mipMult * downscale));
}

float2 GetPixelScreenSize(int mipLevel, int downscale)
{
    return GetPixelTexSize(mipLevel, downscale) / float2(1.0f, 1.0f);
}

float2 PixelToTexCoord(float2 pixelCoord, float2 pixelTexSize)
{
    return (pixelCoord + 0.5f) * pixelTexSize;
}

float2 PixelToScreenCoord(float2 pixelCoord, float2 pixelScreenSize)
{
    return (pixelCoord + 0.5f) * pixelScreenSize;
}

[numthreads(8, 8, 1)]
[shader("compute")]
void cmpMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint2 dstCoord = dispatchThreadID.xy;
    if (dstCoord.x >= ubo.projInfo.x || dstCoord.y >= ubo.projInfo.y) return;

    int downscale = ubo.info.x;
    float2 zeroPixelTexScale = GetPixelTexSize(0, 1);
    float2 zeroPixelScreenSize = GetPixelScreenSize(0, 1);
    int2 baseZeroPixel = dstCoord * downscale;

    float2 avgMoments = float2(0.0f, 0.0f);
    float sumWeight = 0.0f;
    [unroll]
    for (int offset = 0; offset < downscale * downscale; offset++)
    {
        int x = offset % downscale;
        int y = offset / downscale;
        int2 offsetZeroPixel = baseZeroPixel + int2(x, y);
        float2 zeroScreenCoord = PixelToScreenCoord(offsetZeroPixel, zeroPixelScreenSize);
        float2 zeroTexCoord = PixelToTexCoord(offsetZeroPixel, zeroPixelTexScale);
        float depth = depthTex.Load(float3(zeroTexCoord, 0.0f));
        float3 viewCoord = Unproject(zeroScreenCoord, depth);
        float dist = length(viewCoord);
        avgMoments.x += dist;
        avgMoments.y += dist * dist;
        sumWeight += 1.0f;
    }
    avgMoments /= sumWeight;
    depthMomentsTex[dstCoord] = avgMoments;
}
