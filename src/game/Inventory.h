#pragma once

// ============================================================================================================
// The game's own item quick keys, read and moved. An inventory item is on quick key N when one of its extra data
// lists carries ExtraQuickKey (type 0x55) with keyID N at +0x18 (read in game 2026-09-29 with TestBench re.quickkeys;
// the UI shows slot N+1). The game keeps ONE item per key, and the save stores it - so the active entry of each
// Equipment slot is simply the item carrying that key, and the wheel's other entries are ours (Wheels.cpp).
//
// Every call here walks or changes the player's inventory: game thread only (the controller read is on it).
// ============================================================================================================

namespace inventory
{
	constexpr int kSlots = 8;

	// formID of the item carrying each key (0 = none)
	using KeyMap = std::array<std::uint32_t, kSlots>;
	KeyMap Keys();

	bool Ready();                                     // the inventory list can be read now
	bool Has(std::uint32_t a_formID);                 // the player carries at least one
	std::string NameOf(std::uint32_t a_formID);
	std::uint32_t FindByName(const std::string& a_name);   // a carried item whose full name (a "LOC_FN_" key, or a custom name) matches

	// carried items whose full name is a placeholder for a name the player typed ("UI_UserInputText_..."): their id, count and weight
	struct Carried
	{
		std::uint32_t id = 0;
		int           count = 0;
		float         weight = 0.0f;
	};
	std::vector<Carried> CarriedWithTypedNames();

	int           PositionOf(std::uint32_t a_formID);   // where the item sits in the player's item list (-1 = not carried)
	std::uint32_t AtPosition(int a_position);          // the item at that place of the list (0 = none)

	void ClearKey(std::uint32_t a_formID);            // takes every quick key off that item
	void ClearSlot(int a_key);                        // takes quick key N off whichever item has it
	void SweepNativeSpellKeys();                      // empties every native hotkey slot that holds a SPELL (items stay); TES thread only, cheap enough for every pass
	bool ClearAllNativeKeys();                        // the game's own clear of every native hotkey, spells too (TES thread only; false = not done)
	bool SetKey(std::uint32_t a_formID, int a_key);   // puts quick key N on that item (and off any other item)

	std::string PlayerName();                         // the character the wheels are saved under

	// what can be seen of an item from outside: how many are carried and whether one is worn (a use of its quick key
	// changes one of the two - equipped, unequipped, drunk, read off the stack). Reads only: any thread.
	struct State
	{
		bool carried = false;
		int  count = 0;
		bool worn = false;
		bool operator==(const State&) const = default;
	};
	State StateOf(std::uint32_t a_formID);
	std::string Describe(const State& a_state);       // "x1, worn" / "not carried"

	// puts the item on (its quick-keyed instance), on the TES thread: RB / A on the inventory wheel, at once
	void EquipKeyed(std::uint32_t a_formID);

	// takes the item off (the worn instance, the quick-keyed one first), on the TES thread: RB / A / closing on the slot of an item already worn
	void UnequipKeyed(std::uint32_t a_formID);
	void ToggleKeyed(std::uint32_t a_formID);   // worn-state decision made at TES dispatch for repeated quick uses
}
