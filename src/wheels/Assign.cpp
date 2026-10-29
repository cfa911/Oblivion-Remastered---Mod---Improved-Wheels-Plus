// Entries added to, removed from and moved between slots; the pending Equipment assign; favourites; arrows moved to the Ammo wheel; the pictures that follow.
#include "wheels/WheelsInternal.h"

#include "game/Menus.h"
#include "game/Rows.h"
#include "wheels/KnownEntry.h"

namespace wheels::detail
{
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

	// Drawing is owned by the overlays (MagicWheel.cpp): nothing is written into the game's view model - every slot the
	// old SetIcons redrew was the widget's own "slot changed" and ticked (the owner, 2026-09-30: "only on the magic wheel
	// do I hear a bunch of ticking noises as soon as it appears"), and the magic menu still showed the inventory wheel's
	// items ("when I go to inventory and magic, I still see all the same items").

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

	// an Equipment slot's key changed under the game: the new item's picture is loaded now (rows::ItemIcon keeps it for
	// the overlays), not at their next draw
	void PreloadIcon(int a_slot)
	{
		if (g_shown == Shown::kMagic || a_slot < 0 || a_slot >= inventory::kSlots) {
			return;
		}
		const auto id = inventory::Keys()[a_slot];
		if (id && !rows::ItemIcon(id)) {
			logger::info("wheels: slot {}'s picture not updated - no row has shown {} this session", a_slot + 1, inventory::NameOf(id));
		}
	}

	// every Equipment slot whose key moved since a_before
	void PreloadChangedIcons(const inventory::KeyMap& a_before)
	{
		const auto now = inventory::Keys();
		for (int k = 0; k < inventory::kSlots; ++k) {
			if (now[k] != a_before[k]) {
				PreloadIcon(k);
			}
		}
	}

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
			PreloadChangedIcons(before);
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
		PreloadChangedIcons(before);
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
			PreloadChangedIcons(original);
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
		PreloadChangedIcons(original);
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
				PreloadChangedIcons(before);
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
						PreloadChangedIcons(before);
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
					PreloadChangedIcons(before);
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
			PreloadChangedIcons(before);   // the Equipment wheel's pictures follow its keys: nothing stale left on it
			logger::info("wheels: {} arrow entr{} moved from the Equipment wheel to the Ammo wheel", moved, moved == 1 ? "y" : "ies");
		}
		return moved;
	}
}
