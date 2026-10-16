// Dear ImGui pixel shader.
Texture2D<float4> g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target0
{
	return color * g_Texture.Sample(g_Sampler, uv);
}
