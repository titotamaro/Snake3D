#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "TestUtilities.h"

#include <Ouroboros/Audio/AudioEngine.h>
#include <Ouroboros/Core/Log.h>

int main(int argc, char** argv)
{
	Ouroboros::Log::Init();
	// Keep test output readable: only warnings and errors from the engine.
	Ouroboros::Log::GetCoreLogger()->set_level(spdlog::level::warn);
	Ouroboros::Log::GetClientLogger()->set_level(spdlog::level::warn);

	doctest::Context context;
	context.applyCommandLine(argc, argv);
	const int result = context.run();

	Ouroboros::Testing::ShutdownGpuEnvironment();
	Ouroboros::AudioEngine::Shutdown();

	Ouroboros::Log::Shutdown();
	return result;
}
