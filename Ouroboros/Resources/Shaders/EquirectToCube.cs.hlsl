// Converts an equirectangular HDR panorama into a cube map (mip 0).
#include "IBLCommon.hlsli"

Texture2D<float4> g_Equirect : register(t0);
SamplerState g_LinearSampler : register(s0);
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> g_Output : register(u0);
[[vk::push_constant]] ConstantBuffer<IBLConstants> g_Constants;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Constants.OutputSize || id.y >= g_Constants.OutputSize)
		return;

	const float3 direction = CubeDirection(id.z, id.xy, g_Constants.OutputSize);
	const float u = atan2(direction.z, direction.x) / (2.0 * PI) + 0.5;
	const float v = acos(clamp(direction.y, -1.0, 1.0)) / PI;
	float3 color = g_Equirect.SampleLevel(g_LinearSampler, float2(u, v), 0).rgb;
	color = min(color, 64000.0); // stay within half-float range
	g_Output[id] = float4(color, 1.0);
}
