#pragma once

// ============================================================================================================
// What other plugins say, through their exports (looked up by name; a plugin without the export, or not loaded, reads
// as "no" and the wheel's rules stay as they were). Game thread.
// ============================================================================================================

namespace interop
{
	// The Apocrypha Menu Framework's window is open (AMF_IsMenuOpen, AMF OR 1.0.5+): the wheels stand down, as the
	// game's own wheel never opens there (the owner, 2026-09-30).
	bool AmfMenuOpen();

	// Tween Menu's own menu is open (TweenMenu_IsOpen): it draws in gameplay, so no game menu reports it (the owner,
	// 2026-09-30: "The ammo wheel shouldn't be able to be called during the tween menu").
	bool TweenMenuOpen();

	// Menus Revamped's controller keyboard is up: it has the controller, the menu rules stand aside (X on a favourite
	// would be swallowed as 'not a drop' before the keyboard could use it as Delete).
	bool ControllerKeyboardOpen();

	// SearchLight (Menus Revamped) has the typing in a bar of its own, or its controller keyboard is up: the keys are still
	// down for GetAsyncKeyState, so a typed digit or R/Z/C/F must not reach the wheel's keys.
	bool SearchLightTyping();

	// the mouse is over a SearchLight bar: a click there is the bar's (it starts the typing)
	bool SearchLightCursorOverBar();

	// the old Inventory Search / Magic Search plugins are loaded (their search boxes are text boxes the keyboard looks for)
	bool OldSearchPluginsLoaded();
}
