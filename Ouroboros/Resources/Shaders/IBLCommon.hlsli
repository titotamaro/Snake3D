// Helpers shared by the image based lighting precomputation compute shaders.
#ifndef OB_IBL_COMMON_HLSLI
#define OB_IBL_COMMON_HLSLI

static const float PI = 3.14159265359;

struct IBLConstants
{
	uint OutputSize;     // face size of the mip being written
	uint MipLevel;
	float Roughness;     // prefilter roughness for this mip
	uint SampleCount;
	float SourceSize;    // face size of the source cube's mip 0
	float3 Padding;
};

// Direction through texel `pixel` of cube face `face` (D3D/Vulkan face order +X, -X, +Y, -Y, +Z, -Z).
float3 CubeDirection(uint face, uint2 pixel, uint size)
{
	const float2 uv = (float2(pixel) + 0.5) / float(size) * 2.0 - 1.0;
	float3 direction;
	switch (face)
	{
		case 0: direction = float3(1.0, -uv.y, -uv.x); break;
		case 1: direction = float3(-1.0, -uv.y, uv.x); break;
		case 2: direction = float3(uv.x, 1.0, uv.y); break;
		case 3: direction = float3(uv.x, -1.0, -uv.y); break;
		case 4: direction = float3(uv.x, -uv.y, 1.0); break;
		default: direction = float3(-uv.x, -uv.y, -1.0); break;
	}
	return normalize(direction);
}

float RadicalInverseVdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint count)
{
	return float2(float(i) / float(count), RadicalInverseVdC(i));
}

float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness)
{
	const float a = roughness * roughness;
	const float phi = 2.0 * PI * xi.x;
	const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
	const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	const float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

	const float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);
	return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float DistributionGGX(float NdotH, float roughness)
{
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}

#endif
