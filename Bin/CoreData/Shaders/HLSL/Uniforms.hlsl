#ifndef D3D11

// D3D9 uniforms (no constant buffers)

#ifdef COMPILEVS

// Vertex shader uniforms
uniform float3 cAmbientStartColor;
uniform float3 cAmbientEndColor;
#ifdef BILLBOARD
uniform float3x3 cBillboardRot;
#endif
uniform float3 cCameraPos;
uniform float cNearClip;
uniform float cFarClip;
uniform float4 cDepthMode;
uniform float cDeltaTime;
uniform float cElapsedTime;
uniform float3 cFrustumSize;
uniform float4 cGBufferOffsets;
uniform float4 cLightPos;
uniform float3 cLightDir;
uniform float4 cNormalOffsetScale;
uniform float4x3 cModel;
uniform float4x3 cView;
uniform float4x3 cViewInv;
uniform float4x4 cViewProj;
uniform float4 cUOffset;
uniform float4 cVOffset;
uniform float4x3 cZone;
#ifdef SKINNED
    uniform float4x3 cSkinMatrices[MAXBONES];
#endif
#ifdef NUMVERTEXLIGHTS
    uniform float4 cVertexLights[4*3];
#else
    uniform float4x4 cLightMatrices[4];
#endif
#endif

#ifdef COMPILEPS

// Pixel shader uniforms
uniform float4 cAmbientColor;
uniform float3 cCameraPosPS;
uniform float cDeltaTimePS;
uniform float4 cDepthReconstruct;
uniform float cElapsedTimePS;
uniform float4 cFogParams;
uniform float3 cFogColor;
uniform float2 cGBufferInvSize;
uniform float4 cLightColor;
uniform float4 cLightPosPS;
uniform float3 cLightDirPS;
uniform float4 cNormalOffsetScalePS;
uniform float4 cMatDiffColor;
uniform float3 cMatEmissiveColor;
uniform float3 cMatEnvMapColor;
uniform float4 cMatSpecColor;
#ifdef PBR
    uniform float cRoughness;
    uniform float cMetallic;
    uniform float cLightRad;
    uniform float cLightLength;
#endif
uniform float3 cZoneMin;
uniform float3 cZoneMax;
uniform float cNearClipPS;
uniform float cFarClipPS;
uniform float4 cShadowCubeAdjust;
uniform float4 cShadowDepthFade;
uniform float2 cShadowIntensity;
uniform float2 cShadowMapInvSize;
uniform float4 cShadowSplits;
uniform float4x4 cLightMatricesPS[4];
#ifdef VSM_SHADOW
uniform float2 cVSMShadowParams;
#endif
#endif

#else

// D3D11 uniforms (using constant buffers)

#if defined(COMPILEVS) || defined(COMPILEGS) || defined(COMPILEHS) || defined(COMPILEDS)


// Vertex shader uniforms
cbuffer FrameVS : register(b0)
{
    float cDeltaTime;
    float cElapsedTime;
}

cbuffer CameraVS : register(b1)
{
    #ifdef VR
        float3 cCameraPos[3];
    #else
        float3 cCameraPos;
    #endif
    
    float cNearClip;
    float cFarClip;
    float4 cDepthMode;
    float3 cFrustumSize;
    float4 cGBufferOffsets;
    
    #ifdef VR
        float4x3 cView[3]; // 0 is left, 1 is right, 2 is combined
        float4x3 cViewInv[3];
        float4x4 cViewProj[3];
    #else
        float4x3 cView;
        float4x3 cViewInv;
        float4x4 cViewProj;
    #endif
    
    float4 cClipPlane;
}

cbuffer ZoneVS : register(b2)
{
    float3 cAmbientStartColor;
    float3 cAmbientEndColor;
    float4x3 cZone;
}

cbuffer LightVS : register(b3)
{
    float4 cLightPos;
    float3 cLightDir;
    float4 cNormalOffsetScale;
#ifdef NUMVERTEXLIGHTS
    float4 cVertexLights[4 * 3];
#else
    float4x4 cLightMatrices[4];
#endif
}

#ifndef CUSTOM_MATERIAL_CBUFFER
cbuffer MaterialVS : register(b4)
{
    float4 cUOffset;
    float4 cVOffset;
}
#endif

cbuffer ObjectVS : register(b5)
{
    float4x3 cModel;
#if defined(BILLBOARD) || defined(POINTBILLBOARD)
    float3x3 cBillboardRot;
#endif
#ifdef SKINNED
    uniform float4x3 cSkinMatrices[MAXBONES];
#endif
}
#endif

#ifdef COMPILEPS

// Pixel shader uniforms
cbuffer FramePS : register(b0)
{
    float cDeltaTimePS;
    float cElapsedTimePS;
}

cbuffer CameraPS : register(b1)
{
    #ifdef VR
        float3 cCameraPosPS[3];
    #else
        float3 cCameraPosPS;
    #endif        
    float4 cDepthReconstruct;
    float2 cGBufferInvSize;
    float cNearClipPS;
    float cFarClipPS;
}

cbuffer ZonePS : register(b2)
{
    float4 cAmbientColor;
    float4 cFogParams;
    float3 cFogColor;
    float3 cZoneMin;
    float3 cZoneMax;
}

cbuffer LightPS : register(b3)
{
    float4 cLightColor;
    float4 cLightPosPS;
    float3 cLightDirPS;
    float4 cNormalOffsetScalePS;
    float4 cShadowCubeAdjust;
    float4 cShadowDepthFade;
    float2 cShadowIntensity;
    float2 cShadowMapInvSize;
    float4 cShadowSplits;
    float2 cVSMShadowParams;
    float4x4 cLightMatricesPS[4];
    #ifdef PBR
        float cLightRad;
        float cLightLength;
    #endif
}

#ifndef CUSTOM_MATERIAL_CBUFFER
cbuffer MaterialPS : register(b4)
{
    float4 cMatDiffColor;
    float3 cMatEmissiveColor;
    float3 cMatEnvMapColor;
    float4 cMatSpecColor;
    #ifdef PBR
        float cRoughness;
        float cMetallic;
    #endif
}
#endif

#endif

#endif
