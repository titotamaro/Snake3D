#pragma once

#include <cstdint>
#include <functional>

namespace Ouroboros {

	// Random entity identifier. Zero is reserved and means "no UUID".
	// Generated values are limited to 53 bits so they survive round trips through Lua numbers and JSON parsers that
	// store numbers as IEEE doubles (JavaScript, many agent tools).
	class UUID
	{
	public:
		static constexpr uint64_t MaxValue = (1ull << 53) - 1;

		UUID();
		constexpr UUID(uint64_t uuid)
			: m_UUID(uuid)
		{
		}

		constexpr operator uint64_t() const { return m_UUID; }

	private:
		uint64_t m_UUID;
	};

}

namespace std {

	template<>
	struct hash<Ouroboros::UUID>
	{
		std::size_t operator()(const Ouroboros::UUID& uuid) const noexcept
		{
			return std::hash<uint64_t>()(static_cast<uint64_t>(uuid));
		}
	};

}
