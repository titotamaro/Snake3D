// Background: the environment cube map, or a procedural gradient when the scene has no sky light.
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);
TextureCube<float4> g_Environment : register(t0);
SamplerState g_LinearClampSampler : register(s0);

struct SkyOutput
{
	float4 Color : SV_Target0;
	float Depth : SV_Depth;
};

SkyOutput main(float4 position : SV_Position, float2 uv : TEXCOORD0)
{
	const float4 clip = float4(UVToClip(uv), 1.0, 1.0);
	const float4 world = mul(g_Frame.InverseViewProjection, clip);
	const float3 direction = normalize(world.xyz / world.w - g_Frame.CameraPosition.xyz);

	float3 color;
	if (g_Frame.EnvironmentParams.w > 0.5 && g_Frame.GridParams.w > 0.5)
	{
		color = g_Environment.SampleLevel(g_LinearClampSampler, RotateY(direction, -g_Frame.EnvironmentParams.y), 0).rgb * g_Frame.EnvironmentParams.x;
	}
	else
	{
		const float3 horizon = float3(0.55, 0.62, 0.70);
		const float3 zenith = float3(0.16, 0.30, 0.55);
		const float3 ground = float3(0.12, 0.11, 0.10);
		color = direction.y >= 0.0 ? lerp(horizon, zenith, pow(direction.y, 0.6)) : lerp(horizon, ground, pow(-direction.y, 0.4));
	}

	SkyOutput output;
	output.Color = float4(color, 1.0);
	output.Depth = 1.0;
	return output;
}
