// Fullscreen triangle. UV (0,0) is the top-left corner; clip space is Y up (D3D conventions, as used by NVRHI).
struct FullscreenVaryings
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
};

FullscreenVaryings main(uint vertexID : SV_VertexID)
{
	FullscreenVaryings output;
	output.UV = float2((vertexID << 1) & 2, vertexID & 2);
	output.Position = float4(output.UV.x * 2.0 - 1.0, 1.0 - output.UV.y * 2.0, 0.0, 1.0);
	return output;
}
