#pragma once

// ============================================================================================================
// The Magic wheel as a wheel of its own (the owner, 2026-09-30: "creating a second wheel, not just a renamed wheel, a
// second entirely different wheel, similar to how we created the ammo wheel. And this one needs to be specific to the
// magic inventory menu"). Its own image with its own instance of the game's wheel material (MIC_UI_QuickKeys), laid
// exactly over the game's wheel while the Magic wheel is up - the magic menu's panel, or the HUD radial with Magic
// active - with the game's wheel image hidden under it. Its eight slots are the Magic wheel's spells (empty ones drawn
// empty); the selector follows the game's own. The game's wheel, its pictures and its view model are never written, so
// nothing on it clicks (the old approach pushed the spells into the shared view model: every slot it redrew ticked).
//
// The inventory wheel (the game's own keys) holds no magic (the owner, 2026-09-30: "the inventory wheel menu still
// having magic on it"): a key that holds no item - a spell the game kept from before - is drawn empty on the game's
// wheel, and closing the radial on it uses nothing.
// ============================================================================================================

namespace magicwheel
{
	void Tick();                    // every controller read (game thread)
	bool IsSpellKey(int a_slot);    // the game's key a_slot (0-7) holds no item but shows a picture: a spell, off this wheel
	std::string Status();
}
