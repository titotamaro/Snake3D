// Separable, depth-aware (bilateral) blur for the SSAO term.
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);
Texture2D<float> g_AO : register(t0);
Texture2D<float> g_Depth : register(t1);
SamplerState g_PointClampSampler : register(s0);

struct BlurConstants
{
	float2 Direction; // (1,0) or (0,1) in texels
	float2 Padding;
};
[[vk::push_constant]] ConstantBuffer<BlurConstants> g_Blur;

float LinearDepth(float2 uv)
{
	const float depth = g_Depth.SampleLevel(g_PointClampSampler, uv, 0);
	return -ViewPositionFromDepth(uv, depth, g_Frame.InverseProjection).z;
}

float main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	const float centerDepth = LinearDepth(uv);
	const float2 texel = g_Blur.Direction * g_Frame.ViewportSize.zw;

	static const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

	float sum = g_AO.SampleLevel(g_PointClampSampler, uv, 0) * weights[0];
	float totalWeight = weights[0];
	[unroll]
	for (int i = 1; i < 5; i++)
	{
		[unroll]
		for (int side = -1; side <= 1; side += 2)
		{
			const float2 sampleUV = uv + texel * float(i * side);
			const float sampleDepth = LinearDepth(sampleUV);
			const float depthWeight = exp(-abs(sampleDepth - centerDepth) * 8.0 / max(centerDepth * 0.05, 0.01));
			const float weight = weights[i] * depthWeight;
			sum += g_AO.SampleLevel(g_PointClampSampler, sampleUV, 0) * weight;
			totalWeight += weight;
		}
	}
	return sum / max(totalWeight, 1e-4);
}
