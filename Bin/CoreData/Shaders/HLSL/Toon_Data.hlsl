#ifndef D3D11
    sampler2D sControlMap : register(s3);
    sampler2D sMatCap : register(s4);
    sampler2D sHairRing : register(s4);
    sampler2D sLinework : register(s5);
    sampler2D sVerticalRamp : register(s6);
    sampler2D sToneMap : register(s7);
#else
    Texture2D tControlMap : register(t3);
    Texture2D tMatCap : register(t4);
    Texture2D tHairRing : register(t4);
    Texture2D tLinework : register(t5);
    Texture2D tVerticalRamp : register(t6);
    Texture2D tToneMap : register(t7);
    
    SamplerState sControlMap : register(s3);
    SamplerState sMatCap : register(s4);
    SamplerState sHairRing : register(s4);
    SamplerState sLinework : register(s5);
    SamplerState sVerticalRamp : register(s6);
    SamplerState sToneMap : register(s7);
#endif

#if defined(COMPILEVS) || defined(COMPILEHS) || defined(COMPILEDS)
cbuffer CustomVS : register(b6)
{
    float4 cOutlinePower;
    #if defined(TOON_TESS)
        float4 cTessParams;
    #endif
    #if defined(TOON_VERTICAL_RAMP)
        float4 cVerticalRampShift;
    #endif
}
#endif

#if defined(COMPILEPS)
cbuffer CustomPS : register(b6)
{
    float4 cRimLightColor;
    float4 cLineWorkPower;
    float4 cOutlinePower;
    float4 cHairRingColor;
    float2 cExtinctionInscatter;
    float2 cSaturation;
}
#endif