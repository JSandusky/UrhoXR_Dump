#include "Constants.hlsl"
#include "Uniforms.hlsl"
#include "Samplers.hlsl"
#include "Transform.hlsl"
#include "ScreenPos.hlsl"
#include "Fog.hlsl"

#if defined(COMPILEPS) && defined(SOFTPARTICLES)
#ifndef D3D11
// D3D9 uniform
uniform float cSoftParticleFadeScale;
#else
// D3D11 constant buffer
cbuffer CustomPS : register(b6)
{
    float cSoftParticleFadeScale;
}
#endif
#endif

void VS(float4 iPos : POSITION,
    #ifndef NOUV
        #if defined(POINTBILLBOARD) || defined(POINTDIRBILLBOARD)
            float4 iTexCoord : TEXCOORD0,
        #else
            float2 iTexCoord : TEXCOORD0,
        #endif
    #endif
    #ifdef VERTEXCOLOR
        float4 iColor : COLOR0,
    #endif
    #ifdef SKINNED
        float4 iBlendWeights : BLENDWEIGHT,
        int4 iBlendIndices : BLENDINDICES,
    #endif
    #ifdef INSTANCED
        float4x3 iModelInstance : TEXCOORD4,
    #endif
    #if defined(BILLBOARD) || defined(DIRBILLBOARD)
        float2 iSize : TEXCOORD1,
    #elif defined(POINTBILLBOARD) || defined(POINTDIRBILLBOARD)
        float4 iSize : TEXCOORD1,
    #endif
    #if defined(DIRBILLBOARD) || defined(TRAILBONE) || defined(POINTDIRBILLBOARD)
        float3 iNormal : NORMAL,
    #endif
    #if defined(TRAILFACECAM) || defined(TRAILBONE)
        float4 iTangent : TANGENT,
    #endif
    
    #if defined(POINTBILLBOARD) || defined(POINTDIRBILLBOARD)
        out float4 oTexCoord : TEXCOORD0,
        out float4 oSize : TEXCOORD1,
    #else
        out float2 oTexCoord : TEXCOORD0,
    #endif
    #if defined(POINTDIRBILLBOARD)
        out float3 oNormal : NORMAL,
    #endif
    #if defined(SOFTPARTICLES) && !defined(POINTEXPAND)
        out float4 oScreenPos : TEXCOORD1,
    #endif
    out float4 oWorldPos : TEXCOORD2,
    #ifdef VERTEXCOLOR
        out float4 oColor : COLOR0,
    #endif
    #if defined(D3D11) && defined(CLIPPLANE)
        out float oClip : SV_CLIPDISTANCE0,
    #endif
    out float4 oPos : OUTPOSITION)
{
    // Define a 0,0 UV coord if not expected from the vertex data
    #ifdef NOUV
    float2 iTexCoord = float2(0.0, 0.0);
    #endif

    #if defined(POINTBILLBOARD) || defined(POINTDIRBILLBOARD)
        float3 worldPos = iPos;
        oPos = GetClipPos(worldPos);
        oTexCoord = iTexCoord;
        oSize = iSize;
        oWorldPos = float4(worldPos, GetDepth(oPos));
        #ifdef POINTDIRBILLBOARD
            oNormal = iNormal;
        #endif
    #else
        float4x3 modelMatrix = iModelMatrix;
        float3 worldPos = GetWorldPos(modelMatrix);
        oPos = GetClipPos(worldPos);
        oTexCoord = GetTexCoord(iTexCoord);
        oWorldPos = float4(worldPos, GetDepth(oPos));
    #endif

    #if defined(D3D11) && defined(CLIPPLANE)
        oClip = dot(oPos, cClipPlane);
    #endif

    #if defined(SOFTPARTICLES) && !defined(POINTEXPAND)
        oScreenPos = GetScreenPos(oPos);
    #endif

    #ifdef VERTEXCOLOR
        oColor = iColor;
    #endif
}


#if defined(COMPILEGS) && (defined(POINTBILLBOARD) || defined(POINTDIRBILLBOARD))

struct VSOutput
{
    float4 iTexCoord : TEXCOORD0;
    float4 iSize : TEXCOORD1;
    #ifdef POINTDIRBILLBOARD
        float3 iNormal : NORMAL;
    #endif
    float4 iWorldPos: TEXCOORD2;
    #ifdef VERTEXCOLOR
        float4 iColor : COLOR0;
    #endif
};

struct GSOutput
{
    float2 iTexCoord : TEXCOORD0;
    #ifdef SOFTPARTICLES
        float4 iScreenPos: TEXCOORD1;
    #endif
    float4 iWorldPos: TEXCOORD2;
    #ifdef VERTEXCOLOR
        float4 iColor : COLOR0;
    #endif
    float4 oPos : SV_POSITION;
};

