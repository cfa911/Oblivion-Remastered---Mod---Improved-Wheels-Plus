#pragma once
#include <cstdint>
#include <unordered_set>

namespace carriedhistory
{
	inline void Reset(std::unordered_set<std::uint32_t>& seen, std::uint64_t& nextCheck)
	{
		seen.clear();
		nextCheck = 0;
	}
}
