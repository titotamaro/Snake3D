#pragma once

#if defined(_WIN32) || defined(_WIN64)
	#define OB_PLATFORM_WINDOWS
#elif defined(__APPLE__) && defined(__MACH__)
	#define OB_PLATFORM_MACOS
#elif defined(__linux__)
	#define OB_PLATFORM_LINUX
#else
	#error "Unsupported platform: Ouroboros supports Windows, macOS and Linux"
#endif

#if defined(OB_PLATFORM_WINDOWS)
	#define OB_DEBUGBREAK() __debugbreak()
#elif defined(OB_PLATFORM_MACOS) || defined(OB_PLATFORM_LINUX)
	#include <csignal>
	#define OB_DEBUGBREAK() raise(SIGTRAP)
#endif
