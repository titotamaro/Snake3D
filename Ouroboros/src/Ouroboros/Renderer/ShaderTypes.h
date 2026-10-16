#pragma once

#include <glm/glm.hpp>

#include <cstdint>

// C++ mirrors of the structures in Ouroboros/Resources/Shaders/Common.hlsli and IBLCommon.hlsli.
// Shaders are compiled with -fvk-use-dx-layout and column-major matrices, so these layouts must match HLSL packing.

namespace Ouroboros {

	constexpr uint32_t MaxPointLights = 16;
	constexpr uint32_t ShadowCascadeCount = 4;

	struct GPUPointLight
	{
		glm::vec4 PositionRadius;
		glm::vec4 ColorIntensity;
	};

	struct GPUFrameConstants
	{
		glm::mat4 View;
		glm::mat4 Projection;
		glm::mat4 ViewProjection;
		glm::mat4 InverseView;
		glm::mat4 InverseProjection;
		glm::mat4 InverseViewProjection;
		glm::mat4 CascadeViewProjection[ShadowCascadeCount];
		glm::vec4 CascadeSplits;
		glm::vec4 CameraPosition;
		glm::vec4 LightDirection;
		glm::vec4 LightColor;
		glm::vec4 ShadowParams;
		glm::vec4 EnvironmentParams;
		glm::vec4 ViewportSize;
		glm::vec4 AOParams;
		glm::vec4 AmbientColor;
		glm::vec4 GridParams;
		glm::uvec4 Counts;
		GPUPointLight PointLights[MaxPointLights];
	};

	struct GPUDrawData
	{
		glm::mat4 Model;
		glm::mat4 NormalMatrix;
		glm::vec4 Color;
		glm::vec4 Emissive;
		float Metallic;
		float Roughness;
		uint32_t EntityID;
		uint32_t Padding;
	};
	static_assert(sizeof(GPUDrawData) == 176, "GPUDrawData must match DrawData in Common.hlsli");

	struct GPUPushConstants
	{
		uint32_t DrawIndex;
		uint32_t CascadeIndex;
		uint32_t Padding0;
		uint32_t Padding1;
	};

	struct GPUMaterialConstants
	{
		glm::vec4 BaseColorFactor;
		glm::vec4 EmissiveFactor;
		float MetallicFactor;
		float RoughnessFactor;
		float NormalScale;
		float OcclusionStrength;
	};

	struct GPUIBLConstants
	{
		uint32_t OutputSize;
		uint32_t MipLevel;
		float Roughness;
		uint32_t SampleCount;
		float SourceSize;
		float Padding[3];
	};
	static_assert(sizeof(GPUIBLConstants) == 32, "GPUIBLConstants must match IBLConstants");

}
