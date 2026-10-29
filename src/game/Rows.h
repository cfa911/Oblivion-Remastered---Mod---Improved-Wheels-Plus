#pragma once

// ============================================================================================================
// The rows of the inventory and magic menu lists - which one is highlighted, and what each shows. Read in game
// 2026-09-29 with the UE4SS probe:
//   WBP_OriginalMenu_InventoryEntry_C   Properties : OriginalInventoryMenuItemProperties
//                                         { Name (FText), Icon (texture), bIsFavorite, form (UTESForm*: m_formID) ... }
//   WBP_ModernMenu_MagicEntry_C         Properties : LegacyMagicMenuItemProperties
//                                         { Name (FText), Icon (texture), bIsFavorite, InventoryIndex ... }  - no form
// Both fire BP_OnItemSelectionChanged(bool) through ProcessEvent as the highlight moves (watched with pe::Watch).
// A magic row carries no form, so its spell is the player's spell of that name. Game thread only.
// ============================================================================================================

namespace rows
{
	void Tick();                            // finds and watches the row classes as the menus first load them

	std::uint32_t HighlightedItem();        // inventory: the highlighted row's item formID (0 = none)

	// a container or barter list's highlighted row (the same row widget): the player's own item, if it is one
	std::uint32_t HighlightedPlayerItemInContainer();
	std::uint32_t HighlightedSpell();       // magic menu: the highlighted row's spell formID (0 = none / not a spell)

	UE::UObject* ItemIcon(std::uint32_t a_formID);    // the icon a row showed for it this session (nullptr = not seen)
	UE::UObject* SpellIcon(std::uint32_t a_formID);

	bool          PlayerHasSpell(std::uint32_t a_formID);   // the player knows that spell
	std::size_t   PlayerSpellCount();                       // how many spells the player knows (0 while the list is not built)
	std::uint32_t SpellByName(const std::string& a_name);   // the player's spell of that name (0 = none)
	std::string   SpellName(std::uint32_t a_formID);
	std::string   DisplayName(std::uint32_t a_formID);   // a spell the player made: the name the magic menu showed for it (kept per character in ImprovedWheelsPlus/Assignments/<character>.spells); "" = not seen yet

	// the favourites column (Favourites.cpp): every live inventory row now, the key its name reads (a string-table key,
	// else the text) and the item it shows; true from TakeInventoryRowsChanged when a row fired an event since the last ask
	std::vector<UE::UObject*> LiveInventoryRows();
	std::string               InventoryRowKey(UE::UObject* a_row);
	std::uint32_t             InventoryRowForm(UE::UObject* a_row);
	bool                      TakeInventoryRowsChanged();
	// where an inventory-list row sits: 0 the Inventory menu, 1 a container or barter menu's list of the player's own items, 2 a container or barter list of
	// the other side (the container's or the merchant's items: no star there)
	int                       InventoryRowSide(UE::UObject* a_row);
	UE::UClass*               InventoryRowClass();
	UE::UClass*               MagicRowClass();
	// the same for the magic menu's rows (their spell: the player's spell of the row's name)
	std::vector<UE::UObject*> LiveMagicRows();
	std::string               MagicRowKey(UE::UObject* a_row);
	std::uint32_t             MagicRowSpell(UE::UObject* a_row);
	bool                      TakeMagicRowsChanged();

	std::string Status();                   // for the self-check
}
