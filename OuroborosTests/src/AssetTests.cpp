#include "TestUtilities.h"

#include <Ouroboros/Asset/GltfImporter.h>
#include <Ouroboros/Asset/Image.h>
#include <Ouroboros/Asset/MeshFactory.h>
#include <Ouroboros/Core/FileSystem.h>

using namespace Ouroboros;

namespace {

	// Every triangle must wind counter-clockwise when seen from outside: its geometric normal agrees with the
	// vertex normals (front faces are counter-clockwise in the renderer).
	void CheckOutwardWinding(const MeshData& mesh)
	{
		size_t wrong = 0;
		for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
		{
			const Vertex& a = mesh.Vertices[mesh.Indices[i]];
			const Vertex& b = mesh.Vertices[mesh.Indices[i + 1]];
			const Vertex& c = mesh.Vertices[mesh.Indices[i + 2]];
			const glm::vec3 faceNormal = glm::cross(b.Position - a.Position, c.Position - a.Position);
			if (glm::dot(faceNormal, faceNormal) < 1e-12f)
				continue; // degenerate (sphere poles)
			if (glm::dot(faceNormal, a.Normal + b.Normal + c.Normal) <= 0.0f)
				wrong++;
		}
		CHECK(wrong == 0);
	}

}

TEST_SUITE("Assets")
{
	TEST_CASE("Primitive meshes are valid, outward facing and have tangents")
	{
		for (const char* name : { "Primitive:Cube", "Primitive:Sphere", "Primitive:Plane" })
		{
			CAPTURE(name);
			std::optional<MeshData> mesh = MeshFactory::CreatePrimitive(name);
			REQUIRE(mesh);
			std::string error;
			CHECK_MESSAGE(mesh->Validate(&error), error);
			CheckOutwardWinding(*mesh);
			CHECK(mesh->Materials.size() == 1);
			CHECK(mesh->Materials[0].MetallicFactor == doctest::Approx(0.0f));
			for (const Vertex& vertex : mesh->Vertices)
			{
				CHECK(std::abs(glm::dot(glm::vec3(vertex.Tangent), vertex.Normal)) < 1e-3f);
				CHECK(std::abs(vertex.Tangent.w) == doctest::Approx(1.0f));
			}
		}

		const MeshData cube = MeshFactory::CreateCube();
		CHECK(cube.Vertices.size() == 24);
		CHECK(cube.Indices.size() == 36);
		CHECK(cube.BoundsMin.x == doctest::Approx(-0.5f));
		CHECK(cube.BoundsMax.y == doctest::Approx(0.5f));

		const MeshData sphere = MeshFactory::CreateSphere(8, 4);
		CHECK(sphere.Indices.size() == 8 * 4 * 6);
		for (const Vertex& vertex : sphere.Vertices)
			CHECK(glm::length(vertex.Position) == doctest::Approx(0.5f));

		CHECK(MeshFactory::IsPrimitivePath("Primitive:Cube"));
		CHECK_FALSE(MeshFactory::IsPrimitivePath("Meshes/Cube.gltf"));
		CHECK_FALSE(MeshFactory::CreatePrimitive("Primitive:Teapot"));
		CHECK_FALSE(MeshFactory::CreatePrimitive("Meshes/Apple.gltf"));
	}

	TEST_CASE("MeshData validation")
	{
		MeshData mesh = MeshFactory::CreatePlane();
		std::string error;
		REQUIRE(mesh.Validate(&error));

		MeshData badIndex = mesh;
		badIndex.Indices[0] = 99;
		CHECK_FALSE(badIndex.Validate(&error));
		CHECK(error == "index out of range");

		MeshData badMaterial = mesh;
		badMaterial.Submeshes[0].MaterialIndex = 3;
		CHECK_FALSE(badMaterial.Validate(&error));

		MeshData badRange = mesh;
		badRange.Submeshes[0].IndexCount = 100;
		CHECK_FALSE(badRange.Validate(&error));

		MeshData notTriangles = mesh;
		notTriangles.Indices.pop_back();
		CHECK_FALSE(notTriangles.Validate(&error));

		CHECK_FALSE(MeshData().Validate(&error));
		MeshData empty;
		empty.ComputeBounds();
		CHECK(empty.BoundsMax == glm::vec3(0.0f));
	}

	TEST_CASE("glTF import of the game models")
	{
		Testing::ScopedProject project;

		std::string error;
		std::optional<MeshData> apple = GltfImporter::Import(Project::ResolveAssetPath("Meshes/Apple.gltf"), &error);
		REQUIRE_MESSAGE(apple, error);
		CHECK(apple->Submeshes.size() == 3);
		REQUIRE(apple->Materials.size() == 3);
		CHECK(apple->Materials[0].Name == "AppleSkin");
		CHECK(apple->Materials[0].BaseColorFactor.r == doctest::Approx(0.86f));
		CHECK(apple->Materials[0].EmissiveFactor.r == doctest::Approx(0.08f));
		CheckOutwardWinding(*apple);

		std::optional<MeshData> wall = GltfImporter::Import(Project::ResolveAssetPath("Meshes/Wall.gltf"), &error);
		REQUIRE_MESSAGE(wall, error);
		REQUIRE(wall->Materials.size() == 1);
		const MaterialData& stone = wall->Materials[0];
		CHECK(stone.BaseColorTexture.IsValid());
		CHECK(std::filesystem::exists(stone.BaseColorTexture.Path));
		CHECK(std::filesystem::exists(stone.NormalTexture.Path));
		CHECK(std::filesystem::exists(stone.MetallicRoughnessTexture.Path));
		CHECK_FALSE(stone.OcclusionTexture.IsValid());
		CheckOutwardWinding(*wall);

		for (const char* model : { "Meshes/SnakeHead.gltf", "Meshes/SnakeBody.gltf", "Meshes/Board.gltf" })
		{
			CAPTURE(model);
			std::optional<MeshData> mesh = GltfImporter::Import(Project::ResolveAssetPath(model), &error);
			REQUIRE_MESSAGE(mesh, error);
			CheckOutwardWinding(*mesh);
			CHECK(mesh->BoundsMax.x > mesh->BoundsMin.x);
		}
	}

	TEST_CASE("glTF import errors")
	{
		Testing::TemporaryDirectory directory;
		std::string error;
		CHECK_FALSE(GltfImporter::Import(directory.Path() / "missing.gltf", &error));
		CHECK(error.find("could not parse") != std::string::npos);

		FileSystem::WriteTextFile(directory.Path() / "broken.gltf", "{ \"asset\": { \"version\": \"2.0\" }, \"buffers\": [ { \"uri\": \"nope.bin\", \"byteLength\": 4 } ] }");
		CHECK_FALSE(GltfImporter::Import(directory.Path() / "broken.gltf", &error));

		FileSystem::WriteTextFile(directory.Path() / "empty.gltf", "{ \"asset\": { \"version\": \"2.0\" } }");
		CHECK_FALSE(GltfImporter::Import(directory.Path() / "empty.gltf", &error));
		CHECK(error.find("no geometry") != std::string::npos);
	}

	TEST_CASE("Images decode, encode and build mip chains")
	{
		Testing::ScopedProject project;
		std::string error;

		TextureSource source;
		source.Path = Project::ResolveAssetPath("Textures/Grass_BaseColor.png");
		std::optional<Image> grass = ImageUtils::Load(source, &error);
		REQUIRE_MESSAGE(grass, error);
		CHECK(grass->Width == 128);
		CHECK(grass->Pixels.size() == 128 * 128 * 4);

		const std::vector<Image> mips = ImageUtils::GenerateMips(*grass, true);
		CHECK(mips.size() == ImageUtils::CalculateMipCount(128, 128));
		CHECK(mips.back().Width == 1);
		CHECK(ImageUtils::CalculateMipCount(256, 64) == 9);
		CHECK(ImageUtils::CalculateMipCount(1, 1) == 1);

		// A 2x1 black/white image averages to mid grey in linear space (brighter than 128 in sRGB).
		Image pair{ 2, 1, { 0, 0, 0, 255, 255, 255, 255, 255 } };
		const std::vector<Image> pairMips = ImageUtils::GenerateMips(pair, true);
		REQUIRE(pairMips.size() == 2);
		CHECK(pairMips[1].Pixels[0] > 180);
		const std::vector<Image> linearMips = ImageUtils::GenerateMips(pair, false);
		CHECK(linearMips[1].Pixels[0] == doctest::Approx(128).epsilon(0.01));

		Testing::TemporaryDirectory directory;
		const std::filesystem::path written = directory.Path() / "out" / "image.png";
		REQUIRE(ImageUtils::WritePNG(written, pair.Width, pair.Height, pair.Pixels.data(), pair.Width * 4));
		TextureSource writtenSource;
		writtenSource.Path = written;
		std::optional<Image> reloaded = ImageUtils::Load(writtenSource, &error);
		REQUIRE(reloaded);
		CHECK(reloaded->Pixels == pair.Pixels);

		const std::optional<std::vector<uint8_t>> bytes = FileSystem::ReadBinaryFile(written);
		REQUIRE(bytes);
		TextureSource embedded;
		embedded.EncodedData = *bytes;
		embedded.Name = "embedded";
		CHECK(embedded.GetCacheKey() == "embedded");
		CHECK(ImageUtils::Load(embedded, &error));

		std::optional<ImageHDR> hdr = ImageUtils::LoadHDR(Project::ResolveAssetPath("Environments/kloofendal_48d_partly_cloudy_puresky_1k.hdr"), &error);
		REQUIRE_MESSAGE(hdr, error);
		CHECK(hdr->Width == 2 * hdr->Height);
		float brightest = 0.0f;
		for (size_t i = 0; i < hdr->Pixels.size(); i += 4)
			brightest = std::max(brightest, hdr->Pixels[i]);
		CHECK(brightest > 1.0f); // high dynamic range

		TextureSource missing;
		missing.Path = directory.Path() / "missing.png";
		CHECK_FALSE(ImageUtils::Load(missing, &error));
		CHECK_FALSE(ImageUtils::LoadFromMemory(reinterpret_cast<const uint8_t*>("junk"), 4, &error));
		CHECK_FALSE(ImageUtils::LoadHDR(directory.Path() / "missing.hdr", &error));
	}
}
