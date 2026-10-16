// Editor ground grid on the Y = 0 plane (alpha blended, depth tested against the scene).
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);

struct GridOutput
{
	float4 Color : SV_Target0;
	float Depth : SV_Depth;
};

float GridLines(float2 coordinate, float cellSize)
{
	const float2 scaled = coordinate / cellSize;
	const float2 derivative = fwidth(scaled);
	const float2 grid = abs(frac(scaled - 0.5) - 0.5) / max(derivative, 1e-5);
	return 1.0 - min(min(grid.x, grid.y), 1.0);
}

GridOutput main(float4 position : SV_Position, float2 uv : TEXCOORD0)
{
	const float4 nearWorld = mul(g_Frame.InverseViewProjection, float4(UVToClip(uv), 0.0, 1.0));
	const float4 farWorld = mul(g_Frame.InverseViewProjection, float4(UVToClip(uv), 1.0, 1.0));
	const float3 rayOrigin = nearWorld.xyz / nearWorld.w;
	const float3 rayEnd = farWorld.xyz / farWorld.w;
	const float3 rayDirection = rayEnd - rayOrigin;

	const float t = -rayOrigin.y / rayDirection.y;
	if (abs(rayDirection.y) < 1e-6 || t <= 0.0 || t >= 1.0)
		discard;

	const float3 hit = rayOrigin + rayDirection * t;
	const float4 clip = mul(g_Frame.ViewProjection, float4(hit, 1.0));

	const float cellSize = g_Frame.GridParams.y;
	const float minor = GridLines(hit.xz, cellSize);
	const float major = GridLines(hit.xz, cellSize * 10.0);

	const float distance = length(hit - g_Frame.CameraPosition.xyz);
	const float fade = saturate(1.0 - distance / g_Frame.GridParams.z);

	float3 color = lerp(float3(0.35, 0.35, 0.35), float3(0.6, 0.6, 0.6), major);
	if (abs(hit.x) < cellSize * 0.05 * max(1.0, distance * 0.05))
		color = float3(0.25, 0.35, 0.9);
	if (abs(hit.z) < cellSize * 0.05 * max(1.0, distance * 0.05))
		color = float3(0.9, 0.25, 0.25);

	const float alpha = max(minor * 0.35, major * 0.6) * fade;
	if (alpha <= 0.001)
		discard;

	GridOutput output;
	output.Color = float4(color, alpha);
	output.Depth = clip.z / clip.w;
	return output;
}
