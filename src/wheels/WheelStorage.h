#pragma once

#include <cctype>
#include <array>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <vector>
#include <limits>
#include <utility>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace wheelstorage
{
	inline constexpr std::string_view kSignature = "# ImprovedWheelsPlus assignments v1";
	inline constexpr std::string_view kSpellNamesSignature = "# ImprovedWheelsPlus spell names v1";   // <character>.spells, same folder
	inline constexpr std::string_view kCharacterPrefix = "# Character: ";

	// the file stem of a character: what a file name may keep of the identity (the header carries the exact identity)
	inline std::string SafeName(std::string_view a_name)
	{
		std::string safe;
		for (const char c : a_name) {
			safe += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
		}
		return safe;
	}

	inline bool ReadHeader(std::istream& a_in, std::string_view a_character, std::string_view a_signature = kSignature)
	{
		std::string signature, character;
		return std::getline(a_in, signature) && std::getline(a_in, character) &&
			signature == a_signature && character == std::string(kCharacterPrefix) + std::string(a_character);
	}

	inline void WriteHeader(std::ostream& a_out, std::string_view a_character, std::string_view a_signature = kSignature)
	{
		a_out << a_signature << '\n' << kCharacterPrefix << a_character << '\n';
	}

	struct Slot
	{
		std::vector<std::uint32_t> entries;
		int active = -1;
		bool operator==(const Slot&) const = default;
	};
	using Model = std::array<std::array<Slot, 8>, 4>;
	inline constexpr std::array<std::string_view, 4> kWheelNames{ "Equipment", "Magic", "Ammo", "Favourite" };

	// The structure a slot needs to be drawn and cycled, whatever the character owns: no empty form ID, and an active entry that is
	// inside the list (or -1). It never asks about ownership: an entry this save does not carry or know may be another save's of
	// the same character, and erasing it would lose that save's assignment. True when the slot had to be repaired (then, and only
	// then, the file is written again).
	inline bool NormalizeSlot(Slot& a_slot)
	{
		const int n = static_cast<int>(a_slot.entries.size());
		const int active = a_slot.active >= 0 && a_slot.active < n ? a_slot.active : -1;
		std::vector<std::uint32_t> kept;
		kept.reserve(a_slot.entries.size());
		int keptActive = -1;
		for (int i = 0; i < n; ++i) {
			if (!a_slot.entries[static_cast<std::size_t>(i)]) continue;
			if (i == active) keptActive = static_cast<int>(kept.size());
			kept.push_back(a_slot.entries[static_cast<std::size_t>(i)]);
		}
		const bool changed = static_cast<int>(kept.size()) != n || keptActive != a_slot.active;
		if (changed) {
			a_slot.entries = std::move(kept);
			a_slot.active = keptActive;
		}
		return changed;
	}

	inline bool ParseUnsigned(std::string_view text, std::uint32_t& value, int base)
	{
		if (base == 16 && (text.starts_with("0x") || text.starts_with("0X"))) text.remove_prefix(2);
		if (text.empty()) return false;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
		return error == std::errc{} && end == text.data() + text.size();
	}

	// Commit only a completely valid file. Never let a partially read file replace live assignments.
	inline bool ReadAssignments(std::istream& in, std::string_view character, Model& result)
	{
		if (!ReadHeader(in, character)) return false;
		Model parsed{};
		std::array<std::array<bool, 8>, 4> seen{};
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::istringstream row(line);
			std::string wheel, number, active, id;
			if (!(row >> wheel >> number >> active) || !active.starts_with("active=")) return false;
			std::size_t w = 0;
			while (w < kWheelNames.size() && wheel != kWheelNames[w]) ++w;
			std::uint32_t slotNo = 0, selected = 0;
			if (w == kWheelNames.size() || !ParseUnsigned(number, slotNo, 10) || slotNo < 1 || slotNo > 8 ||
				!ParseUnsigned(std::string_view(active).substr(7), selected, 10) || seen[w][slotNo - 1]) return false;
			seen[w][slotNo - 1] = true;
			auto& slot = parsed[w][slotNo - 1];
			while (row >> id) {
				std::uint32_t form = 0;
				if (!ParseUnsigned(id, form, 16) || !form) return false;
				slot.entries.push_back(form);
			}
			if (selected > slot.entries.size() || selected > static_cast<std::uint32_t>((std::numeric_limits<int>::max)())) return false;
			slot.active = static_cast<int>(selected) - 1;
		}
		if (in.bad() || !in.eof()) return false;
		result = std::move(parsed);
		return true;
	}

	inline void WriteAssignments(std::ostream& out, std::string_view character, const Model& model)
	{
		WriteHeader(out, character);
		out << "# <wheel> <slot 1-8> active=<entry, 0 = none> <formID>...\n";
		for (std::size_t w = 0; w < model.size(); ++w) {
			for (std::size_t s = 0; s < model[w].size(); ++s) {
				const auto& slot = model[w][s];
				if (slot.entries.empty()) continue;
				out << kWheelNames[w] << ' ' << (s + 1) << " active=" << (slot.active + 1);
				for (auto id : slot.entries) {
					char hex[8];
					const auto [end, ec] = std::to_chars(hex, hex + sizeof(hex), id, 16);
					(void)ec;
					for (auto* c = hex; c != end; ++c) if (*c >= 'a' && *c <= 'f') *c = static_cast<char>(*c - 'a' + 'A');
					out << " 0x" << std::string(8 - static_cast<std::size_t>(end - hex), '0') << std::string(hex, end);
				}
				out << '\n';
			}
		}
	}

}
