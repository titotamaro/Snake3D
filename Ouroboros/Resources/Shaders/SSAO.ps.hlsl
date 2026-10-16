// Screen-space ambient occlusion: normal-oriented hemisphere sampling with a range check.
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);
Texture2D<float> g_Depth : register(t0);
Texture2D<float4> g_Normals : register(t1);
SamplerState g_PointClampSampler : register(s0);

static const uint SampleCount = 24;

// Deterministic, well distributed hemisphere kernel (spherical Fibonacci, denser near the origin).
float3 KernelSample(uint index)
{
	const float goldenAngle = 2.39996323;
	const float t = (float(index) + 0.5) / float(SampleCount);
	const float z = sqrt(1.0 - t);          // cosine weighted
	const float r = sqrt(saturate(1.0 - z * z));
	const float phi = float(index) * goldenAngle;
	float scale = (float(index) + 1.0) / float(SampleCount);
	scale = lerp(0.1, 1.0, scale * scale);
	return float3(cos(phi) * r, sin(phi) * r, z) * scale;
}

float main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	const float depth = g_Depth.SampleLevel(g_PointClampSampler, uv, 0);
	if (depth >= 1.0)
		return 1.0;

	const float3 viewPosition = ViewPositionFromDepth(uv, depth, g_Frame.InverseProjection);
	const float3 normal = normalize(g_Normals.SampleLevel(g_PointClampSampler, uv, 0).xyz);

	// Random rotation around the normal per pixel.
	const float angle = InterleavedGradientNoise(position.xy) * 2.0 * PI;
	float3 randomVector = float3(cos(angle), sin(angle), 0.0);
	const float3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
	const float3 bitangent = cross(normal, tangent);
	const float3x3 tbn = float3x3(tangent, bitangent, normal);

	const float radius = g_Frame.AOParams.x;
	const float bias = g_Frame.AOParams.z;

	float occlusion = 0.0;
	[unroll]
	for (uint i = 0; i < SampleCount; i++)
	{
		const float3 samplePosition = viewPosition + mul(KernelSample(i), tbn) * radius;

		float4 clip = mul(g_Frame.Projection, float4(samplePosition, 1.0));
		clip.xyz /= clip.w;
		const float2 sampleUV = ClipToUV(clip.xy);
		if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
			continue;

		const float sceneDepth = g_Depth.SampleLevel(g_PointClampSampler, sampleUV, 0);
		const float sceneZ = ViewPositionFromDepth(sampleUV, sceneDepth, g_Frame.InverseProjection).z;
		const float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(viewPosition.z - sceneZ), 1e-4));
		occlusion += (sceneZ >= samplePosition.z + bias ? 1.0 : 0.0) * rangeCheck;
	}

	const float ao = 1.0 - occlusion / float(SampleCount);
	return pow(saturate(ao), g_Frame.AOParams.y);
}
