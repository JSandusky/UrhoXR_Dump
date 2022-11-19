//==========================================================
//  Toon Shader
//==========================================================
//  Features:
//      - artist supplied *tone* ramp
//      - Control map to force shadow/highlight
//      - model-space vertical 1px shade control (hat shadows, etc)
//      - MSDF *linework* map
//      - rimlight glow
//      - geometry-shader outlines
//          - flaky about normals, hates most *stock* models
//      - matcap/spherical environment maps
//      - hair *halo* rings
//
//==========================================================
//  Configuration
//==========================================================
//  #define TOON_HAIR_RING || TOON_HAIR_RING_UV2
//      includes a mat-cap like hair ring, requires TEXCOORD1 for the vertical coordinate, U is ignored
//      HAIR_RING and MATCAP are mutually exclusive
//  #define TOON_MATCAP
//      includes a mat-cap for metals
//      MATCAP and HAIR_RING are mutually exclusive
//  #define TOON_RIMLIGHT
//      includes rim-lighting features
//  #define TOON_VERTICAL_RAMP
//      includes a vertically sampled 1d texture for controlled dimming, like an AO map
//      model-space Y coordinate is used to sample
//  #define TOON_CONTROL_MAP
//      has a control map
//  #define TOON_RIMLIGHT
//  #define TOON_LINE_MAP || TOON_LINE_MAP_CONTROLLED
//      MSDF linework map
//  #define TOON_GS_OUTLINE
//      Use geometry shader to emit reversed faces colored black for lines
//  #define TOON_TESS
//      Use PN tessellation

//  #define NORMALMAP is supported
//      BEWARE of noisy normals
//  #define SPECMAP is supported
//      However, it is used *straight* and not stepped like diffuse
//  #define ALPHAMASK is supported
//  #define EMISSIVEMAP is supported, but means a ControlMap cannot be used

//  Textures
//      Required: tone map
//          remaps the acquired lighting values, responsible for the overal shade tone
//      Optional: control map
//          R channel: min lighting sample, use to force highlights
//          G channel: max lighting sample, use to ban highlights
//          B channel: outline power, scales the outline thickness
//          A channel: matcap mask
//      Optional: rimlight map
//          RGB color
//          A channel: rim-light mask
//      Optional: linework map
//          RGB channels: MSDF
//          optional A channel: minimum dot-product to gate lines
//              only used with TOON_LINE_MAP_CONTROLLED
//      Optional: vertical ramp
//  New Uniforms
//      cHairRingTransform: XY = offset, ZW = scale
//      cVerticalRampShift: X = lower, Y = upper
//      cRimLightColor: RGB = color, A = power
//      cLineWorkPower: XY = msdf scales, Z = multiplier
//      cOutlinePower: X = scale along normal, Y = offset along camera vector
//  Reused Uniforms
//      cMatSpecColor: as-is
//      cMatEnvMapColor: multiplied with matcap map

#include "Uniforms.hlsl"
#include "Constants.hlsl"
#include "Samplers.hlsl"
#include "Transform.hlsl"
#include "Lighting.hlsl"
#include "Fog.hlsl"

#include "Toon_Data.hlsl"

#include "Toon_Util.hlsl"

struct VSOutput
{
    #ifndef NORMALMAP
        float2 oTexCoord : TEXCOORD0;
    #else
        float4 oTexCoord : TEXCOORD0;
        float4 oTangent : TEXCOORD3;
    #endif
    float4 oPosition : OUTPOSITION;
    float3 oNormal : TEXCOORD1;
    float4 oWorldPos : TEXCOORD2;
    #if defined(TOON_GS_OUTLINE)
        float4 oColor : COLOR0;
    #endif
    
    #ifdef PERPIXEL
        #ifdef SHADOW
            float4 oShadowPos[NUMCASCADES] : TEXCOORD4;
        #endif
        #ifdef SPOTLIGHT
            float4 oSpotPos : TEXCOORD5;
        #endif
        #ifdef POINTLIGHT
            float3 oCubeMaskVec : TEXCOORD5;
        #endif
    #else
        float3 oVertexLight : COLOR1;
    #endif
    
    #if defined(D3D11) && defined(CLIPPLANE)
        float oClip : SV_CLIPDISTANCE0;
    #endif
    
    #if defined(TOON_VERTICAL_RAMP)
        float oRampCoord : TEXCOORD6;
    #endif
    
    #if defined(TOON_HAIR_RING) || defined(TOON_HAIR_RING_UV2)
        float2 oHairUV : TEXCOORD7;
    #endif
    #if defined(TOON_MATCAP)
        float2 oMatCapUV : TEXCOORD7;
    #endif
    
    #if defined(TOON_GS_OUTLINE)
        float4 oOutlineOffset : TEXCOORD8;
    #endif
};

#if defined(COMPILEVS)

