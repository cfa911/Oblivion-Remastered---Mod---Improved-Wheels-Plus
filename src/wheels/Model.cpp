// The wheel model and its file (Assignments/<character>.iwp): what may go on which wheel, saving and loading, keeping the wheels in step with the game's keys and the character's items.
#include "wheels/WheelsInternal.h"

#include "core/AtomicFile.h"
#include "core/Settings.h"
#include "engine/TesThread.h"
#include "game/Rows.h"

#include <fstream>
#include <sstream>

namespace wheels::detail
{
	const char* WheelName(int a_wheel) { return a_wheel >= 0 && a_wheel < kWheels ? kWheelNames[a_wheel] : "?"; }

	bool IsAmmo(std::uint32_t a_id)
	{
		auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
		return form && form->GetFormType() == RE::FormType::Ammo;
	}

	bool Wearable(std::uint32_t a_id, bool* a_shield)
	{
		auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
		if (!form) return false;
		const RE::TESBipedModelForm* biped = nullptr;
		if (form->GetFormType() == RE::FormType::Armor) biped = form->As<RE::TESObjectARMO>();
		else if (form->GetFormType() == RE::FormType::Clothing) biped = form->As<RE::TESObjectCLOT>();
		if (!biped) return false;
		if (a_shield) *a_shield = (biped->bipedModelData.bipedObjectSlots & kShieldSlot) != 0;
		return true;
	}

	bool AllowedOnEquipment(std::uint32_t a_id)
	{
		if (settings::Get().armourOnWheel) return true;   // bArmourOnWheel: armour and clothing too
		bool shield = false;
		return !Wearable(a_id, &shield) || shield;
	}

	std::string NameOf(int a_wheel, std::uint32_t a_id)
	{
		return a_wheel == kMagic ? rows::SpellName(a_id) : inventory::NameOf(a_id);
	}

	std::filesystem::path FileFor(const std::string& a_name)
	{
		return settings::PluginFolder() / L"ImprovedWheelsPlus" / L"Assignments" / (wheelstorage::SafeName(a_name) + ".iwp");
	}

	void Save()
	{
		g_generation.fetch_add(1, std::memory_order_relaxed);
		if (g_loadedFor.empty() || g_storageRejected) {
			return;
		}
		const auto path = FileFor(g_loadedFor);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		// Do not overwrite a foreign file even if it appeared after Load.
		if (std::filesystem::exists(path)) {
			std::ifstream existing(path);
			if (!wheelstorage::ReadHeader(existing, g_loadedFor)) {
				g_storageRejected = true;
				logger::error("wheels: refusing to overwrite unrecognized assignment file {}", path.string());
				return;
			}
		}
		std::ostringstream data;
		wheelstorage::WriteAssignments(data, g_loadedFor, g_wheels);
		if (!data || !atomicfile::Write(path, data.str())) {
			logger::error("wheels: atomic assignment save failed; previous file preserved: {}", path.string());
		}
	}

	void Load(const std::string& a_name)
	{
		ResetCarriedHistory();
		g_wheels = {};
		g_emptySetup.Reset();
		g_initializeEmpty = false;
		g_storageRejected = false;
		g_loadedFor = a_name;
		g_generation.fetch_add(1, std::memory_order_relaxed);
		const auto path = FileFor(a_name);
		std::ifstream f(path);
		if (!f) {
			if (std::filesystem::exists(path)) {
				g_storageRejected = true;
				logger::error("wheels: assignment file cannot be read; leaving it untouched: {}", path.string());
			} else {
				g_initializeEmpty = true;
				logger::info("wheels: no ImprovedWheelsPlus assignments for {} yet ({})", a_name, path.filename().string());
			}
			return;
		}
		if (!wheelstorage::ReadAssignments(f, a_name, g_wheels)) {
			g_storageRejected = true;
			logger::error("wheels: invalid assignment data or identity in {}; file left untouched", path.string());
			return;
		}
		logger::info("wheels: assignments loaded for {}", a_name);
	}

	int IndexOf(const Slot& a_slot, std::uint32_t a_id)
	{
		const auto it = std::find(a_slot.entries.begin(), a_slot.entries.end(), a_id);
		return it == a_slot.entries.end() ? -1 : static_cast<int>(it - a_slot.entries.begin());
	}

	int Cap() { return std::max(1, settings::Get().entriesPerSlot); }

