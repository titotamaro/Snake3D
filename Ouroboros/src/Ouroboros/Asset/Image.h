#pragma once

#include "Ouroboros/Asset/MeshData.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Ouroboros {

	// 8-bit RGBA image.
	struct Image
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels;
	};

	// 32-bit float RGBA image.
	struct ImageHDR
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<float> Pixels;
	};

	class ImageUtils
	{
	public:
		// Decodes PNG/JPEG/TGA/BMP (any stb_image format) from a file or embedded bytes into RGBA8.
		static std::optional<Image> Load(const TextureSource& source, std::string* error = nullptr);
		static std::optional<Image> LoadFromMemory(const uint8_t* data, size_t size, std::string* error = nullptr);
		// Decodes a Radiance .hdr (or any image, converted to linear float) into RGBA32F.
		static std::optional<ImageHDR> LoadHDR(const std::filesystem::path& path, std::string* error = nullptr);

		static bool WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, size_t rowPitch);

		// Builds the full mip chain (level 0 = the input). `srgb` filters in linear space.
		static std::vector<Image> GenerateMips(const Image& image, bool srgb);
		static uint32_t CalculateMipCount(uint32_t width, uint32_t height);
	};

}
