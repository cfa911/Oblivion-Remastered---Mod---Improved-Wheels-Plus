// Using a slot: the radial's own close, RB / A at once, the game's quick-key press, the repair hammer from the Inventory.
#include "wheels/WheelsInternal.h"

#include "core/Settings.h"
#include "core/TextUtil.h"
#include "engine/PEHook.h"
#include "engine/TesThread.h"
#include "engine/Ui.h"
#include "game/Menus.h"
#include "game/Rows.h"

namespace wheels::detail
{
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
	constexpr std::uint32_t kRepairHammer = 0x0000000C;

	bool MiscTool(std::uint32_t a_id)
	{
		return a_id == kRepairHammer;
	}

	// the controller function that presses the open-inventory action ("OpenInventoryMenuInput_Pressed" in game 1.512.105.0); found once by name
	std::string OpenInventoryFunction(UE::UObject* a_pc)
	{
		static std::string found;
		static bool        looked = false;
		if (looked) return found;
		looked = true;
		for (UE::UStruct* st = a_pc ? static_cast<UE::UStruct*>(a_pc->GetClass()) : nullptr; st && found.empty(); st = st->superStruct) {
			auto* f = *reinterpret_cast<UE::UField* const*>(&st->children);
			for (int n = 0; f && n < 8000; ++n, f = f->next) {
				if (pe::Utf8(f->GetClass()->GetFName().ToString()) != "Function") continue;
				const std::string name = pe::Utf8(f->GetFName().ToString());
				const std::string l = textutil::Lower(name);
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
			logger::info("wheels: USE result slot {} ({}) - {}: {} -> {} {} ms after the press - {}", n, g_use.how, name, inventory::Describe(g_use.before),
				inventory::Describe(st), ms, st.worn ? "equipped at once through Actor::EquipObject" : MiscTool(g_use.id) ? (menus::AnyOpen() ? "a menu is open (the hammer's repair menu?)" : "NO menu opened") : "NOT worn: the equip did not take");
			return;
		}
		if (!g_use.pressed) {
			if (quickkeys::RadialOpen()) {
				if (now - g_use.armed > 3s) {
					g_use.on = false;
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
					logger::info("wheels: USE slot {} - a menu is open 1.5 s after the radial closed; {} not used", n, name);
				}
				return;
			}
			const auto keys = inventory::Keys();
			const auto st = inventory::StateOf(g_use.id);
			if (keys[static_cast<std::size_t>(g_use.key)] != g_use.id) {
				g_use.on = false;
				logger::info("wheels: USE slot {} - the game's key {} no longer holds {}; nothing pressed", n, n, name);
				return;
			}
			if (st != g_use.before) {
				// the radial's own close used it after all (the choice was cleared, so it should not have): never twice
				g_use.on = false;
				logger::info("wheels: USE slot {} - the radial's own close already used {} ({} -> {}); the key is not pressed again", n, name,
					inventory::Describe(g_use.before), inventory::Describe(st));
				return;
			}
			const bool ok = quickkeys::PressQuickKey(g_use.key);
			logger::info("wheels: USE slot {} - the game's own quick key {} pressed for {} (VEnhancedAltarPlayerController Quick{}Input_Pressed / _Released){}",
				n, n, name, n, ok ? "" : " - the call FAILED");
			if (!ok) {
				g_use.on = false;
				logger::info("wheels: USE slot {} - the game's key press could not be made; {} not used", n, name);
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
			logger::info("wheels: USE result slot {} ({}) - {}: {} -> {}{} - used through the game's own quick key {}", n, g_use.how, name,
				inventory::Describe(g_use.before), inventory::Describe(st), menu ? ", a menu opened" : "", n);
			return;
		}
		if (Equippable(g_use.id) && st.carried && !st.worn) {
			logger::info("wheels: USE result slot {} ({}) - {} unchanged 0.7 s after the game's key press ({}); FALLBACK: equipped through Actor::EquipObject on the TES thread",
				n, g_use.how, name, inventory::Describe(st));
			inventory::EquipKeyed(g_use.id);
			return;
		}
		logger::info("wheels: USE result slot {} ({}) - {} unchanged 0.7 s after the game's key press ({}) - nothing visible changed", n, g_use.how, name,
			inventory::Describe(st));
	}
}

namespace wheels
{
	using namespace detail;

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
		inventory::EquipAmmo(slot.entries[static_cast<std::size_t>(slot.active)]);
	}
}
