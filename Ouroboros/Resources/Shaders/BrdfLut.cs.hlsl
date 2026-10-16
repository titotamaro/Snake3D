// Integrates the split-sum BRDF lookup table (scale, bias) over (NdotV, roughness).
#include "IBLCommon.hlsli"

[[vk::image_format("rg16f")]] RWTexture2D<float2> g_Output : register(u0);
[[vk::push_constant]] ConstantBuffer<IBLConstants> g_Constants;

float GeometrySchlickGGX(float NdotV, float roughness)
{
	const float k = (roughness * roughness) / 2.0;
	return NdotV / (NdotV * (1.0 - k) + k);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const uint size = g_Constants.OutputSize;
	if (id.x >= size || id.y >= size)
		return;

	const float NdotV = max((float(id.x) + 0.5) / float(size), 1e-3);
	const float roughness = (float(id.y) + 0.5) / float(size);

	const float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
	const float3 N = float3(0.0, 0.0, 1.0);

	float scale = 0.0;
	float bias = 0.0;
	const uint sampleCount = 512;
	for (uint i = 0; i < sampleCount; i++)
	{
		const float2 xi = Hammersley(i, sampleCount);
		const float3 H = ImportanceSampleGGX(xi, N, roughness);
		const float3 L = normalize(2.0 * dot(V, H) * H - V);

		const float NdotL = saturate(L.z);
		const float NdotH = saturate(H.z);
		const float VdotH = saturate(dot(V, H));
		if (NdotL > 0.0)
		{
			const float G = GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
			const float visibility = (G * VdotH) / max(NdotH * NdotV, 1e-5);
			const float fresnel = pow(1.0 - VdotH, 5.0);
			scale += (1.0 - fresnel) * visibility;
			bias += fresnel * visibility;
		}
	}

	g_Output[id.xy] = float2(scale, bias) / float(sampleCount);
}
