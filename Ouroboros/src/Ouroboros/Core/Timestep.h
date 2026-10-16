#pragma once

#include <chrono>

namespace Ouroboros {

	class Timestep
	{
	public:
		constexpr Timestep(float seconds = 0.0f)
			: m_Seconds(seconds)
		{
		}

		constexpr operator float() const { return m_Seconds; }

		constexpr float GetSeconds() const { return m_Seconds; }
		constexpr float GetMilliseconds() const { return m_Seconds * 1000.0f; }

	private:
		float m_Seconds;
	};

	class Timer
	{
	public:
		Timer() { Reset(); }

		void Reset() { m_Start = std::chrono::steady_clock::now(); }

		float Elapsed() const
		{
			return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_Start).count();
		}

		float ElapsedMillis() const { return Elapsed() * 1000.0f; }

	private:
		std::chrono::steady_clock::time_point m_Start;
	};

}
