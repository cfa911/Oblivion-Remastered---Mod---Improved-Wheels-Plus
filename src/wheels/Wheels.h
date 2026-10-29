#pragma once

#include "game/Menus.h"

#include <unordered_set>

// ============================================================================================================
// The three wheels (Equipment, Magic, Ammo) x eight slots x up to uEntriesPerSlot entries - the owner's rulings of
// 2026-09-26 and 2026-09-29 (PLAN.md). The controls (Pad.cpp) call these; every call is on the game thread.
// Slots are the game's key numbers 0-7 (the UI shows 1-8). Saved per character in
// OBSE\Plugins\ImprovedWheelsPlus\Assignments\<character>.iwp.
//
// EQUIPMENT: the active entry of a slot is the item carrying the game's quick key (Inventory.h), so the game's
// own radial, its "use" on close and the save all keep working untouched; the other entries are ours.
//   Assign (the game's A in the inventory's panel) is watched, not replaced: before the game handles the press the
//   pointed slot's item is taken off the key, then the result is read back -
//     the same item came back   -> it was pressed again on the same item: REMOVED from the slot
//     another item arrived      -> ADDED as a new entry (the old ones stay; it becomes the active one)
//     an inactive entry arrived -> REMOVED (pressing A on anything already in the slot takes it out)
//     nothing happened          -> the item goes back on its key
// MAGIC: entirely ours - where the game keeps a spell's own quick key was never found (RESEARCH.md), so the game's
//   keys stay the Equipment wheel's. The Magic wheel is its own widget over the game's wheel (overlays/MagicWheel;
//   the game's view model is never written), A in the magic menu's panel is ours, and choosing a Magic slot on the
//   radial makes its spell the one the player casts.
// AMMO: entirely ours, one arrow kind a slot. Inventory arrows show the Ammo assign panel; A assigns the chosen
//   one of eight cells without touching native hotkeys. The favourite shortcut also puts arrows here, never on the
//   Equipment wheel, and arrows found on the Equipment wheel (an older save, or the game's own assign) move over and
//   take the game's key with them off the radial. Each wheel holds only its own kind: items, spells, arrows.
// ============================================================================================================

namespace wheels
{
	enum class Wheel { kEquipment, kMagic, kAmmo };

	const char* Name(Wheel a_wheel);
	Wheel Active();                            // the wheel the HUD radial shows

	void Favourite(menus::Menu a_menu);
	// a keyboard slot key in the Inventory / Magic menu: the highlighted item or spell to slot N of its wheel directly (pressed again on an item the slot
	// already holds: it comes off), with no controller input played - the game switched its button pictures to the controller's for the played L3 / stick / A
	void AssignHighlighted(menus::Menu a_menu, int a_slot);        // Y in the inventory / magic menu: the highlighted item or spell on/off its wheel
	void NoteLeaving(std::uint32_t a_formID);   // a favourite is being dropped or sold: it leaves the wheels once none is carried
	bool IsFavourite(std::uint32_t a_formID);  // on the Equipment or Ammo wheel - such an item cannot be dropped, sold or handed over
	// every item on the Equipment or Ammo wheel, and a count that moves whenever any wheel changes (the favourites column)
	std::unordered_set<std::uint32_t> Favourites();
	std::uint32_t Generation();
	bool Ready();   // false while missing-file native cleanup has not completed
	bool CanFavourite(std::uint32_t a_formID);   // any carried item (armour and clothing: a favourite, never on the wheel)
	void ToggleItem(std::uint32_t a_formID);   // the star in the inventory's favourites column: as Y on that item
	std::unordered_set<std::uint32_t> MagicFavourites();   // every spell on the Magic wheel
	// each slot (key order): how many entries LT / RT can reach, and which of them (1-based) the slot shows now - 0 when
	// it shows none. The slot counters ("2/3").
	struct SlotCount
	{
		int position = 0;
		int count = 0;
	};
	std::array<SlotCount, 8> SlotCounts(Wheel a_wheel);
	int SlotCap();
	void ToggleSpell(std::uint32_t a_formID);  // the star in the magic menu's favourites column: as Y on that spell

	// the Magic wheel's eight slots (key order): each slot's active spell, 0 = empty (MagicWheel.cpp draws them)
	std::array<std::uint32_t, 8> MagicSlots();

	// the Ammo wheel's eight slots, the top one first: the arrows the player carries (0 = empty, or not carried)
	std::array<std::uint32_t, 8> AmmoSlots();
	int                          AmmoSlotAtRing(int a_ring);        // the radial's Ammo view: the Ammo slot whose arrows sit at that ring position (0-7), -1 when none
	Wheel AssignmentWheel(menus::Menu a_menu);   // Inventory arrows use Ammo; other rows use Equipment
	void AssignPressed(menus::Menu a_menu, int a_slot);   // A with a menu's panel showing (a_slot = pointed key, -1 none)
	void Tick(bool a_assignHeld);              // every controller read: settles a pending Equipment assign
	void CycleEntry(Wheel a_wheel, int a_slot, int a_dir);   // step the active entry of a slot (D-pad L/R in a panel, LT/RT on the radial)
	void ClearSlot(Wheel a_wheel, int a_slot);   // X held on a pointed slot of a panel: every entry of the slot comes off the wheel
	void SwitchWheel(int a_dir);               // D-pad L/R on the HUD radial
	void KeepActive();                 // the radial that opens in the next moment keeps the wheel that is active (the keyboard's own presses)
	void SetActive(Wheel a_wheel);             // a wheel's own button or key: the radial opens (or shows) on that wheel
	// RB (or A) on the HUD radial's Equipment wheel (key 0-7): equipment goes on at once through Actor::EquipObject on
	// the TES thread (an item already worn: nothing, logged); anything else gets the game's own quick-key press once the
	// radial has closed (on nothing). The result is read back from the item and logged ("USE result")
	void QuickUse(int a_slot);   // active wheel slot directly, no radial/input simulation
	void UseNow(int a_slot, const char* a_how, bool a_stayOpen = false);   // a_stayOpen: a consumable is used at once and the radial stays up
	void UseMagic(int a_slot);                 // the radial closed on a slot of the Magic wheel: that spell becomes the one cast
	// While the radial closes on a Magic or Ammo slot, the game's own close would use the item on ITS key of that slot (a potion on
	// the Equipment wheel's slot 3 as a spell of slot 3 was chosen): the key is taken off that item until the radial has closed.
	bool StaysOpenAfterUse(int a_slot);   // bStayOpenAfterUse and the item on the game's key of that slot is equipment (weapon, armour, clothing, light): the radial stays open after its use
	void HoldKey(int a_slot);
	void ReleaseKey();
	void UseAmmo(int a_slot);                  // the radial closed on a slot of the Ammo view: those arrows go on

	// what the wheel's pictures show
	void PanelShown(menus::Menu a_menu);       // a menu's panel came up: Magic in the magic menu; Inventory arrows use Ammo
	void PanelHidden();
	void RadialShown();
	void RadialHidden();
}
