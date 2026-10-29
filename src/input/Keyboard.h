#pragma once

#include <Xinput.h>   // types only

// ============================================================================================================
// The keyboard as a virtual controller (the owner, 2026-10-16: "Its better to do this all as virtual controller and map
// there. We are going to have a lot of issues if we don't do that"): the keys in the INI's [Keyboard] section press the
// controller control they are named for, laid over the real pad on the game thread, so every rule in Pad.cpp (the wheels,
// the assign panel, favouriting, clearing a slot, the ammo wheel) runs for them exactly as for a controller. With no pad
// connected the read is handed over as a connected pad while a bound key is down (and for a moment after).
// Slot keys activate the selected entry directly, without opening the radial or injecting controller input.
// ============================================================================================================

namespace keyboard
{
	// lays the keys that are down over a_pad; true when any bound key is active (so a pad-less read is worth handing over)
	bool Apply(XINPUT_GAMEPAD& a_pad);
	// Keyboard-toggle sessions use mouse pointing; an idle virtual stick must not rest-snap them.
	bool MouseOwnsRadial();
	// true when, with no pad connected, the game should be handed an idle connected pad all the time (the game polls a missing pad only
	// now and then, so the keys would otherwise reach the rules a few times a minute)
	bool PresentWhenNoPad();
	// the mouse position in the UI's viewport units (the unit the widgets are measured in); false when it cannot be read
	bool MouseUnits(double& a_x, double& a_y);
	// a click or assignment script is running: preserve its intended pointed slot
	bool ScriptRunning();
	// the radial opened with the native R key closes the way Esc or a second R closes it (B and the key-up of the held R); nothing when R is not held
	void CancelNativeWheel();
}
