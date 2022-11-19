

#ifdef COMPILEPS

float4 EvaluateHairRing(float3 normal, float2 secondaryUV, float3 worldPos)
{
    return Sample2D(HairRing, float2(secondaryUV.x, secondaryUV.y)) * cHairRingColor;
}

// User: szamq
// https://discourse.urho3d.io/t/matcap-shader-and-modelview-matrix/272/5
float3 EvaluateMatCap(float2 matcapUV, float2 uv)
{
    float4 sampleValue = Sample2D(MatCap, matcapUV);
    float matcapPower = clamp(cAmbientColor.a, 0, 1);
    #ifdef TOON_CONTROL_MAP
        matcapPower *= Sample2D(ControlMap, uv).a;
    #endif
    return cMatEnvMapColor.rgb * sampleValue.rgb * matcapPower;
}

float MSDF(float r, float g, float b) 
{
	return max(min(r, g), min(max(r, g), b));
}

float3 Saturate(in float3 src, float saturation)
{
    float weightFactor = dot(src, float3(0.2125, 0.7154, 0.0721));
    return lerp(float3(weightFactor, weightFactor, weightFactor), src, saturation);
}

float SampleMSDF(float2 uvCoord, float3 normal, float3 cameraDir)
{
    float2 texSize = float2(0, 0);
    tLinework.GetDimensions(texSize.x, texSize.y);
    float2 stepSize = float2(4.0,4.0) / texSize;
    float4 samp = Sample2D(Linework, uvCoord);
    #ifdef TOON_LINE_MAP_CONTROLLED
        if (samp.a > saturate(dot(normal, cameraDir)))
            return 1.0;
    #endif
	float fldVal = (MSDF(samp.r, samp.g, samp.b) - 0.5) * dot(stepSize, 0.5 / fwidth(uvCoord));
    float weight = saturate(fldVal + 0.5);
    
    // use step instead? might it alias too much if stepSize isn't good?
    return lerp(1.0, 0.0, weight);
}

#endif