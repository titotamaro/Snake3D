#pragma once

#include "Ouroboros/Core/PlatformDetection.h"
#include "Ouroboros/Core/Log.h"

// OB_CORE_ASSERT / OB_ASSERT are compiled out of release builds.
// OB_CORE_VERIFY / OB_VERIFY are always evaluated and always checked.

#define OB_INTERNAL_ASSERT_IMPL(logger, check, ...)                                         \
	do                                                                                       \
	{                                                                                        \
		if (!(check))                                                                        \
		{                                                                                    \
			logger("Assertion '{}' failed at {}:{}", #check, __FILE__, __LINE__);           \
			__VA_OPT__(logger(__VA_ARGS__);)                                                 \
			OB_DEBUGBREAK();                                                                 \
		}                                                                                    \
	} while (false)

#ifdef OB_DEBUG
	#define OB_CORE_ASSERT(check, ...) OB_INTERNAL_ASSERT_IMPL(OB_CORE_CRITICAL, check __VA_OPT__(, ) __VA_ARGS__)
	#define OB_ASSERT(check, ...) OB_INTERNAL_ASSERT_IMPL(OB_CRITICAL, check __VA_OPT__(, ) __VA_ARGS__)
#else
	#define OB_CORE_ASSERT(check, ...) ((void)0)
	#define OB_ASSERT(check, ...) ((void)0)
#endif

#define OB_CORE_VERIFY(check, ...) OB_INTERNAL_ASSERT_IMPL(OB_CORE_CRITICAL, check __VA_OPT__(, ) __VA_ARGS__)
#define OB_VERIFY(check, ...) OB_INTERNAL_ASSERT_IMPL(OB_CRITICAL, check __VA_OPT__(, ) __VA_ARGS__)
