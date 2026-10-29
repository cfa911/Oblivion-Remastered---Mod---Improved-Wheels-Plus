#include "wheels/Wheels.h"
#include "wheels/WheelStorage.h"
#include "core/AtomicFile.h"
#include "engine/SessionContext.h"
#include "wheels/KnownEntry.h"
#include "wheels/EmptySetup.h"
#include "wheels/CarriedHistory.h"

#include "ammo/Ammo.h"
#include "game/Inventory.h"
#include "game/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "game/QuickKeys.h"
#include "game/Rows.h"
#include "core/Settings.h"
#include "engine/TesThread.h"
#include "engine/Ui.h"

#include <fstream>
#include <set>
#include <unordered_map>
#include <sstream>

namespace wheels
{
	namespace
	{
		constexpr int kEquip = 0;
		constexpr int kMagic = 1;
		constexpr int kAmmo = 2;   // the bow's ammo wheel: one arrow kind a slot, entirely ours
		constexpr int kFav = 3;    // favourites that go on no wheel: armour and clothing (the owner, 2026-09-30: "It should be fine
		                           // to favorite armor, just not added to the wheel") - all in slot 1, no cap
		constexpr int kWheels = 4;
		constexpr const char* kWheelNames[kWheels] = { "Equipment", "Magic", "Ammo", "Favourite" };

		std::atomic<int> g_active{ static_cast<int>(Wheel::kEquipment) };

		using Slot = wheelstorage::Slot;
		wheelstorage::Model g_wheels;
		std::string g_loadedFor;   // the character g_wheels belongs to
		bool g_storageRejected = false;   // never load or overwrite a file without our format and character identity
		emptysetup::Gate g_emptySetup;
		bool g_initializeEmpty = false;   // no stored wheel setup: clear native item keys once inventory is ready
		std::unordered_set<std::uint32_t> g_carriedSeen;
		std::uint64_t g_goneCheckAt = 0;
		void ResetCarriedHistory() { carriedhistory::Reset(g_carriedSeen, g_goneCheckAt); }

		int MigrateAmmo();

		// what the wheel's pictures are showing now
		enum class Shown { kGame, kMagic };
		Shown g_shown = Shown::kGame;
		ULONGLONG g_keepUntil = 0;   // until then a radial that opens keeps the wheel that is active (a wheel's own button or a keyboard key chose it)

		const char* WheelName(int a_wheel) { return a_wheel >= 0 && a_wheel < kWheels ? kWheelNames[a_wheel] : "?"; }

		bool IsAmmo(std::uint32_t a_id)
		{
			auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
			return form && form->GetFormType() == RE::FormType::Ammo;
		}

		// Armour and clothing never go on the wheel - shields do (the owner, 2026-09-30: "I don't want armor that's favorited
		// to appear on the wheel at all. I just want the shields to be appearing on it"). Oblivion's body slot 13 is the shield.
		constexpr std::uint16_t kShieldSlot = 1u << 13;

		bool Wearable(std::uint32_t a_id, bool* a_shield = nullptr)
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

		// ---- persistence ----

		std::filesystem::path FileFor(const std::string& a_name)
		{
			return settings::PluginFolder() / L"ImprovedWheelsPlus" / L"Assignments" / (wheelstorage::SafeName(a_name) + ".iwp");
		}

		std::atomic<std::uint32_t> g_generation{ 1 };   // moves with every saved change (the favourites column redraws)

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

		bool AssignPending();   // the game's own assign is being watched (Settle reads the keys' change itself)


		std::string Unreachable(int a_slot, std::uint32_t a_id, const inventory::KeyMap& a_keys);

		// Stored wheel assignments are authoritative. Game hotkeys are only the backing keys for
		// explicit wheel choices; loading a save must never import new entries into the wheel.
		int           g_holdKey = -1;      // the quick key taken off its item while the radial closes on another wheel's slot
		std::uint32_t g_holdItem = 0;

