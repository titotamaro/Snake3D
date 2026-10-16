// Shared vertex shader for the depth/normal pre-pass and the forward PBR pass.
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);
StructuredBuffer<DrawData> g_Draws : register(t0);
[[vk::push_constant]] ConstantBuffer<PushConstants> g_Push;

MeshVaryings main(VertexInput input)
{
	const DrawData draw = g_Draws[g_Push.DrawIndex];
	const float4 worldPosition = mul(draw.Model, float4(input.Position, 1.0));

	MeshVaryings output;
	output.Position = mul(g_Frame.ViewProjection, worldPosition);
	output.WorldPosition = worldPosition.xyz;
	output.Normal = normalize(mul((float3x3)draw.NormalMatrix, input.Normal));
	output.Tangent = float4(normalize(mul((float3x3)draw.Model, input.Tangent.xyz)), input.Tangent.w);
	output.TexCoord = input.TexCoord;
	output.DrawIndex = g_Push.DrawIndex;
	return output;
}
