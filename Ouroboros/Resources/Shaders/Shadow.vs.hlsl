// Depth-only vertex shader for cascaded shadow map rendering.
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);
StructuredBuffer<DrawData> g_Draws : register(t0);
[[vk::push_constant]] ConstantBuffer<PushConstants> g_Push;

float4 main(VertexInput input) : SV_Position
{
	const DrawData draw = g_Draws[g_Push.DrawIndex];
	const float4 worldPosition = mul(draw.Model, float4(input.Position, 1.0));
	return mul(g_Frame.CascadeViewProjection[g_Push.CascadeIndex], worldPosition);
}