		int  NextEquipment(int a_slot, int a_from, int a_dir, std::string* a_skipped = nullptr);
		void RemoveLeavers();
		void RemoveGoneInMenus();
		bool Consumable(std::uint32_t a_id);

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
		void PatchEquipment(int a_slot);
		void Refresh();

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
				for (const int s : advanced) PatchEquipment(s);
				if (!advanced.empty()) Refresh();
			}
		}

		// What the wheels hold that THIS character does not have now (a save loaded after another, or a spell or item lost since) is
		// KEPT: a stored entry is the character's, not the save's, and a later save of the same character may carry it - erasing it here
		// wiped the .iwp assignments of every later save (the audit of 2026-10-23). Nothing ever tries to use or draw a form the save
		// does not have because the display and every use already filter by ownership (KnownMagicEntry, Unreachable, IsAmmo and
		// inventory::Has in AmmoSlots / SlotCounts). Run once, a moment after a character loads, when the inventory and the spell list
		// exist. It only repairs the structure (an empty form ID, an active entry outside the list) and writes the file again only for
		// that; an unowned entry is logged once and left alone.
		ULONGLONG g_sanitizeAt = 0;
		bool      g_sanitized = true;

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

		bool ObserveContext();

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

		// ---- pictures ----

		// One view model feeds the HUD radial and both menu panels (a single live instance, read 2026-09-29), and the
		// game pushes its own eight pictures into it just AFTER one of them opens. So:
		//   Equipment - the game's pictures already are the Equipment wheel (its keys); only a slot we change is patched.
		//   Magic     - while it shows, every controller read puts the Magic wheel's pictures back if the game replaced
		//               them, keeping what the game pushed so it goes back when the Magic wheel leaves the screen.

		// Resolve the currently known entry each time, including when an earlier save loads
		// without an observable context transition. Do not erase stored entries on a transient list.
		int KnownMagicEntry(const Slot& a_slot, int a_from, int a_dir, bool a_includeFrom)
		{
			return knownentry::Find(a_slot.entries, a_from, a_dir, a_includeFrom, rows::PlayerHasSpell);
		}

		quickkeys::Icons MagicIcons()
		{
			quickkeys::Icons icons{};
			for (int k = 0; k < inventory::kSlots; ++k) {
				const Slot& slot = g_wheels[kMagic][k];
				if (const int known = KnownMagicEntry(slot, slot.active, +1, true); known >= 0) {
					icons[k] = rows::SpellIcon(slot.entries[known]);
				}
			}
			return icons;
		}

		// Drawing is owned by MagicWheel.cpp. Keep the existing notification wrappers;
		// the old widget redraw branch was permanently disabled.
		void AssertEquipment() {}

		// The Magic wheel is its own widget now (MagicWheel.cpp): nothing is written into the game's view model - every
		// slot the old SetIcons redrew was the widget's own "slot changed" and ticked (the owner, 2026-09-30: "only on the
		// magic wheel do I hear a bunch of ticking noises as soon as it appears"), and the magic menu still showed the
		// inventory wheel's items ("when I go to inventory and magic, I still see all the same items").
		void WriteMagic(bool /*a_force*/) {}

		void AssertMagic()
		{
			if (g_shown != Shown::kMagic) {
				AssertEquipment();
				return;
			}
			WriteMagic(false);
		}

		void ShowMagic(bool a_on)
		{
			if ((g_shown == Shown::kMagic) == a_on) {
				return;
			}
			if (a_on) {
				g_shown = Shown::kMagic;
				int drawn = 0, filled = 0;
				for (const auto* i : MagicIcons()) {
					drawn += i ? 1 : 0;
				}
				for (const Slot& slot : g_wheels[kMagic]) {
					filled += slot.active >= 0 ? 1 : 0;
				}
				logger::info("wheels: the Magic wheel is up - {} of 8 slots hold a spell, {} with its picture known (its own widget draws them)",
					filled, drawn);
			} else {
				g_shown = Shown::kGame;
				logger::info("wheels: the Magic wheel is down - the game's own wheel shows");
			}
		}

		// an Equipment slot's key changed under the game: its picture follows (when a row has shown that item's icon)
		void PatchEquipment(int a_slot)
		{
			if (g_shown == Shown::kMagic || a_slot < 0 || a_slot >= inventory::kSlots) {
				return;
			}
			const auto id = inventory::Keys()[a_slot];
			auto* icon = id ? rows::ItemIcon(id) : nullptr;
			if (id && !icon) {
				logger::info("wheels: slot {}'s picture not updated - no row has shown {} this session", a_slot + 1, inventory::NameOf(id));
				return;
			}
			AssertEquipment();
		}

		void Refresh()
		{
			AssertMagic();
		}

		// every Equipment slot whose key moved since a_before gets its picture
		void PatchChanged(const inventory::KeyMap& a_before)
		{
			const auto now = inventory::Keys();
			for (int k = 0; k < inventory::kSlots; ++k) {
				if (now[k] != a_before[k]) {
					PatchEquipment(k);
				}
			}
		}

		// ---- Equipment: moving the game's key ----

		// Why an Equipment entry cannot be stepped to (LT / RT, D-pad left / right): it is not carried, or it is on another
		// slot's game key. Empty = it can be.
		std::string Unreachable(int a_slot, std::uint32_t a_id, const inventory::KeyMap& a_keys)
		{
			if (!inventory::Has(a_id)) {
				return "not carried";
			}
			for (int k = 0; k < inventory::kSlots; ++k) {
				if (k != a_slot && a_keys[k] == a_id) {
					return std::format("on slot {}'s key", k + 1);
				}
			}
			return {};
		}

		// The entry of an Equipment slot to make active after a_from (dir +1/-1): carried, and not active in another slot.
		// Every entry passed over is named with its reason in *a_skipped.
		int NextEquipment(int a_slot, int a_from, int a_dir, std::string* a_skipped)
		{
			const Slot& slot = g_wheels[kEquip][a_slot];
			const int n = static_cast<int>(slot.entries.size());
			const auto keys = inventory::Keys();
			for (int step = 1; step <= n; ++step) {
				const int i = ((a_from + a_dir * step) % n + n) % n;
				const auto id = slot.entries[i];
				const std::string why = Unreachable(a_slot, id, keys);
				if (why.empty()) {
					return i;
				}
				if (a_skipped && i != slot.active) {
					*a_skipped += std::format("{}{} skipped: {}", a_skipped->empty() ? "" : "; ", inventory::NameOf(id), why);
				}
			}
			return -1;
		}

		// Takes entry a_index out of a slot; on the Equipment wheel the key goes to the entry that takes its place.
		void RemoveAt(int a_wheel, int a_slot, int a_index, const std::string& a_why)
		{
			Slot& slot = g_wheels[a_wheel][a_slot];
			if (a_index < 0 || a_index >= static_cast<int>(slot.entries.size())) {
				return;
			}
			const auto gone = slot.entries[a_index];
			const bool wasActive = a_index == slot.active;
			slot.entries.erase(slot.entries.begin() + a_index);
			if (slot.active > a_index) {
				--slot.active;
			}
			if (a_wheel != kEquip) {   // Magic and Ammo are data only
				if (wasActive) {
					slot.active = slot.entries.empty() ? -1 : std::min(a_index, static_cast<int>(slot.entries.size()) - 1);
				}
			} else {
				inventory::ClearKey(gone);
				if (wasActive || slot.active < 0) {
					slot.active = -1;
					if (!slot.entries.empty()) {
						const int next = NextEquipment(a_slot, a_index - 1, +1);
						if (next >= 0 && inventory::SetKey(slot.entries[next], a_slot)) {
							slot.active = next;
						}
					}
				} else if (slot.active >= 0 && slot.active < static_cast<int>(slot.entries.size())) {
					inventory::SetKey(slot.entries[slot.active], a_slot);
				}
			}
			logger::info("wheels: {} - {} REMOVED from {} slot {}; slot now {} entr{} (active: {})", a_why, NameOf(a_wheel, gone),
				WheelName(a_wheel), a_slot + 1, slot.entries.size(), slot.entries.size() == 1 ? "y" : "ies",
				slot.active >= 0 && slot.active < static_cast<int>(slot.entries.size()) ? NameOf(a_wheel, slot.entries[slot.active]) : "none");
		}

		// Favourites the player dropped or sold (bBlockFavouriteDropSell=0): once none is carried they leave every wheel, the Equipment
		// wheel's key going to the slot's next entry.
		std::unordered_set<std::uint32_t> g_leaving;

		void RemoveLeavers()
		{
			if (g_leaving.empty()) {
				return;
			}
			const auto before = inventory::Keys();
			bool       any = false;
			for (auto it = g_leaving.begin(); it != g_leaving.end();) {
				if (inventory::Has(*it)) {
					++it;
					continue;
				}
				const auto id = *it;
				for (const int w : { static_cast<int>(kEquip), static_cast<int>(kAmmo), static_cast<int>(kFav) }) {
					for (int s = 0; s < inventory::kSlots; ++s) {
						for (int idx = IndexOf(g_wheels[w][s], id); idx >= 0; idx = IndexOf(g_wheels[w][s], id)) {
							RemoveAt(w, s, idx, "dropped or sold");
							any = true;
						}
					}
				}
				it = g_leaving.erase(it);
			}
			if (any) {
				Save();
				PatchChanged(before);
				Refresh();
			}
		}

		// An item that was carried and is gone while a menu is open was dropped, sold or stored - by the mouse or a real keyboard key as well, which NoteLeaving
		// (the pad's X and A) never sees. Equipment and ammo leave the wheels at once (nothing in a menu uses them up); a potion, poison or ingredient only in a
		// container or barter menu, because the Inventory menu can drink it. Only items seen carried count: what a loaded save does not have is Sanitize's.

		void RemoveGoneInMenus()
		{
			if (!inventory::Ready()) { ResetCarriedHistory(); return; }
			const ULONGLONG now = GetTickCount64();
			if (now < g_goneCheckAt) return;
			g_goneCheckAt = now + 250;
			const bool inMenu = menus::AnyOpen() && !quickkeys::RadialOpen();
			const bool barterOrContainer = inMenu && menus::Active() == menus::Menu::kNone;
			std::vector<std::uint32_t> gone;
			for (const int w : { static_cast<int>(kEquip), static_cast<int>(kAmmo), static_cast<int>(kFav) }) {
				for (const Slot& slot : g_wheels[w]) {
					for (const auto id : slot.entries) {
						if (inventory::Has(id)) {
							g_carriedSeen.insert(id);
						} else if (inMenu && g_carriedSeen.count(id) && (!Consumable(id) || barterOrContainer) &&
								   std::find(gone.begin(), gone.end(), id) == gone.end()) {
							gone.push_back(id);
						}
					}
				}
			}
			if (gone.empty()) return;
			const auto before = inventory::Keys();
			for (const auto id : gone) {
				for (const int w : { static_cast<int>(kEquip), static_cast<int>(kAmmo), static_cast<int>(kFav) }) {
					for (int s = 0; s < inventory::kSlots; ++s) {
						for (int idx = IndexOf(g_wheels[w][s], id); idx >= 0; idx = IndexOf(g_wheels[w][s], id)) {
							RemoveAt(w, s, idx, "gone while a menu was open (dropped, sold or stored)");
						}
					}
				}
				g_carriedSeen.erase(id);
				g_leaving.erase(id);
			}
			Save();
			PatchChanged(before);
			Refresh();
		}

		// Taking an item off the wheel the normal way unfavourites it (the owner, 2026-09-29: "I just removed the item from
		// the wheel the normal way. Which should be considered going forward."): every OTHER slot of that wheel lets it go
		// too, so a copy hidden among a slot's inactive entries cannot keep it a favourite (the Revealer of Iniquity stayed
		// undroppable as the first of Equipment slot 3's three entries after it was taken off slots 7 and 1).
		void RemoveFromOtherSlots(int a_wheel, int a_keepSlot, std::uint32_t a_id)
		{
			for (int s = 0; s < inventory::kSlots; ++s) {
				if (s == a_keepSlot) {
					continue;
				}
				for (int idx = IndexOf(g_wheels[a_wheel][s], a_id); idx >= 0; idx = IndexOf(g_wheels[a_wheel][s], a_id)) {
					RemoveAt(a_wheel, s, idx, std::format("taken off the wheel from slot {}", a_keepSlot + 1));
				}
			}
		}

		// Adds to a slot as its active entry (the Equipment wheel moves the game's key onto it).
		bool AddTo(int a_wheel, int a_slot, std::uint32_t a_id, const char* a_why)
		{
			Slot& slot = g_wheels[a_wheel][a_slot];
			if (static_cast<int>(slot.entries.size()) >= Cap()) {
				logger::info("wheels: {} slot {} is full ({} entries) - {} not added", WheelName(a_wheel), a_slot + 1, Cap(),
					NameOf(a_wheel, a_id));
				return false;
			}
			if (a_wheel == kEquip && !inventory::SetKey(a_id, a_slot)) {
				return false;
			}
			slot.entries.push_back(a_id);
			slot.active = static_cast<int>(slot.entries.size()) - 1;
			logger::info("wheels: {} - {} ADDED to {} slot {}; slot now {} entr{}, it is the active one", a_why, NameOf(a_wheel, a_id),
				WheelName(a_wheel), a_slot + 1, slot.entries.size(), slot.entries.size() == 1 ? "y" : "ies");
			return true;
		}

		// ---- a pending Equipment assign (the game handles the press between our reads) ----

		struct Pending
		{
			bool              on = false;
			int               slot = -1;
			std::uint32_t     parked = 0;       // the pointed slot's item, taken off its key before the game's assign
			inventory::KeyMap before{};
			int               reads = 0;
			int               readsSinceRelease = 0;
			bool              released = false;
		} g_pending;

		bool AssignPending() { return g_pending.on; }

		void Settle(int a_slot, std::uint32_t a_arrived)
		{
			const Pending p = g_pending;
			g_pending = {};
			inventory::KeyMap original = p.before;
			if (p.slot >= 0) {
				original[p.slot] = p.parked;
			}
			Slot& slot = g_wheels[kEquip][a_slot];
			const std::uint32_t previous = a_slot == p.slot ? p.parked : p.before[a_slot];   // what the slot held
			if (a_slot != p.slot && p.parked) {
				logger::info("wheels: the game assigned to slot {}, not the pointed slot {}", a_slot + 1, p.slot + 1);
				inventory::SetKey(p.parked, p.slot);   // the pointed slot keeps its item
			}
			// an item the game MOVED here from another slot leaves that slot
			for (int u = 0; u < inventory::kSlots; ++u) {
				if (u != a_slot && p.before[u] == a_arrived) {
					const int idx = IndexOf(g_wheels[kEquip][u], a_arrived);
					if (idx >= 0) {
						g_wheels[kEquip][u].active = idx;
						RemoveAt(kEquip, u, idx, std::format("moved to slot {}", a_slot + 1));
						inventory::SetKey(a_arrived, a_slot);   // RemoveAt cleared it
					}
				}
			}
			if (!AllowedOnEquipment(a_arrived)) {
				inventory::ClearKey(a_arrived);
				if (previous) {
					inventory::SetKey(previous, a_slot);
				}
				logger::info("wheels: the game assigned {} to slot {} - armour and clothing never go on the wheel (shields do); the slot keeps {}",
					inventory::NameOf(a_arrived), a_slot + 1, previous ? inventory::NameOf(previous) : "nothing");
				Save();
				PatchChanged(original);
				return;
			}
			const int idx = IndexOf(slot, a_arrived);
			if (a_arrived == previous) {
				// pressed again on the item the slot shows: out it goes, the next entry takes its place
				RemoveAt(kEquip, a_slot, idx >= 0 ? idx : slot.active, "assign pressed again on the same item");
				RemoveFromOtherSlots(kEquip, a_slot, a_arrived);
			} else if (idx >= 0) {
				// pressed on an entry the slot holds but is not showing: out it goes, the shown one stays
				slot.entries.erase(slot.entries.begin() + idx);
				inventory::ClearKey(a_arrived);
				slot.active = previous ? IndexOf(slot, previous) : -1;
				if (previous) {
					inventory::SetKey(previous, a_slot);
				}
				logger::info("wheels: assign on {} (already in slot {}) - REMOVED; {} stays active", inventory::NameOf(a_arrived), a_slot + 1,
					previous ? inventory::NameOf(previous) : "nothing");
				RemoveFromOtherSlots(kEquip, a_slot, a_arrived);
			} else if (static_cast<int>(slot.entries.size()) >= Cap()) {
				// full: the new favourite takes the place of the entry the slot shows - the one D-pad left / right stepped to
				// (the owner, 2026-09-30: "D-pad LEFT/RIGHT in the inventory steps through the slot's items to choose which one a
				// new favourite replaces"). The game's key is already on the new item; the one replaced leaves the wheel.
				const int at = slot.active >= 0 && slot.active < static_cast<int>(slot.entries.size()) ? slot.active : 0;
				const auto out = slot.entries[at];
				slot.entries[at] = a_arrived;
				slot.active = at;
				logger::info("wheels: Equipment slot {} is full ({} entries) - {} REPLACES {} (entry {})", a_slot + 1, Cap(), inventory::NameOf(a_arrived),
					inventory::NameOf(out), at + 1);
			} else {
				slot.entries.push_back(a_arrived);
				slot.active = static_cast<int>(slot.entries.size()) - 1;
				logger::info("wheels: {} ADDED to Equipment slot {} - now {} entr{}, it is the active one (the others stay in the slot)",
					inventory::NameOf(a_arrived), a_slot + 1, slot.entries.size(), slot.entries.size() == 1 ? "y" : "ies");
			}
			Save();
			PatchChanged(original);
			MigrateAmmo();   // arrows the game's own assign put on a key go to the Ammo wheel
		}

		void SettleNothing()
		{
			const Pending p = g_pending;
			g_pending = {};
			if (p.parked) {
				inventory::SetKey(p.parked, p.slot);
			}
			logger::info("wheels: the assign changed nothing (slot {}) - left as it was", p.slot + 1);
		}

		// An item's kind, for putting like with like (the owner, 2026-09-30: "similar types of items stack in the same slot
		// when they're favorited. So that great swords go to great swords, claymores to claymores, maces to maces"): a
		// weapon by its type (blade or blunt, one- or two-handed, staff, bow), armour and clothing by the body slot they
		// fill (the lowest slot bit), anything else by its form type. 0 = not known.
		std::uint32_t KindOf(std::uint32_t a_id)
		{
			auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
			if (!form) return 0;
			const auto type = form->GetFormType();
			if (type == RE::FormType::Weapon) {
				auto* weap = form->As<RE::TESObjectWEAP>();
				return weap ? 0x100u + static_cast<std::uint32_t>(weap->data.type) : 0;
			}
			const RE::TESBipedModelForm* biped = nullptr;
			if (type == RE::FormType::Armor) biped = form->As<RE::TESObjectARMO>();
			else if (type == RE::FormType::Clothing) biped = form->As<RE::TESObjectCLOT>();
			if (biped) {
				const std::uint32_t slots = biped->bipedModelData.bipedObjectSlots;
				std::uint32_t low = 0;
				while (low < 16 && !(slots & (1u << low))) ++low;
				return (type == RE::FormType::Armor ? 0x200u : 0x300u) + low;
			}
			return 0x400u + static_cast<std::uint32_t>(type);
		}

		// Y: on its wheel (the first slot with room) or off it (wherever it is)
		void ToggleFavourite(int a_wheel, std::uint32_t a_id)
		{
			// armour and clothing: a favourite, never on the wheel (shields go on it)
			if (a_wheel == kEquip && !AllowedOnEquipment(a_id)) {
				Slot& fav = g_wheels[kFav][0];
				if (const int idx = IndexOf(fav, a_id); idx >= 0) {
					fav.entries.erase(fav.entries.begin() + idx);
					logger::info("wheels: Favourite toggled off - {} (armour or clothing: a favourite, not on the wheel)", inventory::NameOf(a_id));
				} else {
					fav.entries.push_back(a_id);
					logger::info("wheels: Favourite - {} is a favourite now (armour or clothing: not put on the wheel; shields go on it)",
						inventory::NameOf(a_id));
				}
				fav.active = fav.entries.empty() ? -1 : 0;
				Save();
				return;
			}
			const auto before = inventory::Keys();
			for (int s = 0; s < inventory::kSlots; ++s) {
				const int idx = IndexOf(g_wheels[a_wheel][s], a_id);
				if (idx >= 0) {
					RemoveAt(a_wheel, s, idx, "Favourite toggled off");
					RemoveFromOtherSlots(a_wheel, s, a_id);   // off the whole wheel, not only the first slot holding it
					Save();
					PatchChanged(before);
					Refresh();
					return;
				}
			}

			// the Equipment wheel: like with like first - a slot already holding this kind of item, with room
			if (a_wheel == kEquip) {
				if (const auto kind = KindOf(a_id)) {
					for (int s = 0; s < inventory::kSlots; ++s) {
						const Slot& slot = g_wheels[a_wheel][s];
						if (slot.entries.empty() || static_cast<int>(slot.entries.size()) >= Cap()) continue;
						const bool same = std::any_of(slot.entries.begin(), slot.entries.end(), [&](std::uint32_t e) { return KindOf(e) == kind; });
						if (same && AddTo(a_wheel, s, a_id, "Favourite (with its kind)")) {
							Save();
							PatchChanged(before);
							Refresh();
							return;
						}
					}
				}
			}
			// then an empty slot, then any slot with room (the Ammo wheel: one arrow kind a slot, empty slots only)
			for (int pass = 0; pass < (a_wheel == kAmmo ? 1 : 2); ++pass) {
				for (int s = 0; s < inventory::kSlots; ++s) {
					const Slot& slot = g_wheels[a_wheel][s];
					const bool fits = pass == 0 ? slot.entries.empty() : static_cast<int>(slot.entries.size()) < Cap();
					if (fits && AddTo(a_wheel, s, a_id, "Favourite")) {
						Save();
						PatchChanged(before);
						Refresh();
						return;
					}
				}
			}
			logger::info("wheels: Favourite - every {} slot is full", WheelName(a_wheel));
		}

		// Arrows belong on the Ammo wheel (the owner, 2026-09-29: "if anything's already on the normal wheel, they would
		// get moved over. And make sure that the normal wheel is refreshed so there's nothing stale left on it."). Every
		// arrow entry on the Equipment wheel leaves it - the game's key with it, so the radial no longer shows it - and
		// goes to the Ammo wheel's first empty slot unless it is there already. Returns how many moved.
		int MigrateAmmo()
		{
			const auto before = inventory::Keys();
			int moved = 0;
			for (int s = 0; s < inventory::kSlots; ++s) {
				for (int i = static_cast<int>(g_wheels[kEquip][s].entries.size()) - 1; i >= 0; --i) {
					if (i >= static_cast<int>(g_wheels[kEquip][s].entries.size())) {
						continue;   // a removal above re-seated the slot
					}
					const auto id = g_wheels[kEquip][s].entries[i];
					if (!AllowedOnEquipment(id)) {
						RemoveAt(kEquip, s, i, "armour and clothing never go on the wheel (shields do) - it stays a favourite");
						if (IndexOf(g_wheels[kFav][0], id) < 0) {
							g_wheels[kFav][0].entries.push_back(id);
							g_wheels[kFav][0].active = 0;
						}
						++moved;
						continue;
					}
					if (!IsAmmo(id)) {
						continue;
					}
					RemoveAt(kEquip, s, i, "arrows belong on the Ammo wheel");
					++moved;
					bool there = false;
					for (const Slot& a : g_wheels[kAmmo]) {
						there = there || IndexOf(a, id) >= 0;
					}
					if (there) {
						continue;
					}
					bool placed = false;
					for (int a = 0; a < inventory::kSlots && !placed; ++a) {
						placed = g_wheels[kAmmo][a].entries.empty() && AddTo(kAmmo, a, id, "moved from the Equipment wheel");
					}
					if (!placed) {
						logger::info("wheels: the Ammo wheel is full - {} left the Equipment wheel and is on no wheel now", inventory::NameOf(id));
					}
				}
			}
			if (moved) {
				Save();
				PatchChanged(before);   // the Equipment wheel's pictures follow its keys: nothing stale left on it
				Refresh();
				logger::info("wheels: {} arrow entr{} moved from the Equipment wheel to the Ammo wheel", moved, moved == 1 ? "y" : "ies");
			}
			return moved;
		}
	}

	namespace
	{
		// RB (or A) on an Equipment slot of the HUD radial: the radial closes on NOTHING (the choice cleared, the stick
		// held centred), and once it has closed the game's own quick-key press for that key is run - the path the number
		// keys 1-8 take (quickkeys::PressQuickKey). The vanilla close did not equip reliably: 2026-10-01 01:52:39, "USE
		// Equipment slot 2 now (RB)" then "the game uses slot 2" (the view model's KeyIndex, our reading), and the Steel
		// Claymore on key 2 never came to hand - the bow stayed. What the use did is read back from the item itself.
		struct PendingUse
		{
			bool                                  on = false;
			bool                                  closed = false;
			bool                                  pressed = false;
			bool                                  direct = false;   // equipped at once through Actor::EquipObject: only the read-back is left
			bool                                  waitClose = false;   // a potion, poison or ingredient: used through the same call, but only once the radial has closed
			int                                   key = -1;
			std::uint32_t                         id = 0;
			inventory::State                      before{};
			const char*                           how = "RB";
			std::chrono::steady_clock::time_point armed{}, closedAt{}, verifyAt{};
		} g_use;
		std::string g_lastUse = "none yet";

		bool Equippable(std::uint32_t a_id)
		{
			auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
			if (!form) return false;
			const auto t = form->GetFormType();
			return t == RE::FormType::Weapon || t == RE::FormType::Armor || t == RE::FormType::Clothing || t == RE::FormType::Light;
		}

		// a potion, a poison or an ingredient: used (drunk, eaten) through the same call that puts equipment on - for the mouse click and the keyboard's
		// slot keys, whose radial the game's own key press could not be waited out of
		bool Consumable(std::uint32_t a_id)
		{
			auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
			if (!form) return false;
			const auto t = form->GetFormType();
			return t == RE::FormType::AlchemyItem || t == RE::FormType::Ingredient;
		}

		// the repair hammer (form 0x0000000C, a Misc item): the game's own quick-key press does nothing for it (the owner's log, 2026-10-20: "unchanged 0.7 s after
		// the game's key press ... nothing visible changed", IWP-011), so it is used from inside the Inventory menu once the radial has closed (HammerInvStart)
		bool MiscTool(std::uint32_t a_id)
		{
			return a_id == 0x0000000C;
		}

		// A repair hammer is used from INSIDE the Inventory menu, as the game does (IWP-011): the EquipObject call made in plain gameplay opened the repair menu but left
		// a stuck blurred screen (and, with no wheel, the game in menu mode). The Inventory is opened through the controller's own open-inventory input function
		// (pressed, released 200 ms later: released in the same frame the player menu opened on its System page), the hammer is equipped once the Inventory is up,
		// and the repair menu then sits over it; Esc returns to the Inventory and a second Esc to the game.
		struct HammerInv
		{
			bool          on = false;
			int           stage = 0;
			ULONGLONG     at = 0;
			std::uint32_t id = 0;
			std::string   fn, rel;
			bool          released = false;
		} g_hi;

		// the controller function that presses the open-inventory action ("OpenInventoryMenuInput_Pressed" in game 1.512.105.0); found once by name
		std::string OpenInventoryFunction(UE::UObject* a_pc)
		{
			static std::string found;
			static bool        looked = false;
			if (looked) return found;
			looked = true;
			const auto lower = [](std::string a_s) { for (auto& c : a_s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return a_s; };
			for (UE::UStruct* st = a_pc ? static_cast<UE::UStruct*>(a_pc->GetClass()) : nullptr; st && found.empty(); st = st->superStruct) {
				auto* f = *reinterpret_cast<UE::UField* const*>(&st->children);
				for (int n = 0; f && n < 8000; ++n, f = f->next) {
					if (pe::Utf8(f->GetClass()->GetFName().ToString()) != "Function") continue;
					const std::string name = pe::Utf8(f->GetFName().ToString());
					const std::string l = lower(name);
					if (l.find("openinventory") != std::string::npos && l.ends_with("_pressed")) {
						found = name;
						break;
					}
				}
			}
			logger::info("wheels: the controller's open-inventory function: {}", found.empty() ? std::string("not found - hammers are not used from the wheel") : found);
			return found;
		}

		bool HammerInvStart(std::uint32_t a_id)
		{
			if (g_hi.on) return true;
			if (menus::AnyOpen()) {
				logger::info("wheels: a menu is already open - the hammer is not used");
				return false;
			}
			auto*             pc = ui::PlayerController();
			const std::string fn = OpenInventoryFunction(pc);
			if (!pc || fn.empty()) {
				return false;
			}
			g_hi = {};
			g_hi.fn = fn;
			g_hi.rel = fn;
			g_hi.rel.replace(g_hi.rel.size() - 8, 8, "_Released");
			ui::Call press(pc, pe::Wide(fn).c_str());
			if (!press || !press.RunGuarded()) {
				logger::info("wheels: {} failed - the hammer is not used", fn);
				return false;
			}
			g_hi.on = true;
			g_hi.stage = 1;
			g_hi.at = GetTickCount64();
			g_hi.id = a_id;
			logger::info("wheels: {} pressed - the hammer is used once the Inventory is up", fn);
			return true;
		}

		void HammerInvTick()
		{
			if (!g_hi.on) return;
			const ULONGLONG now = GetTickCount64();
			if (g_hi.stage == 1) {
				if (!g_hi.released && now - g_hi.at >= 200) {
					g_hi.released = true;
					if (auto* pc = ui::PlayerController()) {
						ui::Call release(pc, pe::Wide(g_hi.rel).c_str());
						if (release) release.RunGuarded();
					}
				}
				if (menus::Active() == menus::Menu::kInventory) {
					logger::info("wheels: the Inventory is up after {} ms", now - g_hi.at);
					g_hi.stage = 2;
					g_hi.at = now + 500;
				} else if (now - g_hi.at > 3000) {
					g_hi.on = false;
					logger::info("wheels: the Inventory did not open within 3 s - the hammer is not used");
				}
			} else if (g_hi.stage == 2 && now >= g_hi.at) {
				g_hi.on = false;
				logger::info("wheels: {} used inside the Inventory through Actor::EquipObject", inventory::NameOf(g_hi.id));
				inventory::EquipKeyed(g_hi.id);
			}
		}

		void DriveUse()
{
			if (!g_use.on) {
				return;
			}
			using namespace std::chrono;
			const auto now = steady_clock::now();
			const auto name = inventory::NameOf(g_use.id);
			const int  n = g_use.key + 1;
			if (g_use.direct && g_use.waitClose) {
				if (quickkeys::RadialOpen()) {
					if (now - g_use.armed > 3s) {
						g_use.on = false;
						g_lastUse = std::format("slot {}: the radial never closed", n);
						logger::info("wheels: USE slot {} - the radial did not close within 3 s; {} not used", n, name);
					}
					return;
				}
				if (!g_use.closed) {
					g_use.closed = true;
					g_use.closedAt = now;
				}
				if (now - g_use.closedAt < 150ms) {
					return;
				}
				g_use.waitClose = false;
				g_use.verifyAt = now + (MiscTool(g_use.id) ? 2500ms : 300ms);   // a hammer's menu opens a little later
				if (MiscTool(g_use.id)) {
					HammerInvStart(g_use.id);
				} else {
					inventory::EquipKeyed(g_use.id);
				}
				logger::info("wheels: USE Equipment slot {} ({}) - {}: used through Actor::EquipObject on the TES thread, the radial has closed", n, g_use.how, name);
				return;
			}
			if (g_use.direct) {
				if (now < g_use.verifyAt) {
					return;
				}
				g_use.on = false;
				const auto st = inventory::StateOf(g_use.id);
				const auto ms = duration_cast<milliseconds>(now - g_use.armed).count();
				g_lastUse = std::format("slot {}: {} {} ({} -> {})", n, name, st.worn ? "equipped at once" : "NOT worn after the equip", inventory::Describe(g_use.before),
					inventory::Describe(st));
				logger::info("wheels: USE result slot {} ({}) - {}: {} -> {} {} ms after the press - {}", n, g_use.how, name, inventory::Describe(g_use.before),
					inventory::Describe(st), ms, st.worn ? "equipped at once through Actor::EquipObject" : MiscTool(g_use.id) ? (menus::AnyOpen() ? "a menu is open (the hammer's repair menu?)" : "NO menu opened") : "NOT worn: the equip did not take");
				return;
			}
			if (!g_use.pressed) {
				if (quickkeys::RadialOpen()) {
					if (now - g_use.armed > 3s) {
						g_use.on = false;
						g_lastUse = std::format("slot {}: the radial never closed", n);
						logger::info("wheels: USE slot {} - the radial did not close within 3 s; {} not used", n, name);
					}
					return;
				}
				if (!g_use.closed) {
					g_use.closed = true;
					g_use.closedAt = now;
				}
				if (now - g_use.closedAt < 150ms) {
					return;   // the TES side's own close settles first
				}
				if (menus::AnyOpen()) {
					if (now - g_use.closedAt > 1500ms) {
						g_use.on = false;
						g_lastUse = std::format("slot {}: a menu stayed open", n);
						logger::info("wheels: USE slot {} - a menu is open 1.5 s after the radial closed; {} not used", n, name);
					}
					return;
				}
				const auto keys = inventory::Keys();
				const auto st = inventory::StateOf(g_use.id);
				if (keys[static_cast<std::size_t>(g_use.key)] != g_use.id) {
					g_use.on = false;
					g_lastUse = std::format("slot {}: the key moved", n);
					logger::info("wheels: USE slot {} - the game's key {} no longer holds {}; nothing pressed", n, n, name);
					return;
				}
				if (st != g_use.before) {
					// the radial's own close used it after all (the choice was cleared, so it should not have): never twice
					g_use.on = false;
					g_lastUse = std::format("slot {}: {} used by the radial's own close ({} -> {})", n, name, inventory::Describe(g_use.before),
						inventory::Describe(st));
					logger::info("wheels: USE slot {} - the radial's own close already used {} ({} -> {}); the key is not pressed again", n, name,
						inventory::Describe(g_use.before), inventory::Describe(st));
					return;
				}
				const bool ok = quickkeys::PressQuickKey(g_use.key);
				logger::info("wheels: USE slot {} - the game's own quick key {} pressed for {} (VEnhancedAltarPlayerController Quick{}Input_Pressed / _Released){}",
					n, n, name, n, ok ? "" : " - the call FAILED");
				if (!ok) {
					g_use.on = false;
					g_lastUse = std::format("slot {}: the key press could not be made", n);
					return;
				}
				g_use.pressed = true;
				g_use.verifyAt = now + 700ms;
				return;
			}
			if (now < g_use.verifyAt) {
				return;
			}
			g_use.on = false;
			const auto st = inventory::StateOf(g_use.id);
			const bool menu = menus::AnyOpen();
			if (st != g_use.before || menu) {
				g_lastUse = std::format("slot {}: {} used by the game's key press ({} -> {}{})", n, name, inventory::Describe(g_use.before),
					inventory::Describe(st), menu ? ", a menu opened" : "");
				logger::info("wheels: USE result slot {} ({}) - {}: {} -> {}{} - used through the game's own quick key {}", n, g_use.how, name,
					inventory::Describe(g_use.before), inventory::Describe(st), menu ? ", a menu opened" : "", n);
				return;
			}
			if (Equippable(g_use.id) && st.carried && !st.worn) {
				g_lastUse = std::format("slot {}: {} unchanged after the key press - fallback equip", n, name);
				logger::info("wheels: USE result slot {} ({}) - {} unchanged 0.7 s after the game's key press ({}); FALLBACK: equipped through Actor::EquipObject on the TES thread",
					n, g_use.how, name, inventory::Describe(st));
				inventory::EquipKeyed(g_use.id);
				return;
			}
			g_lastUse = std::format("slot {}: {} unchanged after the key press", n, name);
			logger::info("wheels: USE result slot {} ({}) - {} unchanged 0.7 s after the game's key press ({}) - nothing visible changed", n, g_use.how, name,
				inventory::Describe(st));
		}

		std::chrono::steady_clock::time_point g_hudCheckAt{};
		UE::UObject*                          g_hudIconBefore = nullptr;   // compared only

		// the HUD's spell picture (VHUDMainViewModel.SpellIcon), read to learn whether it follows selectedSpell
		UE::UObject* HudSpellIcon()
		{
			static auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VHUDMainViewModel");
			if (!cls || !reflect::Ok()) return nullptr;
			const auto off = reflect::Offset(cls, "SpellIcon");
			const auto vms = reflect::Instances(cls);
			return off >= 0 && !vms.empty() ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(vms.front()) + off) : nullptr;
		}

		// The HUD's spell picture after a Magic wheel choice. Setting selectedSpell does not reach the HUD: the magic menu's
		// own pick makes the game write VHUDMainViewModel.SpellIcon natively and broadcast the field's change, and the HUD's
		// binding then calls GetSpellIcon once (probe B, 2026-10-01: OnItemClicked, then GetSpellIcon x1 and nothing else
		// spell-related on the HUD; every Magic wheel pick logged "did NOT change"). The same, from here: the spell's icon
		// written into SpellIcon, then MVVMViewModelBase's K2_BroadcastFieldValueChanged for "SpellIcon" (the HUD's view
		// models are VViewModelBase -> MVVMViewModelBase).
		// The HUD's quick-magic picture itself. The view model write and its broadcast did not reach it (the owner, 2026-10-01:
		// "The widget did not update"; a trace of VHUDMainViewModel showed our five K2_BroadcastFieldValueChanged calls and no
		// GetSpellIcon after any of them - the HUD's binding listens under another field id). The widget has its own
		// SetMagicTexture(InTexture) (WBP_ModernHud_MagicIcon_C, from VLegacyHudMagicIcon); called on the live one from
		// TestBench with the Shield picture, the owner: "I did just notice the shield icon". Every live instance (never a
		// template) gets it.
		int SetHudWidgetTexture(UE::UObject* a_icon)
		{
			static auto* widgetCls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Game/UI/Modern/HUD/Main/WBP_ModernHud_MagicIcon.WBP_ModernHud_MagicIcon_C");
			if (!widgetCls || !a_icon) return 0;
			int set = 0;
			for (auto* w : reflect::Instances(widgetCls)) {
				ui::Call call(w, L"SetMagicTexture");
				if (!call || !call.Set("InTexture", a_icon)) continue;
				if (call.Run()) ++set;
			}
			return set;
		}

		void PushHudSpellIcon(std::uint32_t a_formID)
		{
			static auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VHUDMainViewModel");
			static auto* texCls = ui::Class(L"/Script/Engine.Texture2D");
			if (!cls || !texCls || !reflect::Ok()) {
				logger::warn("wheels: the HUD's spell picture cannot be set (view model class {}, Texture2D {})", cls ? "found" : "missing", texCls ? "found" : "missing");
				return;
			}
			const auto off = reflect::Offset(cls, "SpellIcon");
			const auto vms = reflect::Instances(cls);
			auto*      icon = rows::SpellIcon(a_formID);
			if (off < 0 || vms.empty() || !icon || !icon->GetClass()->IsChildOf(texCls)) {
				logger::info("wheels: the HUD's spell picture left as it is (SpellIcon at {}, {} view model(s), icon {})", off, vms.size(),
					!icon ? std::string("none") : pe::Utf8(icon->GetClass()->GetFName().ToString()));
				return;
			}
			auto* vm = vms.front();
			*reinterpret_cast<UE::UObject**>(reinterpret_cast<std::uint8_t*>(vm) + off) = icon;
			const int widgets = SetHudWidgetTexture(icon);
			logger::info("wheels: the HUD's quick-magic picture set to {} on {} widget(s) (SetMagicTexture)", pe::Utf8(icon->GetFName().ToString()), widgets);
			ui::Call broadcast(vm, L"K2_BroadcastFieldValueChanged");
			void* id = broadcast ? broadcast.At("FieldId") : nullptr;
			if (!id) {
				logger::warn("wheels: SpellIcon written, but the view model has no K2_BroadcastFieldValueChanged(FieldId) - the HUD keeps its picture until the game asks again");
				return;
			}
			new (id) UE::FName(L"SpellIcon", UE::EFindName::Add);   // FFieldNotificationId { FName FieldName }
			broadcast.Run();
			logger::info("wheels: the HUD's spell picture set to {} and its change broadcast", pe::Utf8(icon->GetFName().ToString()));
		}

		void CheckHud()
		{
			if (g_hudCheckAt == std::chrono::steady_clock::time_point{} || std::chrono::steady_clock::now() < g_hudCheckAt) return;
			g_hudCheckAt = {};
			auto* now = HudSpellIcon();
			logger::info("wheels: the HUD's spell picture {} after the Magic wheel's choice ({})", now != g_hudIconBefore ? "CHANGED" : "did NOT change",
				now ? pe::Utf8(now->GetFName().ToString()) : std::string("none"));
		}
	}

	namespace
	{
		bool ObserveContext()
		{
			if (!inventory::Ready()) ResetCarriedHistory();
			static sessionepoch::Token observed{};
			const auto now = sessioncontext::Current();
			if (now != observed) {
				ResetCarriedHistory();
				observed = now;
				g_pending = {};
				g_use = {};
				g_holdKey = -1;
				g_holdItem = 0;
				g_leaving.clear();
				g_hudCheckAt = {};
				g_hudIconBefore = nullptr;
				g_loadedFor.clear();
				g_emptySetup.Reset();
				g_initializeEmpty = false;
				g_sanitized = false;
			}
			return now.context.ready;
		}
	}

	bool Ready() { return EnsureLoaded(); }

	const char* Name(Wheel a_wheel)
	{
		return a_wheel == Wheel::kMagic ? "Magic" : a_wheel == Wheel::kAmmo ? "Ammo" : "Equipment";
	}

	Wheel Active()
	{
		return static_cast<Wheel>(g_active.load());
	}

	void NoteLeaving(std::uint32_t a_formID)
	{
		if (a_formID) {
			g_leaving.insert(a_formID);
		}
	}

	bool IsFavourite(std::uint32_t a_formID)
	{
		if (!a_formID || !EnsureLoaded()) {
			return false;
		}
		for (const int w : { kEquip, kAmmo, kFav }) {
			for (const Slot& slot : g_wheels[w]) {
				if (IndexOf(slot, a_formID) >= 0) {
					return true;
				}
			}
		}
		return false;
	}

	std::unordered_set<std::uint32_t> Favourites()
	{
		std::unordered_set<std::uint32_t> out;
		if (!EnsureLoaded()) {
			return out;
		}
		for (const int w : { kEquip, kAmmo, kFav }) {
			for (const Slot& slot : g_wheels[w]) {
				out.insert(slot.entries.begin(), slot.entries.end());
			}
		}
		return out;
	}

	std::uint32_t Generation() { return g_generation.load(std::memory_order_relaxed); }

	bool CanFavourite(std::uint32_t a_formID)
	{
		return a_formID != 0;   // armour too: a favourite, just never on the wheel
	}

	void ToggleItem(std::uint32_t a_formID)
	{
		if (!a_formID || !EnsureLoaded()) {
			return;
		}
		ToggleFavourite(IsAmmo(a_formID) ? kAmmo : kEquip, a_formID);   // arrows go to the Ammo wheel, as Y
	}

	std::array<SlotCount, 8> SlotCounts(Wheel a_wheel)
	{
		std::array<SlotCount, 8> out{};
		if (!EnsureLoaded()) {
			return out;
		}
		const int w = a_wheel == Wheel::kMagic ? kMagic : a_wheel == Wheel::kAmmo ? kAmmo : kEquip;
		// the Equipment counter counts what LT / RT can show - an entry not carried, or on another slot's key, is not one
		// (the owner, 2026-10-01: slot 2 read 2 while LT / RT had nothing to step to) - and says which of those the slot
		// shows now (the owner, 2026-10-01: "the numbers for which item I'm selecting isn't changing whether I'm on one out
		// of five, two out of five")
		const auto keys = w == kEquip ? inventory::Keys() : inventory::KeyMap{};
		for (int s = 0; s < inventory::kSlots; ++s) {
			const Slot& slot = g_wheels[w][s];
			const int shown = w == kMagic ? KnownMagicEntry(slot, slot.active, +1, true) : slot.active;
			for (int i = 0; i < static_cast<int>(slot.entries.size()); ++i) {
				if ((w == kEquip && !Unreachable(s, slot.entries[i], keys).empty()) ||
					(w == kMagic && !rows::PlayerHasSpell(slot.entries[i])) ||
					(w == kAmmo && (!IsAmmo(slot.entries[i]) || !inventory::Has(slot.entries[i])))) {
					continue;
				}
				++out[s].count;
				if (i == shown) {
					out[s].position = out[s].count;
				}
			}
		}

		return out;
	}

	int SlotCap() { return Cap(); }

	std::unordered_set<std::uint32_t> MagicFavourites()
	{
		std::unordered_set<std::uint32_t> out;
		if (!EnsureLoaded()) {
			return out;
		}
		for (const Slot& slot : g_wheels[kMagic]) {
			out.insert(slot.entries.begin(), slot.entries.end());
		}
		return out;
	}

	void ToggleSpell(std::uint32_t a_formID)
	{
		if (!a_formID || !EnsureLoaded()) {
			return;
		}
		ToggleFavourite(kMagic, a_formID);
	}

	std::array<std::uint32_t, 8> MagicSlots()
	{
		std::array<std::uint32_t, 8> out{};
		if (!EnsureLoaded()) {
			return out;
		}
		for (int s = 0; s < inventory::kSlots; ++s) {
			const Slot& slot = g_wheels[kMagic][s];
			const int known = KnownMagicEntry(slot, slot.active, +1, true);
			out[s] = known >= 0 ? slot.entries[known] : 0;
		}
		return out;
	}

	std::array<std::uint32_t, 8> AmmoSlots()
	{
		std::array<std::uint32_t, 8> out{};
		if (!EnsureLoaded()) {
			return out;
		}
		for (int s = 0; s < inventory::kSlots; ++s) {
			const Slot& slot = g_wheels[kAmmo][s];
			const auto id = slot.active >= 0 && slot.active < static_cast<int>(slot.entries.size()) ? slot.entries[slot.active] : 0u;
			out[s] = id && IsAmmo(id) && inventory::Has(id) ? id : 0;
		}
		return out;
	}

	// The radial and Inventory panel preserve the eight saved cell positions.
	int AmmoSlotAtRing(int a_ring)
	{
		return a_ring >= 0 && a_ring < 8 && AmmoSlots()[a_ring] ? a_ring : -1;
	}

	std::array<std::uint32_t, 8> AmmoRingSlots() { return AmmoSlots(); }

	Wheel AssignmentWheel(menus::Menu a_menu)
	{
		if (a_menu == menus::Menu::kMagic) return Wheel::kMagic;
		return a_menu == menus::Menu::kInventory && IsAmmo(rows::HighlightedItem()) ? Wheel::kAmmo : Wheel::kEquipment;
	}

	void AssignHighlighted(menus::Menu a_menu, int a_slot)
	{
		if (a_slot < 0 || a_slot >= inventory::kSlots || !EnsureLoaded()) {
			return;
		}
		std::uint32_t id = 0;
		int           wheel = -1;
		if (a_menu == menus::Menu::kMagic) {
			id = rows::HighlightedSpell();
			wheel = kMagic;
		} else if (a_menu == menus::Menu::kInventory) {
			id = rows::HighlightedItem();
			wheel = id && IsAmmo(id) ? kAmmo : kEquip;
		}
		if (!id || wheel < 0) {
			logger::info("wheels: slot key {} in the {} - no item or spell highlighted", a_slot + 1, menus::Name(a_menu));
			return;
		}
		if (wheel == kEquip && !AllowedOnEquipment(id)) {
			ToggleFavourite(kEquip, id);   // armour with bArmourOnWheel=0: a favourite, never on the wheel
			return;
		}
		const auto before = inventory::Keys();
		Slot&      slot = g_wheels[wheel][a_slot];
		if (const int idx = IndexOf(slot, id); idx >= 0) {
			RemoveAt(wheel, a_slot, idx, std::format("slot key {} pressed again on it", a_slot + 1));
		} else {
			RemoveFromOtherSlots(wheel, a_slot, id);   // an item is on one slot
			if (static_cast<int>(slot.entries.size()) >= (wheel == kAmmo ? 1 : Cap())) {
				RemoveAt(wheel, a_slot, static_cast<int>(slot.entries.size()) - 1, std::format("slot {} is full - its last entry leaves for the new one", a_slot + 1));
			}
			AddTo(wheel, a_slot, id, "slot key");
		}
		Save();
		PatchChanged(before);
		Refresh();
	}

	void Favourite(menus::Menu a_menu)
{
		if (!EnsureLoaded()) {
			return;
		}
		if (a_menu == menus::Menu::kMagic) {
			if (const auto spell = rows::HighlightedSpell()) {
				ToggleFavourite(kMagic, spell);
			}
		} else if (a_menu == menus::Menu::kInventory) {
			if (const auto item = rows::HighlightedItem()) {
				ToggleFavourite(IsAmmo(item) ? kAmmo : kEquip, item);   // arrows go to the Ammo wheel
			}
		}
	}

	void AssignPressed(menus::Menu a_menu, int a_slot)
	{
		if (AssignmentWheel(a_menu) == Wheel::kAmmo) {
			AssignHighlighted(a_menu, a_slot);   // data only: never assign arrows to native Equipment hotkeys
			return;
		}
		if (!EnsureLoaded()) {
			return;
		}
		if (a_menu == menus::Menu::kMagic) {
			// the Magic wheel's assign is ours (the game never sees A here)
			if (a_slot < 0) {
				logger::info("wheels: assign in the magic menu with no slot pointed - nothing to do");
				return;
			}
			const auto spell = rows::HighlightedSpell();
			if (!spell) {
				return;
			}
			Slot& slot = g_wheels[kMagic][a_slot];
			if (const int idx = IndexOf(slot, spell); idx >= 0) {
				RemoveAt(kMagic, a_slot, idx, "assign pressed on a spell already in the slot");
				RemoveFromOtherSlots(kMagic, a_slot, spell);
			} else {
				// one slot a spell (the owner, 2026-09-30: "if I assign magic to a slot, I shouldn't be able to assign it the
				// same magic to two other slots. Because it's redundant" - as the Equipment wheel already keeps it): a spell
				// assigned here leaves every other slot
				RemoveFromOtherSlots(kMagic, a_slot, spell);
				AddTo(kMagic, a_slot, spell, "assign");
			}
			Save();
			Refresh();
			return;
		}
		if (a_menu != menus::Menu::kInventory) {
			return;
		}
		if (g_pending.on) {
			SettleNothing();
		}
		g_pending = {};
		g_pending.on = true;
		g_pending.slot = a_slot;
		g_pending.before = inventory::Keys();
		if (a_slot >= 0 && g_pending.before[a_slot]) {
			g_pending.parked = g_pending.before[a_slot];
			inventory::ClearKey(g_pending.parked);   // off the key while the game assigns: whatever comes back is what was pressed
			g_pending.before[a_slot] = 0;
		}
		logger::info("wheels: assign pressed with Equipment slot {} pointed ({} taken off the key while the game assigns)", a_slot + 1,
			g_pending.parked ? inventory::NameOf(g_pending.parked) : "nothing");
	}

	void Tick(bool a_assignHeld)
	{
		if (!ObserveContext()) return;
		CheckHud();
		AssertMagic();
		DriveUse();
		HammerInvTick();
		if (!g_pending.on) {
			return;
		}
		++g_pending.reads;
		if (!a_assignHeld) {
			g_pending.released = true;
		}
		if (g_pending.released) {
			++g_pending.readsSinceRelease;
		}
		const auto after = inventory::Keys();
		for (int s = 0; s < inventory::kSlots; ++s) {
			if (after[s] && after[s] != g_pending.before[s]) {
				Settle(s, after[s]);
				return;
			}
		}
		if (g_pending.readsSinceRelease >= 20 || g_pending.reads >= 300) {
			SettleNothing();
		}
	}

	void ClearSlot(Wheel a_wheel, int a_slot)
	{
		if (a_slot < 0 || a_slot >= inventory::kSlots || !EnsureLoaded()) {
			return;
		}
		const int w = a_wheel == Wheel::kMagic ? kMagic : a_wheel == Wheel::kAmmo ? kAmmo : kEquip;
		Slot& slot = g_wheels[w][a_slot];
		if (slot.entries.empty()) {
			logger::info("wheels: CLEAR {} slot {} - already empty", Name(a_wheel), a_slot + 1);
			return;
		}
		const auto n = slot.entries.size();
		while (!slot.entries.empty()) {
			RemoveAt(w, a_slot, static_cast<int>(slot.entries.size()) - 1, "slot cleared (X held)");
		}
		slot.active = -1;
		logger::info("wheels: CLEAR {} slot {} - {} entr{} removed", Name(a_wheel), a_slot + 1, n, n == 1 ? "y" : "ies");
		Save();
		Refresh();
	}

	void CycleEntry(Wheel a_wheel, int a_slot, int a_dir)
	{
		if (a_slot < 0 || a_slot >= inventory::kSlots) {
			logger::info("wheels: CYCLE with no slot pointed - nothing to cycle");
			return;
		}
		if (!EnsureLoaded()) {
			return;
		}
		const int w = a_wheel == Wheel::kMagic ? kMagic : a_wheel == Wheel::kAmmo ? kAmmo : kEquip;
		Slot& slot = g_wheels[w][a_slot];
		const int n = static_cast<int>(slot.entries.size());
		int next = -1;
		std::string skipped;
		if (w == kMagic) {
			// Cycle from the entry currently displayed, skipping spells absent from this save.
			const int shown = KnownMagicEntry(slot, slot.active, +1, true);
			next = KnownMagicEntry(slot, shown, a_dir, false);
		} else if (n > 0) {
			next = NextEquipment(a_slot, slot.active < 0 ? (a_dir > 0 ? -1 : 0) : slot.active, a_dir, &skipped);
		}
		if (next < 0 || next == slot.active) {
			logger::info("wheels: CYCLE {} slot {} - {} entr{}, nothing to step to{}", Name(a_wheel), a_slot + 1, n, n == 1 ? "y" : "ies",
				skipped.empty() ? std::string() : " (" + skipped + ")");
			return;
		}
		if (!skipped.empty()) {
			logger::info("wheels: CYCLE {} slot {} - {}", Name(a_wheel), a_slot + 1, skipped);
		}
		if (w == kEquip && !inventory::SetKey(slot.entries[next], a_slot)) {
			return;
		}
		slot.active = next;
		Save();
		logger::info("wheels: CYCLE {} slot {} -> {} ({} of {})", Name(a_wheel), a_slot + 1, NameOf(w, slot.entries[next]), next + 1, n);
		if (w == kEquip) {
			PatchEquipment(a_slot);
		}
		Refresh();
	}

	void SwitchWheel(int a_dir)
	{
		// Equipment -> Magic -> Ammo (when enabled) -> Equipment; left goes the other way round
		std::vector<Wheel> order{ Wheel::kEquipment, Wheel::kMagic };
		if (settings::Get().ammoWheel) order.push_back(Wheel::kAmmo);
		int at = 0;
		for (int i = 0; i < static_cast<int>(order.size()); ++i) {
			if (order[static_cast<std::size_t>(i)] == Active()) at = i;
		}
		const int  n = static_cast<int>(order.size());
		const Wheel next = order[static_cast<std::size_t>((at + (a_dir > 0 ? 1 : n - 1)) % n)];
		g_active.store(static_cast<int>(next));
		logger::info("wheels: SWITCH {} -> the {} wheel is active", a_dir > 0 ? "right" : "left", Name(next));
		if (quickkeys::RadialOpen() && EnsureLoaded()) {
			ShowMagic(next == Wheel::kMagic);
		}
	}

	void KeepActive()
	{
		g_keepUntil = GetTickCount64() + 1500;
	}

	void SetActive(Wheel a_wheel)
{
		if (a_wheel == Wheel::kAmmo && !(settings::Get().ammoWheel)) {
			logger::info("wheels: the Ammo wheel's button - Ammo is disabled, nothing to show");
			return;
		}
		g_keepUntil = GetTickCount64() + 1500;   // this press chose the wheel the radial opens on
if (a_wheel == Active()) {
			return;
		}
		g_active.store(static_cast<int>(a_wheel));
logger::info("wheels: the {} wheel's own button - it is the active wheel", Name(a_wheel));
		if (quickkeys::RadialOpen() && EnsureLoaded()) {
			ShowMagic(a_wheel == Wheel::kMagic);
		}
	}

	void QuickUse(int a_slot)
	{
		if (a_slot < 0 || a_slot >= inventory::kSlots || quickkeys::GameMessageUp() || !EnsureLoaded()) return;
		if (Active() == Wheel::kMagic) { UseMagic(a_slot); return; }
		if (Active() == Wheel::kAmmo) {
			if (settings::Get().ammoWheel) UseAmmo(a_slot);
			return;
		}
		const Slot& slot = g_wheels[kEquip][a_slot];
		if (slot.active < 0 || slot.active >= static_cast<int>(slot.entries.size())) return;
		const auto id = slot.entries[slot.active];
		if (!inventory::Has(id)) return;
		if (MiscTool(id)) {
			HammerInvStart(id);   // retain the existing Inventory-before-hammer flow
		} else if (Equippable(id)) {
			inventory::ToggleKeyed(id);   // resolve worn state on the TES thread, not at enqueue time
		} else {
			inventory::EquipKeyed(id);   // consumables/poisons use the existing game action directly
		}
		g_lastUse = std::format("slot {}: direct quick use of {}", a_slot + 1, inventory::NameOf(id));
		logger::info("wheels: direct quick use slot {} without opening the radial", a_slot + 1);
	}

	void UseNow(int a_slot, const char* a_how, bool a_stayOpen)
	{
		if (a_slot < 0) {
			logger::info("wheels: USE ({}) with no slot pointed - nothing used", a_how);
			return;
		}
		if (!EnsureLoaded()) {
			return;
		}
		const auto id = inventory::Keys()[static_cast<std::size_t>(a_slot)];
		if (!id) {
			logger::info("wheels: USE Equipment slot {} ({}) - the game's key {} holds nothing; nothing used", a_slot + 1, a_how, a_slot + 1);
			g_lastUse = std::format("slot {}: empty key", a_slot + 1);
			return;
		}
		g_use = {};
		g_use.on = true;
		g_use.key = a_slot;
		g_use.id = id;
		g_use.before = inventory::StateOf(id);
		g_use.how = a_how;
		g_use.armed = std::chrono::steady_clock::now();
		// Equipment - a weapon, a shield, armour, clothing, a torch - goes on AT ONCE through Actor::EquipObject on the TES
		// thread (lock argument false). The game's own quick-key press (Quick<N>Input_Pressed / _Released) equipped nothing
		// in any of the 9 RB / A uses of 2026-10-01 02:32-02:37 ("unchanged 0.7 s after the game's key press ... FALLBACK"),
		// and the EquipObject fallback worked every time ("(x1, worn)") - so the press and its 0.7 s wait are gone for it.
		if (Equippable(id) || Consumable(id) || MiscTool(id)) {
			if (g_use.before.worn && !MiscTool(id)) {
				// what the game's own quick key does with an item already worn (unequip it, or nothing) is not known from the
				// code or the game - the key press itself did nothing at all - so nothing is done, and said
				// choosing the slot of an item that is already on takes it off
				g_use.on = false;
				g_lastUse = std::format("slot {}: {} was worn - taken off", a_slot + 1, inventory::NameOf(id));
				logger::info("wheels: USE Equipment slot {} ({}) - {} is already worn ({}): taken off", a_slot + 1, a_how, inventory::NameOf(id), inventory::Describe(g_use.before));
				inventory::UnequipKeyed(id);
				return;
			}
			g_use.direct = true;
			if (!Equippable(id) && !a_stayOpen) {
				// the game's own answer to a use it refuses ('You cannot drink any more potions right now') is a message that needs A; over a
				// radial that is still open it kept the radial up and A never reached the message - so it is used once the radial is closed
				g_use.waitClose = true;
				logger::info("wheels: USE Equipment slot {} ({}) - {} ({}): used once the radial has closed", a_slot + 1, a_how, inventory::NameOf(id), inventory::Describe(g_use.before));
				return;
			}
			g_use.verifyAt = g_use.armed + 300ms;
			inventory::EquipKeyed(id);
			logger::info("wheels: USE Equipment slot {} ({}) - {} ({}): equipped at once through Actor::EquipObject on the TES thread (no game key press, no wait for the close)",
				a_slot + 1, a_how, inventory::NameOf(id), inventory::Describe(g_use.before));
			return;
		}
		// anything else (a potion, a scroll, a book...): the game's own key press once the radial has closed, as before -
		// unproven for these kinds (it never equipped a weapon), and the result is read back and logged
		logger::info("wheels: USE Equipment slot {} ({}) - {} ({}); the radial closes on nothing, then the game's own quick key {} is pressed",
			a_slot + 1, a_how, inventory::NameOf(id), inventory::Describe(g_use.before), a_slot + 1);
	}

	std::string LastUse() { return g_lastUse; }

	bool StaysOpenAfterUse(int a_slot)
	{
		if (!settings::Get().stayOpenAfterUse || a_slot < 0 || a_slot >= inventory::kSlots || !EnsureLoaded()) {
			return false;
		}
		const auto id = inventory::Keys()[static_cast<std::size_t>(a_slot)];
		return id && (Equippable(id) || Consumable(id));   // a poison too: its question over the open wheel is answered without choosing again (quickkeys::GameMessageUp), and closing the wheel for it needed the B fallback, whose B reached the game as crouch   // weapons, armour, clothing, lights and potions, poisons, ingredients (the game's refusal message over an open radial may need a click)
	}

	void HoldKey(int a_slot)
	{
		if (a_slot < 0 || a_slot >= inventory::kSlots || g_holdKey >= 0 || !EnsureLoaded()) {
			return;
		}
		const auto id = inventory::Keys()[static_cast<std::size_t>(a_slot)];
		if (!id) {
			return;   // nothing on the game's key: nothing for its close to use
		}
		g_holdKey = a_slot;
		g_holdItem = id;
		inventory::ClearSlot(a_slot);
		logger::info("wheels: the game's key {} taken off {} while the radial closes (its close would use it)", a_slot + 1, inventory::NameOf(id));
	}

	void ReleaseKey()
	{
		if (!ObserveContext()) return;
		if (g_holdKey < 0) {
			return;
		}
		const int slot = g_holdKey;
		const auto id = g_holdItem;
		g_holdKey = -1;
		g_holdItem = 0;
		if (inventory::Has(id) && inventory::SetKey(id, slot)) {
			logger::info("wheels: the game's key {} is back on {}", slot + 1, inventory::NameOf(id));
		}
	}

	void UseMagic(int a_slot)
	{
		if (a_slot < 0 || a_slot >= inventory::kSlots) {
			logger::info("wheels: the Magic wheel closed on no slot - nothing cast-ready changed");
			return;
		}
		if (!EnsureLoaded()) {
			return;
		}
		const Slot& slot = g_wheels[kMagic][a_slot];
		const int known = KnownMagicEntry(slot, slot.active, +1, true);
		if (known < 0) {
			logger::info("wheels: Magic slot {} has no spell known in this save", a_slot + 1);
			return;
		}
		const auto id = slot.entries[known];
		auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!spell || !player) {
			logger::warn("wheels: Magic slot {} holds 0x{:08X}, which is not a spell now", a_slot + 1, id);
			return;
		}
		// the SELECTED spell - the one the HUD shows and the cast button casts - is set by the game's own PlayerCharacter::
		// SetSelectedSpell, as the Magic menu does (a hardware write watch on the field showed that one function, RVA 0x66075B0,
		// as its only writer, on the TES thread). It also drops the old spell's cast sound and tells the game the selection
		// changed, so the first cast has its own animation and sound. Writing the field ourselves left both stale, and
		// SetCurrentSpell (the caster's spell in use, which the menu never sets) took the sound away.
		const auto formID = spell->GetFormID();
		auto*      before = player->selectedSpell;
		logger::info("wheels: Magic slot {} used - {} is now the selected spell ({})", a_slot + 1, rows::SpellName(formID),
			before == static_cast<RE::MagicItem*>(spell) ? "it was already" : before ? "another spell was" : "none was");
		{
			testhread::Post([formID] {
				// The save can change after input was queued: ownership must still hold on the TES thread.
				if (!rows::PlayerHasSpell(formID)) return;
				inventory::SweepNativeSpellKeys();   // the game's own key must find no spell of its own to select once we have chosen
				auto* pc = RE::PlayerCharacter::GetSingleton();
				auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(formID);
				if (!pc || !sp || pc->selectedSpell == static_cast<RE::MagicItem*>(sp)) return;
				static REL::Relocation<void (*)(RE::PlayerCharacter*, RE::MagicItem*)> setSelected{ REL::ID(406972) };
				setSelected(pc, static_cast<RE::MagicItem*>(sp));
			});
		}
		g_hudCheckAt = std::chrono::steady_clock::now() + 500ms;
		g_hudIconBefore = HudSpellIcon();
		PushHudSpellIcon(spell->GetFormID());
	}

	void UseAmmo(int a_slot)
	{
		if (a_slot < 0) {
			logger::info("wheels: the Ammo wheel closed on no slot - nothing equipped");
			return;
		}
		if (!EnsureLoaded()) {
			return;
		}
		const int ammoSlot = AmmoSlotAtRing(a_slot);   // a_slot is the ring position pointed
		if (ammoSlot < 0) {
			logger::info("wheels: nothing sits at ring position {} of the Ammo wheel", a_slot + 1);
			return;
		}
		const Slot& slot = g_wheels[kAmmo][ammoSlot];
		if (slot.active < 0 || slot.active >= static_cast<int>(slot.entries.size())) {
			logger::info("wheels: Ammo slot {} is empty", ammoSlot + 1);
			return;
		}
		logger::info("wheels: Ammo slot {} used - {}", a_slot + 1, inventory::NameOf(slot.entries[static_cast<std::size_t>(slot.active)]));
		ammo::EquipArrows(slot.entries[static_cast<std::size_t>(slot.active)]);
	}

	void PanelShown(menus::Menu a_menu)
	{
		if (!EnsureLoaded()) {
			return;
		}
		ShowMagic(a_menu == menus::Menu::kMagic);
	}

	void PanelHidden()
	{
		ShowMagic(false);
	}

	void RadialShown()
	{
		if (!settings::Get().rememberWheel && GetTickCount64() >= g_keepUntil && Active() != Wheel::kEquipment) {   // a plain open starts on the Equipment wheel, whichever wheel it was left on
			g_active.store(static_cast<int>(Wheel::kEquipment));
			logger::info("wheels: the radial opens on the Equipment wheel (bRememberWheel=0)");
		}
		g_keepUntil = 0;
if (Active() == Wheel::kAmmo && !(settings::Get().ammoWheel)) {
			g_active.store(static_cast<int>(Wheel::kEquipment));
		}
		if (EnsureLoaded()) {
			ShowMagic(Active() == Wheel::kMagic);
		}
	}

	void RadialHidden()
	{
		ShowMagic(false);
	}

	std::array<std::string, 8> Describe(Wheel a_wheel)
	{
		std::array<std::string, 8> out;
		if (!EnsureLoaded()) {
			return out;
		}
		const int w = a_wheel == Wheel::kMagic ? kMagic : a_wheel == Wheel::kAmmo ? kAmmo : kEquip;
		for (int s = 0; s < inventory::kSlots; ++s) {
			const Slot& slot = g_wheels[w][s];
			for (int i = 0; i < static_cast<int>(slot.entries.size()); ++i) {
				out[s] += (i ? ", " : "") + NameOf(w, slot.entries[i]) + (i == slot.active ? "*" : "");
			}
		}
		return out;
	}

	std::string Status()
	{
		int equip = 0, magic = 0, ammo = 0;
		for (int s = 0; s < inventory::kSlots; ++s) {
			equip += static_cast<int>(g_wheels[kEquip][s].entries.size());
			magic += static_cast<int>(g_wheels[kMagic][s].entries.size());
			ammo += static_cast<int>(g_wheels[kAmmo][s].entries.size());
		}
		return std::format("character {}; Equipment {} entries, Magic {} entries, Ammo {} entries; pictures show {}", g_loadedFor.empty() ? "-" : g_loadedFor, equip,
			magic, ammo, g_shown == Shown::kGame ? "the game's keys" : g_shown == Shown::kMagic ? "the Magic wheel" : "the Equipment wheel");
	}
}
