// Specular pre-filtering of the environment for the split-sum approximation (one roughness per mip).
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

	const float3 N = CubeDirection(id.z, id.xy, g_Constants.OutputSize);
	const float3 V = N;
	const float roughness = g_Constants.Roughness;

	if (roughness <= 0.0)
	{
		g_Output[id] = float4(g_Source.SampleLevel(g_LinearSampler, N, 0).rgb, 1.0);
		return;
	}

	const float sourceTexelSolidAngle = 4.0 * PI / (6.0 * g_Constants.SourceSize * g_Constants.SourceSize);

	float3 color = 0.0;
	float totalWeight = 0.0;
	for (uint i = 0; i < g_Constants.SampleCount; i++)
	{
		const float2 xi = Hammersley(i, g_Constants.SampleCount);
		const float3 H = ImportanceSampleGGX(xi, N, roughness);
		const float3 L = normalize(2.0 * dot(V, H) * H - V);
		const float NdotL = dot(N, L);
		if (NdotL <= 0.0)
			continue;

		// Sample from a mip matching the sample's solid angle (Karis, "Real Shading in Unreal Engine 4").
		const float NdotH = saturate(dot(N, H));
		const float HdotV = saturate(dot(H, V));
		const float pdf = DistributionGGX(NdotH, roughness) * NdotH / (4.0 * HdotV) + 1e-4;
		const float sampleSolidAngle = 1.0 / (float(g_Constants.SampleCount) * pdf + 1e-4);
		const float mip = max(0.5 * log2(sampleSolidAngle / sourceTexelSolidAngle) + 1.0, 0.0);

		color += g_Source.SampleLevel(g_LinearSampler, L, mip).rgb * NdotL;
		totalWeight += NdotL;
	}

	g_Output[id] = float4(color / max(totalWeight, 1e-4), 1.0);
}
