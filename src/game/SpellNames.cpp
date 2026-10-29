#include "game/SpellNames.h"

#include "core/AtomicFile.h"
#include "core/Settings.h"
#include "game/Inventory.h"
#include "wheels/WheelStorage.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace spellnames
{
	namespace
	{
		std::unordered_map<std::uint32_t, std::string> g_displayNames;
		std::string                                    g_namesFor;                // the character g_displayNames belongs to ("" = none loaded yet)
		bool                                           g_namesDirty = false;
		bool                                           g_namesRejected = false;   // a file that is not ours stays untouched and unused
		ULONGLONG                                      g_namesCheckedAt = 0;

		std::filesystem::path NamesFile(const std::string& a_character)
		{
			return settings::PluginFolder() / L"ImprovedWheelsPlus" / L"Assignments" / (wheelstorage::SafeName(a_character) + ".spells");
		}

		// the names of the character now playing are in g_displayNames (false = no player yet, or a file that is not ours)
		bool LoadNames()
		{
			if (!RE::PlayerCharacter::GetSingleton()) return false;
			const ULONGLONG now = GetTickCount64();
			if (!g_namesFor.empty() && now - g_namesCheckedAt < 500) return !g_namesRejected;
			g_namesCheckedAt = now;
			const std::string who = inventory::PlayerName();
			if (who == g_namesFor) return !g_namesRejected;
			g_namesFor = who;
			g_displayNames.clear();
			g_namesRejected = false;
			g_namesDirty = false;
			const auto    path = NamesFile(who);
			std::ifstream f(path);
			if (!f) {
				if (std::filesystem::exists(path)) {
					g_namesRejected = true;
					logger::error("rows: spell names file cannot be read; leaving it untouched: {}", path.string());
				}
				return !g_namesRejected;   // no file yet: nothing learned for this character
			}
			if (!wheelstorage::ReadHeader(f, who, wheelstorage::kSpellNamesSignature)) {
				g_namesRejected = true;
				logger::error("rows: ignoring unrecognized format or character identity in {}; file left untouched", path.string());
				return false;
			}
			std::string line;
			while (std::getline(f, line)) {
				const auto tab = line.find('\t');
				if (tab == std::string::npos || tab < 3) continue;
				try {
					g_displayNames[static_cast<std::uint32_t>(std::stoul(line.substr(0, tab), nullptr, 16))] = line.substr(tab + 1);
				} catch (...) {}
			}
			return true;
		}
	}

	std::string Find(std::uint32_t a_formID)
	{
		if (!LoadNames()) return {};
		const auto it = g_displayNames.find(a_formID);
		return it == g_displayNames.end() ? std::string() : it->second;
	}

	void Remember(std::uint32_t a_id, const std::string& a_name)
	{
		if (a_name.empty() || !LoadNames()) return;
		auto& slot = g_displayNames[a_id];
		if (slot == a_name && !g_namesDirty) return;
		slot = a_name;
		g_namesDirty = true;
		const auto      path = NamesFile(g_namesFor);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (std::filesystem::exists(path)) {   // never overwrite a file that is not ours, even if it appeared after the load
			std::ifstream existing(path);
			if (!wheelstorage::ReadHeader(existing, g_namesFor, wheelstorage::kSpellNamesSignature)) {
				g_namesRejected = true;
				logger::error("rows: refusing to overwrite unrecognized spell names file {}", path.string());
				return;
			}
		}
		std::ostringstream data;
		wheelstorage::WriteHeader(data, g_namesFor, wheelstorage::kSpellNamesSignature);
		for (const auto& [id, name] : g_displayNames) data << std::format("{:08X}", id) << '\t' << name << '\n';
		if (!data || !atomicfile::Write(path, data.str())) {
			logger::error("rows: atomic spell-name save failed; previous file preserved: {}", path.string());
		} else {
			g_namesDirty = false;
		}
	}

	void Reset()
	{
		g_namesFor.clear();
		g_displayNames.clear();
		g_namesCheckedAt = 0;
	}
}
