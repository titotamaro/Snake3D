#include "TestUtilities.h"

#include <Ouroboros/Math/Math.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace Ouroboros;

namespace {

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(epsilon)));
	}

}

TEST_SUITE("Math")
{
	TEST_CASE("Compose and decompose transforms round-trip")
	{
		const glm::vec3 translation(1.0f, -2.0f, 3.5f);
		const glm::vec3 rotation(0.3f, -0.7f, 1.1f);
		const glm::vec3 scale(2.0f, 0.5f, 1.5f);

		const glm::mat4 transform = Math::ComposeTransform(translation, rotation, scale);
		glm::vec3 outTranslation, outRotation, outScale;
		REQUIRE(Math::DecomposeTransform(transform, outTranslation, outRotation, outScale));
		CHECK(NearlyEqual(outTranslation, translation));
		CHECK(NearlyEqual(outScale, scale));

		// Euler angles are not unique; compare the resulting transforms instead.
		const glm::mat4 recomposed = Math::ComposeTransform(outTranslation, outRotation, outScale);
		for (int column = 0; column < 4; column++)
			CHECK(NearlyEqual(glm::vec3(recomposed[column]), glm::vec3(transform[column])));
	}

	TEST_CASE("Decompose rejects degenerate matrices")
	{
		glm::vec3 translation, rotation, scale;
		CHECK_FALSE(Math::DecomposeTransform(glm::mat4(0.0f), translation, rotation, scale));
	}

	TEST_CASE("Normal matrix handles non-uniform scale")
	{
		const glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 1.0f));
		const glm::vec3 normal = glm::normalize(Math::NormalMatrix(transform) * glm::vec3(1.0f, 1.0f, 0.0f));
		// The normal of a surface stretched along X tilts towards Y.
		CHECK(normal.y > normal.x);
		CHECK(Math::NormalMatrix(glm::mat4(0.0f)) == glm::mat3(1.0f));
	}

	TEST_CASE("Perspective projection maps depth to [0, 1] with clip-space Y up")
	{
		const glm::mat4 projection = Math::PerspectiveProjection(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 100.0f);

		glm::vec4 nearPoint = projection * glm::vec4(0.0f, 0.0f, -0.5f, 1.0f);
		glm::vec4 farPoint = projection * glm::vec4(0.0f, 0.0f, -100.0f, 1.0f);
		CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0f).epsilon(1e-5));
		CHECK(farPoint.z / farPoint.w == doctest::Approx(1.0f).epsilon(1e-5));

		const glm::vec4 above = projection * glm::vec4(0.0f, 1.0f, -10.0f, 1.0f);
		CHECK(above.y / above.w > 0.0f);
	}

	TEST_CASE("Orthographic projection")
	{
		const glm::mat4 projection = Math::OrthographicProjection(-10.0f, 10.0f, -5.0f, 5.0f, 0.0f, 50.0f);
		const glm::vec4 corner = projection * glm::vec4(10.0f, 5.0f, -50.0f, 1.0f);
		CHECK(corner.x == doctest::Approx(1.0f));
		CHECK(corner.y == doctest::Approx(1.0f));
		CHECK(corner.z == doctest::Approx(1.0f));
		CHECK(Math::Pi == doctest::Approx(3.14159265f));
	}
}
