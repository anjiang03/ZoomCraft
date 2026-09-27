// Reference copy of the shader embedded in src/Renderer.h (kQuadShader).
// Kept here for editing/syntax-checking convenience; the app compiles the
// embedded string at runtime, so this file is NOT required at build or run time.

cbuffer SceneCB : register(b0)
{
    float2 g_CenterUV;
    float  g_Scale;
    float  g_Sharpness;
    float2 g_StepUV;
    float  g_Sharpen;
    float  g_Pad;
};

Texture2D    g_Source  : register(t0);
SamplerState g_Sampler : register(s0);

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(uint vid : SV_VertexID)
{
    float2 p = float2((vid << 1) & 2, vid & 2);
    VSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(p.x, 1.0 - p.y);
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float2 uv = (i.uv - 0.5) / g_Scale + g_CenterUV;
    float3 color = g_Source.Sample(g_Sampler, uv).rgb;

    if (g_Sharpen > 0.5)
    {
        float3 s0 = g_Source.Sample(g_Sampler, uv + float2(0.0, -g_StepUV.y)).rgb;
        float3 s1 = g_Source.Sample(g_Sampler, uv + float2(-g_StepUV.x, 0.0)).rgb;
        float3 s2 = g_Source.Sample(g_Sampler, uv + float2( g_StepUV.x, 0.0)).rgb;
        float3 s3 = g_Source.Sample(g_Sampler, uv + float2(0.0,  g_StepUV.y)).rgb;

        float3 mn = min(color, min(min(s0, s1), min(s2, s3)));
        float3 mx = max(color, max(max(s0, s1), max(s2, s3)));
        float3 amp = saturate(min(mn, 2.0 - mx) / max(mx, 1e-5));
        float3 w = -g_Sharpness * sqrt(amp);
        color = saturate((s0 * w + s1 * w + color + s2 * w + s3 * w) / (1.0 + 4.0 * w));
    }
    return float4(color, 1.0);
}