VSOutput VS(
    float4 iPos : POSITION,
    float3 iNormal : NORMAL,
    float4 iTangent : TANGENT,
    float2 iTexCoord : TEXCOORD0
    #ifdef SKINNED
        , float4 iBlendWeights : BLENDWEIGHT
        , int4 iBlendIndices : BLENDINDICES
    #endif
    #ifdef INSTANCED
        , float4x3 iModelInstance : TEXCOORD4
    #endif
    #if defined(TOON_HAIR_RING_UV2)
        , float2 iHairUV : TEXCOORD1
    #endif
)
{
    VSOutput ret = (VSOutput)0;
    
    float4x3 modelMatrix = iModelMatrix;
    #if defined(TOON_VERTICAL_RAMP)
        ret.oRampCoord = (iPos.y - cVerticalRampShift.x) / (cVerticalRampShift.y - cVerticalRampShift.x) + cVerticalRampShift.z;
    #endif
    
    float3 worldPos = GetWorldPos(modelMatrix);
    ret.oPosition = GetClipPos(worldPos);
    ret.oNormal = GetWorldNormal(modelMatrix);
    ret.oWorldPos = float4(worldPos, GetDepth(ret.oPosition));
    
    #if defined(D3D11) && defined(CLIPPLANE)
        ret.oClip = dot(ret.oPosition, cClipPlane);
    #endif
    
    #if defined(TOON_GS_OUTLINE)
        // white is default
        ret.oColor = float4(1,1,1,1);
    #endif
    
    #if defined(TOON_HAIR_RING) || defined(TOON_HAIR_RING_UV2)
        #if defined(TOON_HAIR_RING_UV2)
            ret.oHairUV = float2(((mul((float3x3)cViewInv, ret.oNormal) + 1) * 0.5).x, iHairUV.y);
        #else
            ret.oHairUV = float2(((mul((float3x3)cViewInv, ret.oNormal) + 1) * 0.5).x, iTexCoord.y);
        #endif
    #endif
    
    #ifdef TOON_MATCAP
        ret.oMatCapUV = ((mul((float3x3)cViewInv, ret.oNormal) + 1) * 0.5).xy;
    #endif
    
    #ifdef NORMALMAP
        float4 tangent = GetWorldTangent(modelMatrix);
        float3 bitangent = cross(tangent.xyz, ret.oNormal) * tangent.w;
        ret.oTexCoord = float4(GetTexCoord(iTexCoord), bitangent.xy);
        ret.oTangent = float4(tangent.xyz, bitangent.z);
    #else
        ret.oTexCoord = GetTexCoord(iTexCoord);
    #endif
    
    #ifdef TOON_GS_OUTLINE
        // outline offset is computed here so that the GS can almost pass-through instead of repeating clip-space transformation
        #ifdef TOON_CONTROL_MAP
            float outlinePower = Sample2D(ControlMap, ret.oTexCoord.xy).r * cOutlinePower.y;
        #else
            float outlinePower = cOutlinePower.x;
        #endif
        ret.oOutlineOffset = GetClipPos(worldPos + ret.oNormal * -(1+outlinePower) + normalize(cCameraPos - iPos) * cOutlinePower.y);
    #endif
    
    #ifdef PERPIXEL
        // Per-pixel forward lighting
        float4 projWorldPos = float4(worldPos.xyz, 1.0);

        #ifdef SHADOW
            // Shadow projection: transform from world space to shadow space
            GetShadowPos(projWorldPos, ret.oNormal, ret.oShadowPos);
        #endif

        #ifdef SPOTLIGHT
            // Spotlight projection: transform from world space to projector texture coordinates
            ret.oSpotPos = mul(projWorldPos, cLightMatrices[0]);
        #endif

        #ifdef POINTLIGHT
            ret.oCubeMaskVec = mul(worldPos - cLightPos.xyz, (float3x3)cLightMatrices[0]);
        #endif
    #else
        ret.oVertexLight = GetAmbient(GetZonePos(worldPos));
    #endif
    
    return ret;
}

#endif

#if defined(COMPILEGS)

[maxvertexcount(6)]
void GS(triangle in VSOutput vertices[3], inout TriangleStream<VSOutput> triStream)
{
    VSOutput v1 = vertices[0],
        v2 = vertices[1],
        v3 = vertices[2];
        
    triStream.Append(vertices[0]);
    triStream.Append(vertices[1]);
    triStream.Append(vertices[2]);
    
    // emit the flipped vertices
    triStream.RestartStrip();
    
    // color all vertices black
    v1.oColor = v2.oColor = v3.oColor = float4(0,0,0,1);
    v1.oPosition = v1.oOutlineOffset;
    v2.oPosition = v2.oOutlineOffset;
    v3.oPosition = v3.oOutlineOffset;
    triStream.Append(v3);
    triStream.Append(v1);
    triStream.Append(v2);
}

#endif

#if defined(COMPILEPS)

// Fresnel-like, just with a fixed specular of 0,0,0
float3 GetRimlight(in float VdotH)
{
    return pow(1.0 - VdotH, 5.0);
}

