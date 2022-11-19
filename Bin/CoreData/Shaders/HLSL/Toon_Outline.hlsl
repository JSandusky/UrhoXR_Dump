#include "Uniforms.hlsl"
#include "Samplers.hlsl"
#include "Transform.hlsl"

#include "Toon_Data.hlsl"

void VS(float4 iPos : POSITION,
    #ifndef NOUV
        float2 iTexCoord : TEXCOORD0,
    #endif
    float3 iNormal : NORMAL,
    #ifdef SKINNED
        float4 iBlendWeights : BLENDWEIGHT,
        int4 iBlendIndices : BLENDINDICES,
    #endif
    #ifdef INSTANCED
        float4x3 iModelInstance : TEXCOORD4,
    #endif
    out float4 oPos : OUTPOSITION)
{
    // Define a 0,0 UV coord if not expected from the vertex data
    #ifdef NOUV
    float2 iTexCoord = float2(0.0, 0.0);
    #endif

    float4x3 modelMatrix = iModelMatrix;
    float3 worldPos = GetWorldPos(modelMatrix);
    
    #ifdef TOON_CONTROL_MAP
        float outlinePower = Sample2D(sControlMap, iTexCoord.xy).r * cOutlinePower.x;
    #else
        float outlinePower = cOutlinePower.x;
    #endif
   
    oPos = GetClipPos(worldPos + iNormal * -(1+outlinePower) + normalize(cCameraPos - iPos) * cOutlinePower.y);
}

void PS(out float4 oColor : OUTCOLOR0)
{
    oColor = float4(0, 0, 0, 1);
}
