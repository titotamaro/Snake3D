// Dear ImGui vertex shader.
struct ImGuiConstants
{
	float2 Scale;
	float2 Translate;
};
[[vk::push_constant]] ConstantBuffer<ImGuiConstants> g_Constants;

struct VertexInput
{
	float2 Position : POSITION;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
};

struct Varyings
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
};

Varyings main(VertexInput input)
{
	Varyings output;
	output.Position = float4(input.Position * g_Constants.Scale + g_Constants.Translate, 0.0, 1.0);
	output.UV = input.UV;
	output.Color = input.Color;
	return output;
}
