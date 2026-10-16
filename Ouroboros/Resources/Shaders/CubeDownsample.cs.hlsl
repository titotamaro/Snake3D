// Builds the next mip of a cube map by sampling the previous one along each texel's direction.
// g_Source is a view of only the previous mip, so it is sampled at level 0.
#include "IBLCommon.hlsli"

TextureCube<float4> g_Source : register(t0);
SamplerState g_LinearSampler : register(s0);
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> g_Output : register(u0);
[[vk::push_constant]] ConstantBuffer<IBLConstants> g_Constants;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Constants.OutputSize || id.y >= g_Constants.OutputSize)
		return;

	const float3 direction = CubeDirection(id.z, id.xy, g_Constants.OutputSize);
	g_Output[id] = float4(g_Source.SampleLevel(g_LinearSampler, direction, 0).rgb, 1.0);
}
