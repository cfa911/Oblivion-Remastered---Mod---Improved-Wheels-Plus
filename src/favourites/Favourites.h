#pragma once

// ============================================================================================================
// The favourites column in the inventory (the owner, 2026-09-30: "I still don't see the favorites column in the
// inventory interface ... Maybe we can build it just like we did the buttons for the SLS mod", then: "each item have a
// button. In its column with an empty star shape then the star shape gets filled to be white whenever it's favorited
// after they press said button"). Every inventory row (WBP_OriginalMenu_InventoryEntry_C) gets one more column at the end
// of its inv_entry_horizontal: the game's own empty star (T_UI_star_default_D) over a white fill of ours
// (ImprovedWheelsPlus\FavouriteStarFill.png, shown only for a favourite), and the game's invisible button over both - a
// click toggles the favourite as Y does. Rows are reused for other items as the list scrolls: each row keeps its column,
// and only a row whose item or favourite state changed is redrawn (a check every 100 ms while the inventory is open).
// "Favourite" is the owner's term: on the Equipment or Ammo wheel (never the game's own bIsFavorite).
// ============================================================================================================

namespace favourites
{
	void Tick();   // every frame (the game thread)
	std::string Status();
}
