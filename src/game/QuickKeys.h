#pragma once

// ============================================================================================================
// The game's Quick Keys radial, observed. Facts from the runtime research of 2026-09-26 (plans\perfected-wheeler-
// oblivion\RESEARCH.md, "Settled at runtime"):
//
//   * The HUD radial is ONE widget for the session, class WBP_ModernMenu_QuickKeys_C, shown and hidden. The
//     inventory and the magic menu build their OWN instance of the same class - the assign panel on the right of
//     the menu (shown with the left stick click, "Show Shortcuts").
//   * What passes through UObject::ProcessEvent: OnVisibilityChangedEvent(4) as an instance shows, "Update Key
//     Index"(N) each time the pointer crosses into a slot (-1 = none), OnVisibilityChangedEvent(1) as it hides - on
//     the HUD radial the game uses the chosen slot on that close. The chosen slot is the view model's KeyIndex.
//   * KeyIndex numbering: clockwise, 45 degrees a slot, 0 top-left, 1 top ... 7 left.
//
// Observed through pe::Watch (a vtable entry swap - coexists with UE4SS). An instance that shows while the
// inventory or the magic menu is active is that menu's PANEL; otherwise it is the HUD RADIAL.
// ============================================================================================================

namespace quickkeys
{
	enum class Event
	{
		kOpened,        // the HUD radial appeared
		kPointed,       // a slot is under the pointer (radial or panel; slot = -1 when it left every slot)
		kClosed,        // the HUD radial went away; slot = the slot the game uses
		kPanelOpened,   // a menu's assign panel appeared
		kPanelClosed,   // a menu's assign panel went away
	};

	using Listener = void (*)(Event a_event, int a_slot);

	// Call once, after OBSE's post-load. The watch is installed lazily, once the widget's class exists.
	void Install(Listener a_listener);
	void Tick();

	bool RadialOpen();   // the HUD radial is showing
	std::uint32_t OpenCount();   // how many openings of the radial have been reported (a reopen before any close report still counts)
	bool PanelOpen();    // a menu's assign panel is showing
	// one of the game's message boxes is up (WBP_ModernMenu_Message: "You cannot drink any more potions", "apply the poison?", "already poisoned"...):
	// a click or A then answers it and must not be taken as a choice on the wheel behind it. Looked up when a choice is about to be made, not every read.
	// Drawing may defer discovery of a new instance up to100ms; input keeps immediate discovery.
	bool GameMessageUp(bool a_immediate = true);
	// any wheel widget's own visibility says shown (UWidget::IsVisible). The magic menu's panel reports no visibility
	// change through ProcessEvent, so in the magic menu this is how its panel is known to be up. Game thread.
	bool AnyWheelVisible();
	UE::UObject* VisibleWheel();   // the first wheel widget whose own visibility says shown, or nullptr
	bool Animating();         // the wheel changed visibility within the last ~0.9 s: it is popping up or out, so overlays follow it every read
	void BoostAnimations();   // every read: while the wheel is opening or closing, its widget animations play faster (INI uAnimSpeedPercent)
	int  PointedSlot();  // the slot under the pointer on whichever is showing, -1 for none
	void CancelChoice(); // B on the HUD radial: the view model points at no slot, so the close uses nothing
	// the centre rest snap: CancelChoice, the Blueprint's pointed key (QuickKeyID / CurrentScaledKeyID / HoveredKeyID)
	// set to -1 and the selector arrow hidden (SelectorArrowAlpha 0 on the wheel's material). HoldCleared keeps the arrow
	// hidden while the snap lasts (call every read); RestorePointer puts it back (the stick points again, or the wheel closed).
	void ClearPointer();
	void HoldCleared();
	void RestorePointer(const char* a_why);
	// the wheel points at key a_key (0-7) again - widget highlight and view model - when the stick came back to a slot
	// after a rest snap and the game did not say so itself
	void Repoint(int a_key);
	// the game's own direct use of quick key a_key (0-7) - the player controller's Quick<N>Input_Pressed / _Released,
	// what the number keys 1-8 run. Game thread. False when the call could not be made.
	bool PressQuickKey(int a_key);

	// The eight slot pictures read by the active material overlay (MagicWheel.cpp).
	using Icons = std::array<UE::UObject*, 8>;
	Icons ReadIcons();
	void RequestRedraw();   // force overlays to refresh after verified native cleanup
	bool  TakeRedrew();     // the game redrew its own pictures since the last call (the overlay puts its own right at once)

	struct Status
	{
		bool hookInstalled = false;
		bool widgetClassFound = false;
		bool viewModelFound = false;
		bool open = false;        // HUD radial
		bool panelOpen = false;   // a menu's panel
		int  pointedSlot = -1;
		int  lastChosenSlot = -1;
		std::uint32_t opens = 0;
		std::string   problem;
	};
	Status GetStatus();
}
