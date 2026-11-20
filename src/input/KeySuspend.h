#pragma once
// Taking one key off a mapping array for a while and putting it back exactly (IWP-043: the left button must not reach the
// game's Attack while the Ammo shortcut is open). Pure: works on the raw array of FEnhancedActionKeyMapping.
#include <cstdint>
#include <cstring>
#include <vector>

namespace keysuspend
{
	struct Saved
	{
		std::int32_t  index = -1;
		std::uint64_t name = 0;   // the FKey's FName (its first 8 bytes)
	};

	// every entry a_isTarget(entry) picks gets an empty key name (None: no key); returns what it was. Only the FName is
	// changed: the key's details pointer, triggers and modifiers stay as they are, so putting the name back restores it.
	template <class IsTarget>
	std::vector<Saved> Suspend(std::uint8_t* a_data, std::int32_t a_num, std::int32_t a_size, std::int32_t a_keyOffset, IsTarget&& a_isTarget)
	{
		std::vector<Saved> out;
		for (std::int32_t i = 0; a_data && i < a_num; ++i) {
			std::uint8_t* entry = a_data + static_cast<std::ptrdiff_t>(i) * a_size;
			if (!a_isTarget(static_cast<const std::uint8_t*>(entry))) continue;
			Saved s;
			s.index = i;
			std::memcpy(&s.name, entry + a_keyOffset, sizeof(s.name));
			std::memset(entry + a_keyOffset, 0, sizeof(s.name));
			out.push_back(s);
		}
		return out;
	}

	// the saved key names back, only where the entry still has the empty name (something else may have rebound it)
	inline int Restore(std::uint8_t* a_data, std::int32_t a_num, std::int32_t a_size, std::int32_t a_keyOffset, const std::vector<Saved>& a_saved)
	{
		int back = 0;
		for (const auto& s : a_saved) {
			if (!a_data || s.index < 0 || s.index >= a_num) continue;
			std::uint8_t* at = a_data + static_cast<std::ptrdiff_t>(s.index) * a_size + a_keyOffset;
			std::uint64_t now = 0;
			std::memcpy(&now, at, sizeof(now));
			if (now != 0) continue;
			std::memcpy(at, &s.name, sizeof(s.name));
			++back;
		}
		return back;
	}
}