void PS(in VSOutput vtxIn,
    out float4 oColor : OUTCOLOR0)
{
    float4 diffInput = Sample2D(DiffMap, vtxIn.oTexCoord.xy);
    #ifdef ALPHAMASK
        if (diffInput.a < 0.5)
            discard;
    #endif
    float4 diffColor = cMatDiffColor * diffInput;
    
    // Get material specular albedo
    #ifdef SPECMAP
        float3 specColor = cMatSpecColor.rgb * Sample2D(SpecMap, iTexCoord.xy).rgb;
    #else
        float3 specColor = cMatSpecColor.rgb;
    #endif
    
    // Get normal
    #ifdef NORMALMAP
        float3x3 tbn = float3x3(vtxIn.oTangent.xyz, float3(vtxIn.oTexCoord.zw, vtxIn.oTangent.w), vtxIn.oNormal);
        float3 normal = normalize(mul(DecodeNormal(Sample2D(NormalMap, vtxIn.oTexCoord.xy)), tbn));
    #else
        float3 normal = normalize(vtxIn.oNormal);
    #endif
    
    // Get fog factor
    #ifdef HEIGHTFOG
        float fogFactor = GetHeightFogFactor(vtxIn.oWorldPos.w, vtxIn.oWorldPos.y);
    #else
        float fogFactor = GetFogFactor(vtxIn.oWorldPos.w);
    #endif
    
    float3 cameraDir = cCameraPosPS - vtxIn.oWorldPos.xyz;
    #if defined(PERPIXEL)
        // Per-pixel forward lighting
        float3 lightDir;
        float3 lightColor;
        float3 finalColor;

        float diff = saturate(GetDiffuse(normal, vtxIn.oWorldPos, lightDir));

        #ifdef SHADOW
            diff *= GetShadow(vtxIn.oShadowPos, vtxIn.oWorldPos.w);
        #endif
        
        diff = Sample2D(ToneMap, float2(saturate(diff), 0)).r;
        #ifdef TOON_CONTROL_MAP
            float4 controlValues = Sample2D(ControlMap, vtxIn.oTexCoord);
            diff = max(controlValues.r, min(controlValues.g, diff));
        #endif
        #ifdef TOON_VERTICAL_RAMP
            diff = min(diff, Sample2D(VerticalRamp, float2(vtxIn.oRampCoord, 0)));
        #endif

        #if defined(SPOTLIGHT)
            lightColor = vtxIn.oSpotPos.w > 0.0 ? Sample2DProj(LightSpotMap, vtxIn.oSpotPos).rgb * cLightColor.rgb : 0.0;
        #elif defined(CUBEMASK)
            lightColor = SampleCube(LightCubeMap, vtxIn.oCubeMaskVec).rgb * cLightColor.rgb;
        #else
            lightColor = cLightColor.rgb;
        #endif
    
        #ifdef SPECULAR
            float spec = GetSpecular(normal, cameraDir, lightDir, cMatSpecColor.a);
            finalColor = diff * lightColor * (diffColor.rgb + spec * specColor * cLightColor.a) * 2;
        #else
            finalColor = diff * lightColor * diffColor.rgb;
        #endif

        #ifdef AMBIENT
            finalColor += cAmbientColor.rgb * diffColor.rgb;
            finalColor += cMatEmissiveColor;
            
            oColor = float4(GetFog(finalColor, fogFactor), diffColor.a);
        #else
            oColor = float4(GetLitFog(finalColor, fogFactor), diffColor.a);
        #endif
    #else
        // Ambient & per-vertex lighting
        float3 finalColor = vtxIn.oVertexLight * diffColor.rgb;
        #ifdef EMISSIVEMAP
            finalColor += cMatEmissiveColor * Sample2D(EmissiveMap, vtxIn.oTexCoord.xy).rgb;
        #else
            finalColor += cMatEmissiveColor;
        #endif
        
        #ifdef TOON_MATCAP
            finalColor.rgb += EvaluateMatCap(vtxIn.oMatCapUV, vtxIn.oTexCoord);
        #endif
        
        #ifdef TOON_RIMLIGHT
            const float3 toCamera = normalize(cCameraPosPS - vtxIn.oWorldPos.xyz);
            const float3 Hn = normalize(toCamera - vtxIn.oNormal);
            const float vdh = clamp((dot(toCamera, Hn)), 0.0001, 1.0);
            finalColor.rgb += GetRimlight(dot(toCamera, vtxIn.oNormal)) * cRimLightColor.rgb;
        #endif

        oColor = float4(GetFog(finalColor, fogFactor), diffColor.a);
    #endif
    
    // linework always darkens
    #if defined(TOON_LINE_MAP) || defined(TOON_LINE_MAP_CONTROLLED)
        oColor.rgb *= SampleMSDF(vtxIn.oTexCoord, vtxIn.oNormal, normalize(cameraDir));
    #endif
    
    #if defined(TOON_HAIR_RING) || defined(TOON_HAIR_RING_UV2)
        float4 hairRing = EvaluateHairRing(vtxIn.oNormal, vtxIn.oHairUV, vtxIn.oWorldPos);
        oColor.rgb = lerp(oColor.rgb, hairRing.rgb, hairRing.a);
    #endif
    
    #ifdef TOON_GS_OUTLINE
        oColor *= vtxIn.oColor;
    #endif
}

#endif