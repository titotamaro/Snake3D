// HDR -> display: exposure, ACES filmic tonemapping, sRGB encoding, dithering and the editor selection outline.
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);
Texture2D<float4> g_HDR : register(t0);
Texture2D<uint> g_EntityIDs : register(t1);
SamplerState g_PointClampSampler : register(s0);

// ACES fitted curve (Stephen Hill)
float3 ACESFitted(float3 color)
{
	const float3x3 inputMatrix = float3x3(
		0.59719, 0.35458, 0.04823,
		0.07600, 0.90834, 0.01566,
		0.02840, 0.13383, 0.83777);
	const float3x3 outputMatrix = float3x3(
		1.60475, -0.53108, -0.07367,
		-0.10208, 1.10813, -0.00605,
		-0.00327, -0.07276, 1.07602);

	color = mul(inputMatrix, color);
	const float3 a = color * (color + 0.0245786) - 0.000090537;
	const float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	color = a / b;
	return saturate(mul(outputMatrix, color));
}

float3 LinearToSRGB(float3 color)
{
	const float3 low = color * 12.92;
	const float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
	return lerp(high, low, color <= 0.0031308);
}

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	const float3 hdr = g_HDR.SampleLevel(g_PointClampSampler, uv, 0).rgb;
	float3 color = g_Frame.Counts.w == 0 ? ACESFitted(hdr * g_Frame.CameraPosition.w) : saturate(hdr);
	color = LinearToSRGB(color);

	// Selection outline: pixels next to (but not on) the selected entity.
	const uint selected = g_Frame.Counts.y;
	if (selected != 0)
	{
		const int2 pixel = int2(position.xy);
		const uint center = g_EntityIDs.Load(int3(pixel, 0));
		bool edge = false;
		[unroll]
		for (int y = -2; y <= 2; y++)
		{
			[unroll]
			for (int x = -2; x <= 2; x++)
			{
				const uint neighbour = g_EntityIDs.Load(int3(clamp(pixel + int2(x, y), int2(0, 0), int2(g_Frame.ViewportSize.xy) - 1), 0));
				edge = edge || ((neighbour == selected) != (center == selected));
			}
		}
		if (edge)
			color = float3(1.0, 0.55, 0.1);
	}

	// Triangular dither to hide banding in 8-bit output.
	const float noise = InterleavedGradientNoise(position.xy) - InterleavedGradientNoise(position.yx + 17.0);
	color += noise / 255.0;
	return float4(color, 1.0);
}
