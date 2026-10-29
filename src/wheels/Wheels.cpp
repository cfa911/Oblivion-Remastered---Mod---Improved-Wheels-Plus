// The wheels' interface (Wheels.h): queries, assigning from the menus, switching, the panel and radial events.
#include "wheels/WheelsInternal.h"

#include "core/Settings.h"
#include "game/Menus.h"
#include "game/Rows.h"
#include "game/SessionContext.h"

namespace wheels::detail
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

namespace wheels
{
	using namespace detail;

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
		PreloadChangedIcons(before);
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
			PreloadIcon(a_slot);
		}
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
}
