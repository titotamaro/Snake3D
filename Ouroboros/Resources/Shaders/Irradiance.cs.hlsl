// Diffuse irradiance convolution (cosine-weighted hemisphere integral of the environment).
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
	const float3 up = abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	const float3 right = normalize(cross(up, N));
	const float3 tangentUp = cross(N, right);

	// Sample a low-resolution mip so each sample integrates a solid angle (avoids fireflies from small bright sources).
	const float sourceMip = max(log2(g_Constants.SourceSize / 32.0), 0.0);

	float3 irradiance = 0.0;
	float sampleCount = 0.0;
	const float delta = 0.025;
	for (float phi = 0.0; phi < 2.0 * PI; phi += delta * 2.0)
	{
		for (float theta = 0.0; theta < 0.5 * PI; theta += delta)
		{
			const float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			const float3 sampleDirection = tangentSample.x * right + tangentSample.y * tangentUp + tangentSample.z * N;
			irradiance += g_Source.SampleLevel(g_LinearSampler, sampleDirection, sourceMip).rgb * cos(theta) * sin(theta);
			sampleCount += 1.0;
		}
	}

	g_Output[id] = float4(PI * irradiance / sampleCount, 1.0);
}
