
#ifdef COMPILEVS

#ifdef OPEN_XR
    cbuffer CameraConst : register(c0)
    {
        float4x4 ProjMat;
    };
#endif

void VS(float3 iPos : POSITION,
    out float4 oPos : OUTPOSITION)
{

#ifdef OPEN_XR
    oPos = ProjMat * iPos;
#else
    // convert 0 to 1 into -1 to 1
    float2 clipPos = (iPos.xy * 2.0) - 1.0;

    // 2D ortho
    oPos = float4(clipPos.x, clipPos.y, 0, 1.0);
#endif
}

#endif

#ifdef COMPILEPS

float4 PS() : OUTCOLOR0
{
    // bright yellow, you can't miss it if it leaks
    return float4(1, 1, 0, 1);
}

#endif