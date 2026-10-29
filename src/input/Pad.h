#pragma once

// ============================================================================================================
// The controller rules (the owner, 2026-09-29; PLAN.md "rulings"). The game reads the pad through its own
// XINPUT1_3!XInputGetState import (Steam Input answers it); that import slot is chained here the way the Oblivion
// menu framework's pad gate does it (the previous target is kept and called, so either can install first), and
// every read is rewritten on the game thread:
//
//   INVENTORY / MAGIC MENU
//     Y            -> ours: Favourite (the game never sees Y)
//     L3           -> delivered as Y (the game's sorting, which was on Y)
//     D-pad down   -> timed: released before 0.25 s = replayed as a tap (the list moves one row); held past it =
//                     a left-stick-click pulse (the game's own "Show Shortcuts" panel toggles, as L3 did)
//     D-pad L/R    -> with the panel showing: cycle the pointed slot's entry (the game never sees them)
//     B            -> with the panel showing: closes the panel (the next B leaves the menu as before)
//     A            -> the game's Assign Item, watched by wheels::AssignPressed (again on the same item = removed)
//   GAMEPLAY
//     D-pad down   -> the game's wheel button. A TAP (under 0.25 s) keeps it held for the game, so the wheel stays
//                     open; the next press lets go of it, and the game uses the pointed slot. A hold works as before.
//     with the radial open: D-pad L/R switch wheels, LT/RT cycle the pointed slot's entries, RB uses the pointed
//     slot now, LB removes its entry, B closes the wheel without using anything - none of them reach the game.
//     The right stick back at rest in the middle for uRestSnapMs points at no slot (the centre rest snap).
//   The ammo wheel's button (D-pad right) opens the ammo wheel (Ammo.h), which then takes the read.
//
// Never loads an XInput DLL (gate oblivion-plugin-never-loads-xinput): only the game's own import is used.
// ============================================================================================================

namespace pad
{
	bool Install();   // chains the import slot; call once at OBSE's post-load (idempotent)

	// Called on EVERY read, on the game thread, before anything else (connected pad or not). The plugin's
	// look-ups of engine objects run from here: done from a thread of our own while the engine was building
	// objects, a start-up crashed with UObjectArray's "another object exists at that index" (2026-09-29).
	using FrameCallback = void (*)();
	void SetFrameCallback(FrameCallback a_callback);

	bool        MagicPanelUp();

}
