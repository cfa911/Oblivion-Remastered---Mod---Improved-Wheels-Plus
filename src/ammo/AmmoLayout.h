#pragma once
#include <array>
#include <algorithm>
#include <cstdint>

namespace ammolayout
{
	struct Layout
	{
		std::array<std::uint32_t, 8> ids{};
		std::array<int, 8> savedSlots{};
		int count = 0;
		bool full = true;
	};

	// Compact presentation never truncates saved assignments. Six or more use a full ring.
	inline Layout Build(const std::array<std::uint32_t, 8>& slots, bool centred)
	{
		Layout out;
		for (int s = 0; s < 8; ++s) {
			if (!slots[s]) continue;
			out.savedSlots[out.count] = s;
			out.ids[out.count++] = slots[s];
		}
		out.full = centred || out.count > 5;
		return out;
	}
}
