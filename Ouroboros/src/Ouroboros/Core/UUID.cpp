#include "obpch.h"
#include "Ouroboros/Core/UUID.h"

#include <random>

namespace Ouroboros {

	namespace {

		uint64_t GenerateNonZero()
		{
			thread_local std::mt19937_64 s_Engine(std::random_device{}() ^ (static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())));
			thread_local std::uniform_int_distribution<uint64_t> s_Distribution(1, UUID::MaxValue);
			return s_Distribution(s_Engine);
		}

	}

	UUID::UUID()
		: m_UUID(GenerateNonZero())
	{
	}

}
