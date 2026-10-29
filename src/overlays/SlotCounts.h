#pragma once

#include "wheels/Wheels.h"

// ============================================================================================================
// Each inventory-wheel slot's counter: which of its items the slot shows now, out of the items LT / RT can reach, "2/3"
// (the owner, 2026-10-01: "the numbers for which item I'm selecting isn't changing whether I'm on one out of five, two out
// of five" - it had shown the count against the cap, "3/5", since 2026-09-30). An empty slot shows nothing.
// Eight labels of the game's own text prefab, laid over the game's wheel picture while the inventory wheel is up (the
// inventory's panel, or the HUD radial on the Equipment wheel), each on its slot circle. MagicWheel.cpp, which already
// finds and measures that picture, drives it.
// ============================================================================================================

namespace slotcounts
{
	void Show(UE::UObject* a_gameWheelImage, wheels::Wheel a_wheel = wheels::Wheel::kEquipment);   // the game's wheel picture on screen now; the counts of that wheel (Equipment or Magic)
	void Hide();
	// The Magic wheel's centre name, over the wheel (the game's own centre text sits under the Magic wheel's picture, dim and
	// hard to read): set by WheelName each look; "" = none. Shown only while the Magic wheel's counts are.
	void SetCentre(const std::string& a_text);
}
