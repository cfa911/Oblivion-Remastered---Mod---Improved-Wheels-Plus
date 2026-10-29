#pragma once

#include <Xinput.h>   // types and constants only - nothing is linked or loaded
#include <nlohmann/json.hpp>

// ============================================================================================================
// The Ammo wheel: eight assigned arrow cells, available independently of the equipped weapon.
// The legacy edge shortcut remains compact for up to five entries; larger sets open the full ring.
// Built from that description; Wheeler Refined's own ammo wheel is GPL-3.0-only and was not read (rule 62).
//
//   * The wheel's arrows are the Ammo wheel's eight slots (wheels::AmmoSlots): favouriting arrows (Y) puts them there,
//     never on the Equipment wheel, and arrows found on the Equipment wheel move over (Wheels.cpp).
//   * D-pad right (in gameplay, the game's own radial closed) opens it regardless of weapon; the game never sees that press.
//     Centred mode preserves the eight saved positions. Edge mode expands to a full ring above five entries.
//   * The right stick points at an arrow; D-pad up / down step. D-pad right again or A equips the pointed arrows
//     and closes; B closes without equipping.
//     While it is open the right stick, the D-pad, A and B go to the wheel only.
//   * Equipping runs on the TES thread (testhread::Post - logic library 7880: the equipment path traps every other
//     thread), Actor::EquipObject with the whole stack and no lock (logic library: EquipObject's last argument).
// ============================================================================================================

namespace ammo
{
	// the pad read (game thread): a_raw / a_pressed are the buttons as read, a_out what the game will see (the buttons
	// the wheel used are taken out of it, and a_pad's right stick is zeroed while the wheel is open). True when the wheel
	// took this read - the rest of the gameplay rules then leave it alone.
	bool Rewrite(XINPUT_GAMEPAD& a_pad, WORD a_raw, WORD a_pressed, WORD& a_out, bool a_gameplay, bool a_radialOpen);
	bool Open();
	void EquipArrows(std::uint32_t a_formID);            // put those arrows on (the TES thread)
	void PointAt(int a_entry);                          // the mouse is over that entry: it is the pointed one
	int  EntryUnderMouse(double a_x, double a_y);       // the entry at that spot (viewport units), -1 for none
	nlohmann::json State();
}
