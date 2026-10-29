#pragma once

// ============================================================================================================
// Which of the two menus that own a wheel is open: the inventory (the Equipment wheel) or the magic menu (the
// Magic wheel) - the owner, 2026-09-29: "the wheel that pops up in the magic menu is the magic wheel and the
// inventory wheel in the inventory".
//
// Both are CommonUI activatable widgets (read in game 2026-09-29): WBP_OriginalMenu_Inventory_C (native
// VInventoryMenu) and WBP_ModernMenu_MagicMenu_C (native VLegacyMagicMenu); BP_OnActivated / BP_OnDeactivated pass
// through ProcessEvent, watched with pe::Watch. Their classes load the first time each menu opens, so the watch
// lands then; the menu being opened at that moment is taken as active. The Gamebryo menu mode (1 = gameplay)
// clears the state if a deactivation was missed.
// ============================================================================================================

namespace menus
{
	enum class Menu { kNone, kInventory, kMagic };

	void Tick();       // from the plugin's lazy thread: finds the classes once they are loaded and watches them
	Menu Active();     // game thread or any thread
	std::uint64_t MsSinceClosed();   // milliseconds since the Inventory or the Magic menu last deactivated (a huge number if never): the game still reports menu mode while a menu closes
	bool AnyOpen();    // the game is in menu mode (any menu: also containers and barter, which Active() reports as kNone)
	const char* Name(Menu a_menu);
	UE::UClass* ClassOf(Menu a_menu);   // the menu widget's class, once it has loaded (nullptr before)
	UE::UObject* Widget(Menu a_menu);   // the menu's widget (the instance that was activated last), nullptr when there is none
}