	// a poison (an alchemy item the game types as poison): the game asks whether to apply it to the weapon
	bool Poison(std::uint32_t a_id)
	{
		auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
		auto* alch = form && form->GetFormType() == RE::FormType::AlchemyItem ? form->As<RE::AlchemyItem>() : nullptr;
		if (!alch) return false;
		auto* item = static_cast<RE::MagicItem*>(alch);
		if (item->GetSpellType() == RE::MagicSystem::SpellType::kPoison) return true;
		// a poison the player made reads as a potion: every one of its effects is hostile (Oblivion's own rule for poisons)
		int effects = 0;
		for ([[maybe_unused]] auto* e : item->effectList) ++effects;
		return effects > 0 && item->hostileCount >= effects;
	}

	void Reconcile()
	{
		auto keys = inventory::Keys();
		bool changed = false, cleared = false;
		std::vector<int> advanced;
		for (int s = 0; s < inventory::kSlots; ++s) {
			const auto id = keys[s];
			Slot& slot = g_wheels[kEquip][s];
			if (!id || IndexOf(slot, id) >= 0) {
				continue;
			}
			// A saved game's key is not an assignment request. Clear it instead of importing
			// an item (or evicting a stored entry when the slot is full). Pending explicit
			// menu assignments bypass Reconcile until Settle has recorded the new entry.
			inventory::ClearKey(id);
			cleared = true;
			logger::info("wheels: game hotkey {} cleared - stored wheel assignments only (no automatic import)", s + 1);
		}
		if (cleared) {
			keys = inventory::Keys();
		}
		// a keyed item leaves every OTHER slot's entries (an item is on one slot)
		for (int s = 0; s < inventory::kSlots; ++s) {
			if (!keys[s]) {
				continue;
			}
			for (int t = 0; t < inventory::kSlots; ++t) {
				Slot& other = g_wheels[kEquip][t];
				for (int idx = t == s ? -1 : IndexOf(other, keys[s]); idx >= 0; idx = IndexOf(other, keys[s])) {
					other.entries.erase(other.entries.begin() + idx);
					if (other.active == idx) {
						other.active = -1;
					} else if (other.active > idx) {
						--other.active;
					}
					changed = true;
					logger::info("wheels: {} left Equipment slot {}'s entries - it is on slot {}'s key, and an item is on one slot",
						inventory::NameOf(keys[s]), t + 1, s + 1);
				}
			}
		}
		// each slot's active entry on its key
		for (int s = 0; s < inventory::kSlots; ++s) {
			if (s == g_holdKey) continue;   // taken off on purpose, back when the radial has closed
			Slot& slot = g_wheels[kEquip][s];
			const int keyed = keys[s] ? IndexOf(slot, keys[s]) : -1;
			const int want = slot.active >= 0 && slot.active < static_cast<int>(slot.entries.size()) ? slot.active : -1;
			if (want >= 0 && want != keyed && Unreachable(s, slot.entries[want], keys).empty() && inventory::SetKey(slot.entries[want], s)) {
				logger::info("wheels: Equipment slot {}'s choice {} put back on the game's key {} (it held {})", s + 1, inventory::NameOf(slot.entries[want]),
					s + 1, keys[s] ? inventory::NameOf(keys[s]) : std::string("nothing"));
				keys[s] = slot.entries[want];
				continue;
			}
			// the active entry is gone from the bag (the last potion of a stack was drunk, the item sold or dropped): the slot moves on
			// to its next entry that can be used, instead of showing nothing
			if (want >= 0 && keyed < 0 && !Unreachable(s, slot.entries[want], keys).empty()) {
				if (const int next = NextEquipment(s, want, +1); next >= 0 && next != want && inventory::SetKey(slot.entries[next], s)) {
					logger::info("wheels: Equipment slot {}: {} is gone - {} is the slot's entry now ({} of {})", s + 1, inventory::NameOf(slot.entries[want]),
						inventory::NameOf(slot.entries[next]), next + 1, slot.entries.size());
					slot.active = next;
					keys[s] = slot.entries[next];
					changed = true;
					advanced.push_back(s);
					continue;
				}
			}
			// a slot that holds entries but shows none (the entry it showed was not carried when the save loaded, so it was dropped and nothing took its
			// place): its first usable entry becomes the shown one - the wheel read -/5 after a session in which a stack was used up
			if (want < 0 && keyed < 0 && !slot.entries.empty()) {
				if (const int next = NextEquipment(s, -1, +1); next >= 0 && inventory::SetKey(slot.entries[next], s)) {
					logger::info("wheels: Equipment slot {} showed nothing - {} is the slot's entry now ({} of {})", s + 1, inventory::NameOf(slot.entries[next]), next + 1,
						slot.entries.size());
					slot.active = next;
					keys[s] = slot.entries[next];
					changed = true;
					advanced.push_back(s);
					continue;
				}
			}
			slot.active = keyed;
		}
		if (changed) {
			Save();
			for (const int s : advanced) PreloadIcon(s);
		}
	}

