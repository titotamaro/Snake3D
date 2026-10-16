// Depth/normal pre-pass: writes view-space normals (for SSAO) and entity IDs (for editor picking/selection).
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0);
StructuredBuffer<DrawData> g_Draws : register(t0);

struct PrePassOutput
{
	float4 Normal : SV_Target0;
	uint EntityID : SV_Target1;
};

PrePassOutput main(MeshVaryings input, bool isFrontFace : SV_IsFrontFace)
{
	float3 normal = normalize(input.Normal);
	if (!isFrontFace)
		normal = -normal;

	PrePassOutput output;
	output.Normal = float4(normalize(mul((float3x3)g_Frame.View, normal)), 1.0);
	output.EntityID = g_Draws[input.DrawIndex].EntityID;
	return output;
}
