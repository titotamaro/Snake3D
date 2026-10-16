// Forward PBR lighting: metallic-roughness materials, image based lighting, soft cascaded shadows, point lights, SSAO.
#include "Common.hlsli"

ConstantBuffer<FrameConstants> g_Frame : register(b0, space0);
StructuredBuffer<DrawData> g_Draws : register(t0, space0);
Texture2DArray<float> g_ShadowMap : register(t1, space0);
TextureCube<float4> g_IrradianceMap : register(t2, space0);
TextureCube<float4> g_PrefilterMap : register(t3, space0);
Texture2D<float2> g_BrdfLut : register(t4, space0);
Texture2D<float> g_AOTexture : register(t5, space0);
SamplerState g_LinearClampSampler : register(s0, space0);
SamplerComparisonState g_ShadowSampler : register(s1, space0);
SamplerState g_PointClampSampler : register(s2, space0);

struct MaterialConstants
{
	float4 BaseColorFactor;
	float4 EmissiveFactor;   // rgb = emissive factor, w = emissive strength
	float MetallicFactor;
	float RoughnessFactor;
	float NormalScale;
	float OcclusionStrength;
};

ConstantBuffer<MaterialConstants> g_Material : register(b0, space1);
Texture2D<float4> g_BaseColorTexture : register(t0, space1);
Texture2D<float4> g_MetallicRoughnessTexture : register(t1, space1);
Texture2D<float4> g_NormalTexture : register(t2, space1);
Texture2D<float4> g_OcclusionTexture : register(t3, space1);
Texture2D<float4> g_EmissiveTexture : register(t4, space1);
SamplerState g_MaterialSampler : register(s0, space1);

static const float2 PoissonDisk[16] = {
	float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
	float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464), float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
	float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
	float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

float DistributionGGX(float NdotH, float roughness)
{
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * d * d, 1e-7);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
	const float r = roughness + 1.0;
	const float k = (r * r) / 8.0;
	const float ggxV = NdotV / (NdotV * (1.0 - k) + k);
	const float ggxL = NdotL / (NdotL * (1.0 - k) + k);
	return ggxV * ggxL;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	return F0 + (max((1.0 - roughness).xxx, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 EvaluateLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness, float3 F0)
{
	const float3 H = normalize(V + L);
	const float NdotL = saturate(dot(N, L));
	const float NdotV = max(dot(N, V), 1e-4);
	const float NdotH = saturate(dot(N, H));
	const float HdotV = saturate(dot(H, V));

	const float D = DistributionGGX(NdotH, roughness);
	const float G = GeometrySmith(NdotV, NdotL, roughness);
	const float3 F = FresnelSchlick(HdotV, F0);

	const float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
	const float3 kD = (1.0 - F) * (1.0 - metallic);
	return (kD * albedo / PI + specular) * radiance * NdotL;
}

uint SelectCascade(float viewDepth)
{
	uint cascade = 0;
	[unroll]
	for (uint i = 0; i < OB_CASCADE_COUNT - 1; i++)
	{
		if (viewDepth > g_Frame.CascadeSplits[i])
			cascade = i + 1;
	}
	return cascade;
}

// Percentage-closer soft shadows: a blocker search estimates the penumbra, then a rotated Poisson PCF filters it.
float SampleShadow(float3 worldPosition, float3 normal, float viewDepth, float2 pixel)
{
	if (g_Frame.LightColor.w < 0.5 || viewDepth > g_Frame.CascadeSplits[OB_CASCADE_COUNT - 1])
		return 1.0;

	const uint cascade = SelectCascade(viewDepth);
	const float texelSize = g_Frame.ShadowParams.y;
	const float cascadeScale = 1.0 + float(cascade) * 0.75;
	const float3 offsetPosition = worldPosition + normal * g_Frame.ShadowParams.w * cascadeScale;
	const float4 lightClip = mul(g_Frame.CascadeViewProjection[cascade], float4(offsetPosition, 1.0));
	const float3 ndc = lightClip.xyz / lightClip.w;
	const float2 uv = ClipToUV(ndc.xy);
	if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
		return 1.0;

	const float receiverDepth = ndc.z - g_Frame.ShadowParams.z;
	const float lightSize = g_Frame.ShadowParams.x;

	const float angle = InterleavedGradientNoise(pixel) * 2.0 * PI;
	const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));

	// Blocker search
	const float searchRadius = texelSize * (2.0 + 6.0 * lightSize) / cascadeScale;
	float blockerSum = 0.0;
	float blockerCount = 0.0;
	[unroll]
	for (int i = 0; i < 16; i++)
	{
		const float2 offset = mul(rotation, PoissonDisk[i]) * searchRadius;
		const float depth = g_ShadowMap.SampleLevel(g_PointClampSampler, float3(uv + offset, cascade), 0);
		if (depth < receiverDepth)
		{
			blockerSum += depth;
			blockerCount += 1.0;
		}
	}
	if (blockerCount < 1.0)
		return 1.0;

	const float averageBlocker = blockerSum / blockerCount;
	const float penumbra = saturate((receiverDepth - averageBlocker) * 40.0);
	const float filterRadius = texelSize * (1.0 + penumbra * 10.0 * lightSize) / cascadeScale;

	float visibility = 0.0;
	[unroll]
	for (int j = 0; j < 16; j++)
	{
		const float2 offset = mul(rotation, PoissonDisk[j]) * filterRadius;
		visibility += g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler, float3(uv + offset, cascade), receiverDepth);
	}
	return visibility / 16.0;
}