[maxvertexcount(4)]
void GS(point in VSOutput vertexData[1], inout TriangleStream<GSOutput> triStream)
{
    VSOutput vertData = vertexData[0];

    float2 minUV = vertData.iTexCoord.xy;
    float2 maxUV = vertData.iTexCoord.zw;
    float2 partSize = vertData.iSize.xy;

    float2 partUV[] = {
        float2(minUV.x, maxUV.y),
        minUV,
        maxUV,
        float2(maxUV.x, minUV.y),
    };
    
    float s;
    float c;
    sincos(vertData.iSize.z * M_DEGTORAD, s, c);
    float3 vUpNew    = c * float3(1, 0, 0) - s * float3(0, 1, 0);
    float3 vRightNew = s * float3(1, 0, 0) + c * float3(0, 1, 0);
    vUpNew *= partSize.y;
    vRightNew *= partSize.x;
    
    float3 partOffset[] = {
        -vUpNew + -vRightNew,
        -vUpNew + vRightNew,
        vUpNew + -vRightNew,
        vUpNew + vRightNew,
    };
    
    float3 worldLoc = mul(vertData.iWorldPos.xyz, iModelMatrix);
    
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        GSOutput outVert = (GSOutput)0;
        
        #ifdef POINTBILLBOARD       
            float3 vPos = mul(partOffset[i], cBillboardRot);
        #endif
        #ifdef POINTDIRBILLBOARD
            float3 vPos = mul(partOffset[i].xzy, GetFaceCameraRotation(worldLoc, vertData.iNormal));
        #endif
        
        outVert.iWorldPos = float4(worldLoc + vPos, 0);
        outVert.oPos = GetClipPos(outVert.iWorldPos);
        outVert.iTexCoord = GetTexCoord(partUV[i]);
        outVert.iColor = vertData.iColor;
        
        #ifdef SOFTPARTICLES
            outVert.iScreenPos = GetScreenPos(outVert.oPos);
        #endif
        
        triStream.Append(outVert);
    }
}

#endif 

#if COMPILEPS

void PS(float2 iTexCoord : TEXCOORD0,
    #ifdef SOFTPARTICLES
        float4 iScreenPos: TEXCOORD1,
    #endif
    float4 iWorldPos: TEXCOORD2,
    #ifdef VERTEXCOLOR
        float4 iColor : COLOR0,
    #endif
    #if defined(D3D11) && defined(CLIPPLANE)
        float iClip : SV_CLIPDISTANCE0,
    #endif
    out float4 oColor : OUTCOLOR0)
{
    // Get material diffuse albedo
    #ifdef DIFFMAP
        float4 diffColor = cMatDiffColor * Sample2D(DiffMap, iTexCoord);
        #ifdef ALPHAMASK
            if (diffColor.a < 0.5)
                discard;
        #endif
    #else
        float4 diffColor = cMatDiffColor;
    #endif

    #ifdef VERTEXCOLOR
        diffColor *= iColor;
    #endif

    // Get fog factor
    #ifdef HEIGHTFOG
        float fogFactor = GetHeightFogFactor(iWorldPos.w, iWorldPos.y);
    #else
        float fogFactor = GetFogFactor(iWorldPos.w);
    #endif
    
    // Soft particle fade
    // In expand mode depth test should be off. In that case do manual alpha discard test first to reduce fill rate
    #ifdef SOFTPARTICLES
        #if defined(EXPAND) && !defined(ADDITIVE)
            if (diffColor.a < 0.01)
                discard;
        #endif

        float particleDepth = iWorldPos.w;
        float depth = Sample2DProj(DepthBuffer, iScreenPos).r;
        #ifdef HWDEPTH
            depth = ReconstructDepth(depth);
        #endif

        #ifdef EXPAND
            float diffZ = max(particleDepth - depth, 0.0) * (cFarClipPS - cNearClipPS);
            float fade = saturate(diffZ * cSoftParticleFadeScale);
        #else
            float diffZ = (depth - particleDepth) * (cFarClipPS - cNearClipPS);
            float fade = saturate(1.0 - diffZ * cSoftParticleFadeScale);
        #endif

        #ifndef ADDITIVE
            diffColor.a = max(diffColor.a - fade, 0.0);
        #else
            diffColor.rgb = max(diffColor.rgb - fade, float3(0.0, 0.0, 0.0));
        #endif
    #endif

    oColor = float4(GetFog(diffColor.rgb, fogFactor), diffColor.a);
}

#endif
