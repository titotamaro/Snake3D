// Copies a texture to the current render target (used to present the scene image in the runtime).
Texture2D<float4> g_Source : register(t0);
SamplerState g_LinearClampSampler : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	return float4(g_Source.SampleLevel(g_LinearClampSampler, uv, 0).rgb, 1.0);
}
