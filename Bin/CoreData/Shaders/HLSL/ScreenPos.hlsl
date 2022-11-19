#ifdef COMPILEVS

#ifdef VR
float3x3 GetCameraRot(uint iInstanceID)
#else
float3x3 GetCameraRot()
#endif
{
#ifdef VR
    int idx = iInstanceID & 1;
    return float3x3(cViewInv[idx][0][0], cViewInv[idx][0][1], cViewInv[idx][0][2],
        cViewInv[idx][1][0], cViewInv[idx][1][1], cViewInv[idx][1][2],
        cViewInv[idx][2][0], cViewInv[idx][2][1], cViewInv[idx][2][2]);
#else
    return float3x3(cViewInv[0][0], cViewInv[0][1], cViewInv[0][2],
        cViewInv[1][0], cViewInv[1][1], cViewInv[1][2],
        cViewInv[2][0], cViewInv[2][1], cViewInv[2][2]);
#endif
}

float4 GetScreenPos(float4 clipPos)
{
    return float4(
        clipPos.x * cGBufferOffsets.z + cGBufferOffsets.x * clipPos.w,
        -clipPos.y * cGBufferOffsets.w + cGBufferOffsets.y * clipPos.w,
        0.0,
        clipPos.w);
}

float2 GetScreenPosPreDiv(float4 clipPos)
{
    return float2(
        clipPos.x / clipPos.w * cGBufferOffsets.z + cGBufferOffsets.x,
        -clipPos.y / clipPos.w * cGBufferOffsets.w + cGBufferOffsets.y);
}

float2 GetQuadTexCoord(float4 clipPos)
{
    return float2(
        clipPos.x / clipPos.w * 0.5 + 0.5,
        -clipPos.y / clipPos.w * 0.5 + 0.5);
}

float2 GetQuadTexCoordNoFlip(float3 worldPos)
{
    return float2(
        worldPos.x * 0.5 + 0.5,
        -worldPos.y * 0.5 + 0.5);
}

float3 GetFarRay(float4 clipPos)
{
    float3 viewRay = float3(
        clipPos.x / clipPos.w * cFrustumSize.x,
        clipPos.y / clipPos.w * cFrustumSize.y,
        cFrustumSize.z);

#ifdef VR
    return mul(viewRay, GetCameraRot(0));
#else
    return mul(viewRay, GetCameraRot());
#endif
}

float3 GetNearRay(float4 clipPos)
{
    float3 viewRay = float3(
        clipPos.x / clipPos.w * cFrustumSize.x,
        clipPos.y / clipPos.w * cFrustumSize.y,
        0.0);

#ifdef VR
    return mul(viewRay, GetCameraRot(0)) * cDepthMode.z;
#else
    return mul(viewRay, GetCameraRot());
#endif
}
#endif