	// What the wheels hold that THIS character does not have now (a save loaded after another, or a spell or item lost since) is
	// KEPT: a stored entry is the character's, not the save's, and a later save of the same character may carry it - erasing it here
	// wiped the .iwp assignments of every later save (the audit of 2026-10-23). Nothing ever tries to use or draw a form the save
	// does not have because the display and every use already filter by ownership (KnownMagicEntry, Unreachable, IsAmmo and
	// inventory::Has in AmmoSlots / SlotCounts). Run once, a moment after a character loads, when the inventory and the spell list
	// exist. It only repairs the structure (an empty form ID, an active entry outside the list) and writes the file again only for
	// that; an unowned entry is logged once and left alone.
	void Sanitize()
	{
		int repaired = 0, unowned = 0;
		for (int w : { static_cast<int>(kEquip), static_cast<int>(kMagic), static_cast<int>(kAmmo) }) {
			for (int s = 0; s < inventory::kSlots; ++s) {
				Slot& slot = g_wheels[w][s];
				if (slot.entries.empty()) continue;
				if (wheelstorage::NormalizeSlot(slot)) {
					++repaired;
					logger::info("wheels: {} slot {} had an empty form ID or an active entry outside its list - repaired", WheelName(w), s + 1);
				}
				for (const auto id : slot.entries) {
					const bool owned = RE::TESForm::LookupByID(id) != nullptr &&
						(w == kMagic ? rows::PlayerHasSpell(id) : inventory::Has(id) && (w != kAmmo || IsAmmo(id)));
					if (!owned) {
						++unowned;
						logger::info("wheels: {} 0x{:08X} on {} slot {} is not owned, kept (this character may have it in another save)",
							w == kMagic ? "spell" : "item", id, WheelName(w), s + 1);
					}
				}
			}
		}
		if (unowned) {
			logger::info("wheels: {} stored entr{} not owned in this save, kept (shown only while owned)", unowned, unowned == 1 ? "y is" : "ies are");
		}
		if (repaired) {
			Save();
		}
	}

	bool EnsureLoaded()
	{
		if (!ObserveContext()) return false;
		if (!RE::PlayerCharacter::GetSingleton()) {
			return false;
		}
		const std::string name = inventory::PlayerName();
		if (name != g_loadedFor) {
			Load(name);
			g_sanitized = false;
			g_sanitizeAt = GetTickCount64() + 3000;   // let the character's inventory and spell list finish loading
		}
		if (g_storageRejected) return false;
		if (g_initializeEmpty) {
			// Keep saved setup absent and wheel actions blocked until native cleanup really completes.
			// Never strip ExtraQuickKey here: the native remover needs it for its bookkeeping.
			if (GetTickCount64() < g_sanitizeAt || !inventory::Ready() || AssignPending()) return false;
			if (!g_emptySetup.Complete()) {
				if (testhread::Installed()) {
					if (const auto ticket = g_emptySetup.Queue(GetTickCount64())) {
						testhread::Post([ticket] {
							g_emptySetup.Run(ticket, [] { return inventory::ClearAllNativeKeys(); });
						});
					}
				}
				return false;
			}
			g_initializeEmpty = false;
			Save();
			quickkeys::RequestRedraw();
			logger::warn("wheels: empty setup ready - native item and spell hotkeys verified empty before assignments enabled");
		}
		if (!g_sanitized && GetTickCount64() >= g_sanitizeAt && inventory::Ready() && rows::PlayerSpellCount() > 0 &&
			!AssignPending()) {
			g_sanitized = true;
			Sanitize();
		}
		// not while the game's own assign is watched: Settle reads what arrived on the key and decides ADDED / REMOVED /
		// moved itself - reconciling first (the favourites column and the slot counters ask every 100 ms) took the
		// arrival as already "in the slot"
		if (!AssignPending()) {
			RemoveLeavers();
			RemoveGoneInMenus();
			Reconcile();
		}
		MigrateAmmo();
		return true;
	}
}
