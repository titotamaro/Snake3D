// Ouroboros shared shader declarations. Keep in sync with Ouroboros/src/Ouroboros/Renderer/ShaderTypes.h.
#ifndef OB_COMMON_HLSLI
#define OB_COMMON_HLSLI

static const float PI = 3.14159265359;

#define OB_MAX_POINT_LIGHTS 16
#define OB_CASCADE_COUNT 4

struct PointLight
{
	float4 PositionRadius;  // xyz = position, w = radius
	float4 ColorIntensity;  // rgb = color, w = intensity
};

struct FrameConstants
{
	float4x4 View;
	float4x4 Projection;
	float4x4 ViewProjection;
	float4x4 InverseView;
	float4x4 InverseProjection;
	float4x4 InverseViewProjection;
	float4x4 CascadeViewProjection[OB_CASCADE_COUNT];
	float4 CascadeSplits;     // view-space distance at which each cascade ends
	float4 CameraPosition;    // xyz = position, w = exposure
	float4 LightDirection;    // xyz = direction light travels (normalized), w = 1 if a directional light exists
	float4 LightColor;        // rgb = color * intensity, w = 1 if shadows are enabled
	float4 ShadowParams;      // x = light size (softness), y = 1 / shadow map size, z = depth bias, w = normal bias
	float4 EnvironmentParams; // x = intensity, y = rotation (radians), z = prefiltered mip count, w = 1 if environment
	float4 ViewportSize;      // xy = size, zw = 1 / size
	float4 AOParams;          // x = radius, y = intensity, z = bias, w = 1 if enabled
	float4 AmbientColor;      // rgb = ambient used when there is no environment
	float4 GridParams;        // x = 1 if grid enabled, y = cell size, z = fade distance, w = 1 to show the environment as background
	uint4 Counts;             // x = point light count, y = selected entity id, z = frame index, w = debug view
	PointLight PointLights[OB_MAX_POINT_LIGHTS];
};

struct DrawData
{
	float4x4 Model;
	float4x4 NormalMatrix;
	float4 Color;           // base color multiplier
	float4 Emissive;        // rgb = added emissive, w = emissive intensity
	float Metallic;         // >= 0 overrides the material metallic value
	float Roughness;        // >= 0 overrides the material roughness value
	uint EntityID;          // ECS handle + 1 (0 = none)
	uint Padding;
};

struct PushConstants
{
	uint DrawIndex;
	uint CascadeIndex;
	uint Padding0;
	uint Padding1;
};

struct VertexInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 TexCoord : TEXCOORD0;
};

struct MeshVaryings
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 TexCoord : TEXCOORD0;
	nointerpolation uint DrawIndex : DRAW_INDEX;
};

float3 RotateY(float3 v, float angle)
{
	const float s = sin(angle);
	const float c = cos(angle);
	return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

// Screen UV (0,0 = top-left) <-> clip-space XY (Y up). NVRHI uses D3D viewport conventions on every backend.
float2 UVToClip(float2 uv)
{
	return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

float2 ClipToUV(float2 clip)
{
	return float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
}

// Reconstructs a view-space position from a depth buffer value and screen UV.
float3 ViewPositionFromDepth(float2 uv, float depth, float4x4 inverseProjection)
{
	const float4 clip = float4(UVToClip(uv), depth, 1.0);
	const float4 view = mul(inverseProjection, clip);
	return view.xyz / view.w;
}

float InterleavedGradientNoise(float2 pixel)
{
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

#endif
