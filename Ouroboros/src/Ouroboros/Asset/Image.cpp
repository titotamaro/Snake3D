#include "obpch.h"
#include "Ouroboros/Asset/Image.h"

#include "Ouroboros/Core/FileSystem.h"

#include <stb_image.h>
#include <stb_image_write.h>

namespace Ouroboros {

	namespace {

		float SRGBToLinear(float value)
		{
			return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		float LinearToSRGB(float value)
		{
			return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
		}

		const std::array<float, 256>& GetSRGBToLinearTable()
		{
			static const std::array<float, 256> s_Table = []()
			{
				std::array<float, 256> table{};
				for (int i = 0; i < 256; i++)
					table[i] = SRGBToLinear(static_cast<float>(i) / 255.0f);
				return table;
			}();
			return s_Table;
		}

		uint8_t ToByte(float value)
		{
			return static_cast<uint8_t>(std::clamp(value * 255.0f + 0.5f, 0.0f, 255.0f));
		}

	}

	std::optional<Image> ImageUtils::LoadFromMemory(const uint8_t* data, size_t size, std::string* error)
	{
		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
		if (!pixels)
		{
			if (error)
				*error = stbi_failure_reason() ? stbi_failure_reason() : "unknown image decoding error";
			return std::nullopt;
		}

		Image image;
		image.Width = static_cast<uint32_t>(width);
		image.Height = static_cast<uint32_t>(height);
		image.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
		stbi_image_free(pixels);
		return image;
	}

	std::optional<Image> ImageUtils::Load(const TextureSource& source, std::string* error)
	{
		if (!source.EncodedData.empty())
			return LoadFromMemory(source.EncodedData.data(), source.EncodedData.size(), error);

		std::optional<std::vector<uint8_t>> bytes = FileSystem::ReadBinaryFile(source.Path);
		if (!bytes)
		{
			if (error)
				*error = "could not read '" + source.Path.string() + "'";
			return std::nullopt;
		}
		return LoadFromMemory(bytes->data(), bytes->size(), error);
	}

	std::optional<ImageHDR> ImageUtils::LoadHDR(const std::filesystem::path& path, std::string* error)
	{
		std::optional<std::vector<uint8_t>> bytes = FileSystem::ReadBinaryFile(path);
		if (!bytes)
		{
			if (error)
				*error = "could not read '" + path.string() + "'";
			return std::nullopt;
		}

		int width = 0, height = 0, channels = 0;
		float* pixels = stbi_loadf_from_memory(bytes->data(), static_cast<int>(bytes->size()), &width, &height, &channels, 4);
		if (!pixels)
		{
			if (error)
				*error = stbi_failure_reason() ? stbi_failure_reason() : "unknown image decoding error";
			return std::nullopt;
		}

		ImageHDR image;
		image.Width = static_cast<uint32_t>(width);
		image.Height = static_cast<uint32_t>(height);
		image.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
		stbi_image_free(pixels);
		return image;
	}

	bool ImageUtils::WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, size_t rowPitch)
	{
		std::error_code errorCode;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), errorCode);

		// stbi_write_png only accepts narrow paths, so encode to memory and write through FileSystem.
		std::vector<uint8_t> png;
		auto append = [](void* context, void* data, int size)
		{
			auto* output = static_cast<std::vector<uint8_t>*>(context);
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			output->insert(output->end(), bytes, bytes + size);
		};
		if (!stbi_write_png_to_func(append, &png, static_cast<int>(width), static_cast<int>(height), 4, rgba, static_cast<int>(rowPitch)))
			return false;
		return FileSystem::WriteBinaryFile(path, png.data(), png.size());
	}

	uint32_t ImageUtils::CalculateMipCount(uint32_t width, uint32_t height)
	{
		uint32_t levels = 1;
		uint32_t size = std::max(width, height);
		while (size > 1)
		{
			size >>= 1;
			levels++;
		}
		return levels;
	}

	std::vector<Image> ImageUtils::GenerateMips(const Image& image, bool srgb)
	{
		std::vector<Image> mips;
		mips.push_back(image);
		const auto& toLinear = GetSRGBToLinearTable();

		while (mips.back().Width > 1 || mips.back().Height > 1)
		{
			const Image& source = mips.back();
			Image mip;
			mip.Width = std::max(1u, source.Width / 2);
			mip.Height = std::max(1u, source.Height / 2);
			mip.Pixels.resize(static_cast<size_t>(mip.Width) * mip.Height * 4);

			for (uint32_t y = 0; y < mip.Height; y++)
			{
				for (uint32_t x = 0; x < mip.Width; x++)
				{
					for (uint32_t channel = 0; channel < 4; channel++)
					{
						float sum = 0.0f;
						for (uint32_t dy = 0; dy < 2; dy++)
						{
							for (uint32_t dx = 0; dx < 2; dx++)
							{
								const uint32_t sx = std::min(x * 2 + dx, source.Width - 1);
								const uint32_t sy = std::min(y * 2 + dy, source.Height - 1);
								const uint8_t value = source.Pixels[(static_cast<size_t>(sy) * source.Width + sx) * 4 + channel];
								sum += (srgb && channel < 3) ? toLinear[value] : static_cast<float>(value) / 255.0f;
							}
						}
						const float average = sum * 0.25f;
						mip.Pixels[(static_cast<size_t>(y) * mip.Width + x) * 4 + channel] = ToByte((srgb && channel < 3) ? LinearToSRGB(average) : average);
					}
				}
			}
			mips.push_back(std::move(mip));
		}
		return mips;
	}

}
