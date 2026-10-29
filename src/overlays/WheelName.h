#pragma once

// ============================================================================================================
// The wheel's name, shown above whichever wheel is on screen (the owner, 2026-09-30: "The wheel menus aren't named in
// the inventory when they should be"; and, the same night, not knowing an empty Magic wheel for the Magic wheel):
// "Inventory Wheel" or "Magic Wheel" over the HUD radial (the active wheel) and over a menu's assign panel (the
// inventory's is the Inventory wheel, the magic menu's the Magic wheel). The wheel widget on screen is measured from
// its cached geometry (as Minimap Menu places the compass), and a runtime label of the game's own text prefab sits
// centred above it. Game thread, from the controller read; ten looks a second.
// ============================================================================================================

namespace wheelname
{
	void Tick();
}