float4 main(MeshVaryings input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
	const DrawData draw = g_Draws[input.DrawIndex];

	// Material inputs
	const float4 baseColorSample = g_BaseColorTexture.Sample(g_MaterialSampler, input.TexCoord);
	const float3 albedo = baseColorSample.rgb * g_Material.BaseColorFactor.rgb * draw.Color.rgb;
	const float4 metallicRoughness = g_MetallicRoughnessTexture.Sample(g_MaterialSampler, input.TexCoord);
	const float metallic = saturate(draw.Metallic >= 0.0 ? draw.Metallic : metallicRoughness.b * g_Material.MetallicFactor);
	const float roughness = clamp(draw.Roughness >= 0.0 ? draw.Roughness : metallicRoughness.g * g_Material.RoughnessFactor, 0.045, 1.0);
	const float occlusion = lerp(1.0, g_OcclusionTexture.Sample(g_MaterialSampler, input.TexCoord).r, g_Material.OcclusionStrength);
	const float3 emissive = g_EmissiveTexture.Sample(g_MaterialSampler, input.TexCoord).rgb * g_Material.EmissiveFactor.rgb * g_Material.EmissiveFactor.w
		+ draw.Emissive.rgb * draw.Emissive.w;

	// Normal mapping
	float3 N = normalize(input.Normal);
	float3 T = input.Tangent.xyz - N * dot(N, input.Tangent.xyz);
	if (!isFrontFace)
		N = -N;
	if (dot(T, T) > 1e-8)
	{
		T = normalize(T);
		const float3 B = cross(N, T) * (input.Tangent.w < 0.0 ? -1.0 : 1.0);
		float3 tangentNormal = g_NormalTexture.Sample(g_MaterialSampler, input.TexCoord).xyz * 2.0 - 1.0;
		tangentNormal.xy *= g_Material.NormalScale;
		N = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
	}

	const float3 V = normalize(g_Frame.CameraPosition.xyz - input.WorldPosition);
	const float NdotV = max(dot(N, V), 1e-4);
	const float3 F0 = lerp(0.04.xxx, albedo, metallic);

	const float viewDepth = -mul(g_Frame.View, float4(input.WorldPosition, 1.0)).z;
	const float2 screenUV = input.Position.xy * g_Frame.ViewportSize.zw;

	float3 color = 0.0;

	// Directional light
	float shadow = 1.0;
	if (g_Frame.LightDirection.w > 0.5)
	{
		const float3 L = -g_Frame.LightDirection.xyz;
		shadow = SampleShadow(input.WorldPosition, normalize(input.Normal), viewDepth, input.Position.xy);
		color += EvaluateLight(N, V, L, g_Frame.LightColor.rgb, albedo, metallic, roughness, F0) * shadow;
	}

	// Point lights
	for (uint i = 0; i < g_Frame.Counts.x; i++)
	{
		const PointLight light = g_Frame.PointLights[i];
		const float3 toLight = light.PositionRadius.xyz - input.WorldPosition;
		const float distance = length(toLight);
		const float radius = max(light.PositionRadius.w, 1e-3);
		if (distance > radius)
			continue;
		const float window = pow(saturate(1.0 - pow(distance / radius, 4.0)), 2.0);
		const float attenuation = window / (distance * distance + 1.0);
		const float3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation;
		color += EvaluateLight(N, V, toLight / max(distance, 1e-4), radiance, albedo, metallic, roughness, F0);
	}

	// Ambient / image based lighting
	float ambientOcclusion = occlusion;
	if (g_Frame.AOParams.w > 0.5)
		ambientOcclusion *= g_AOTexture.SampleLevel(g_LinearClampSampler, screenUV, 0);

	const float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	const float3 kD = (1.0 - F) * (1.0 - metallic);
	float3 ambient;
	if (g_Frame.EnvironmentParams.w > 0.5)
	{
		const float rotation = -g_Frame.EnvironmentParams.y;
		const float3 R = reflect(-V, N);
		const float3 irradiance = g_IrradianceMap.SampleLevel(g_LinearClampSampler, RotateY(N, rotation), 0).rgb;
		const float maxMip = g_Frame.EnvironmentParams.z - 1.0;
		const float3 prefiltered = g_PrefilterMap.SampleLevel(g_LinearClampSampler, RotateY(R, rotation), roughness * maxMip).rgb;
		const float2 brdf = g_BrdfLut.SampleLevel(g_LinearClampSampler, float2(NdotV, roughness), 0);
		const float3 specular = prefiltered * (F * brdf.x + brdf.y);
		ambient = (kD * irradiance * albedo + specular) * g_Frame.EnvironmentParams.x;
	}
	else
	{
		ambient = g_Frame.AmbientColor.rgb * albedo * kD + g_Frame.AmbientColor.rgb * F * 0.25;
	}
	color += ambient * ambientOcclusion;
	color += emissive;

	// Debug views
	switch (g_Frame.Counts.w)
	{
		case 1: color = albedo; break;
		case 2: color = N * 0.5 + 0.5; break;
		case 3: color = ambientOcclusion.xxx; break;
		case 4:
		{
			const uint cascade = SelectCascade(viewDepth);
			const float3 tints[4] = { float3(1, 0.3, 0.3), float3(0.3, 1, 0.3), float3(0.3, 0.3, 1), float3(1, 1, 0.3) };
			color = tints[cascade] * (0.3 + 0.7 * shadow);
			break;
		}
		case 5: color = float3(metallic, roughness, 0.0); break;
		default: break;
	}

	return float4(color, 1.0);
}
