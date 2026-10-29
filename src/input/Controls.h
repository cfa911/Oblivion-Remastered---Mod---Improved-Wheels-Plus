#pragma once

// ============================================================================================================
// The ammo wheel's row on the game's Controller Controls page (the owner, 2026-09-29: "We would have to add a control
// row in the controls page for it. It would default to D-pad right while holding a bow"; 2026-09-30: "make sure that
// you build the system row and that the game recognizes the key presses properly"). Tween Menu for Oblivion's first
// Controls row (abcfc12) is the model, with its later lessons (logic library 7718, 7736, 7772):
//   * IA_IWM_AmmoWheel, a runtime InputAction (rooted - an unrooted one was collected within seconds in Tween Menu),
//     mapped in IMC_Game_Default to the button from the INI ([AmmoWheel] uButton, D-pad right);
//   * an "Ammo Wheel" row in DT_Modern_Settings_GamepadRebind_Data's RebindSettings, after the game's quick keys row,
//     modelled on a game row (type, category), its rebind data pointing at our action and IMC_Game_Default;
//   * the button our action has in the context is read back every 60 ticks and on the first gameplay tick after a menu:
//     a rebind on the Controls page is kept in the INI (the game's own save cannot restore a runtime action), and a
//     context the game re-applied without our action gets it back (7718), with a rebuild whose parameters are set (7736).
// The press itself is still read on the XInput import (Pad.cpp / Ammo.cpp) with the bound button's mask - the row makes
// the button rebindable where the game's other buttons are.
// Game thread only (the controller-read frame callback).
// ============================================================================================================

namespace controls
{
	void Tick();   // every ~200 ms from the frame callback

	struct Status
	{
		bool        actionCreated = false;
		bool        rowAdded = false;
		std::string boundKey;   // the Gamepad_ key our action has now
		std::string problem;
	};
	Status GetStatus();
}
