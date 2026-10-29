#include "input/Pad.h"
#include "core/ReadMemo.h"
#include "input/RadialSelection.h"
#include "input/CloseEpoch.h"
#include "input/Interop.h"

#include "ammo/Ammo.h"
#include "input/Keyboard.h"
#include "overlays/WheelName.h"
#include "game/Inventory.h"
#include "overlays/MagicWheel.h"
#include "game/Menus.h"
#include "game/QuickKeys.h"
#include "game/MessageDiscovery.h"
#include "game/Rows.h"
#include "core/Settings.h"
#include "slowtime/SlowTime.h"
#include "wheels/Wheels.h"

#include <Xinput.h>   // types and constants only - nothing is linked or loaded

#include <numbers>

namespace pad
{
	namespace
	{
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		using Clock = std::chrono::steady_clock;

		constexpr auto  kHoldThreshold = 250ms;   // the Skyrim Perfected Wheeler's ToggleHoldThreshold (0.25 s)
		// A tap let go before the wheel has shown stays held for the game this long at most, waiting for it to show.
		// The wheel shows a few frames after the press - on a Steam Deck (30-40 fps, Proton) often after a quick tap is
		// already up, and the game then finished its own tap and closed the wheel ~0.3 s later (SaintAnhel, 2026-10-02).
		constexpr auto  kPendingLatchLimit = 600ms;
		constexpr int   kPulseReads = 4;          // a synthetic press is held for this many reads, then let go
		constexpr BYTE  kTriggerOn = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;   // 30

		XInputGetState_t g_previous = nullptr;    // what the game's import pointed at before us (Steam's, or AMF's gate)
		std::atomic<bool> g_installed{ false };
		std::string g_previousTarget;
		std::atomic<FrameCallback> g_frameCallback{ nullptr };

		// The game's own reads come from its main thread (the one OBSE loads plugins on and the one ProcessEvent runs
		// on). Another thread reads through the same import too (seen 2026-09-29: a queued A press was consumed on it and
		// ran the assign there) - it is passed straight through, untouched, so every rule and every inventory change
		// stays on the game thread.
		DWORD g_gameThread = 0;
		// ---- state, touched only inside the read (the game thread) ----
		WORD   g_prevRaw = 0;
		BYTE   g_prevLT = 0, g_prevRT = 0;
		WORD   g_prevOutButtons = 0;
		BYTE   g_prevOutLT = 0, g_prevOutRT = 0;
		DWORD  g_packetOffset = 0;
		menus::Menu g_prevMenu = menus::Menu::kNone;

		// inventory / magic menu: D-pad down timed as tap or hold
		bool              g_menuDownHeld = false;
		bool              g_menuDownFired = false;
		Clock::time_point g_menuDownAt{};

		// gameplay: the wheel button latched open by a tap
		bool              g_latched = false;
		bool              g_suppressDown = false;   // let the game see D-pad down UP until the physical button is released
		Clock::time_point g_gameDownAt{};
		bool              g_pendingLatch = false;   // tapped before the wheel showed: held for the game until it does
		bool              g_downInMenu = false;     // a menu (not the wheel - it only opens after the press) was up at the press
		Clock::time_point g_pendingSince{};
		bool              g_sawRadialOpen = false;  // the radial's open edge, for the press-to-shown time in the log
		int  g_pulseDown = 0;   // reads left of a replayed D-pad down tap
		bool g_swallowB = false;   // B was used to back out of the panel / radial: the game never sees it, until let go
		int  g_centreStick = 0;    // reads left with the right stick held centred (a cancelled radial must not point anywhere)
		int g_pulseLS = 0;     // reads left of a synthetic left stick click (toggles the menu's assign panel)

		bool g_forceClose = false;   // a use on the Equipment wheel is waiting for the radial to close; if it has not closed shortly, B closes it
		ULONGLONG g_forceCloseAt = 0;
		bool      g_leftToGame = false;   // the game keeps the radial open on purpose: its buttons go to the game until it closes
		int       g_forceTries = 0;   // B pulses sent for this use: a radial the game keeps open on purpose (a message such as 'You cannot drink any more potions right now' waiting for A) must not be pulsed forever
		int g_bPulse = 0;
		bool g_closingMagic = false;   // the radial is closing on a Magic slot: the stick held centred, no slot pointed, until it has closed
		closeepoch::Epoch g_closeEpoch;   // the close ends early when the wheel button goes down again or the radial reports another opening (IWP-024)
		bool g_swallowA = false;     // A was ours (the magic menu's panel): the game never sees it, until let go
		bool g_swallowX = false;     // X (drop) on a favourite: the game never sees it, until let go
		bool g_panelX = false;       // X went down on a pointed slot of an assign panel (held 0.6 s clears the slot)
		bool g_panelXFired = false;
		Clock::time_point g_panelXAt{};

		// the centre rest snap on the HUD radial (the owner, 2026-09-29: "we'd have to add the center rest snap feature or
		// you might select items mistakenly"): once the right stick has pointed, bringing it back to rest in the middle for
		// uRestSnapMs points at no slot, so letting go of the wheel button then uses nothing
		using radialselection::kStickOn;
		using radialselection::kStickRest;
		bool              g_radialWasOpen = false;
		bool              g_radialAway = false;     // the stick has pointed since the last rest
		bool              g_radialResting = true;
		Clock::time_point g_radialRestSince{};
		bool              g_snapped = false;        // a rest snap cleared the wheel and the stick has not pointed it again yet
		int               g_unreported = 0;         // reads with the stick out and no slot reported since the snap

		// the key (0-7) the right stick points at
		int KeyFromStick(const XINPUT_GAMEPAD& a_pad)
		{
			return radialselection::KeyAt(a_pad.sThumbRX / 32767.0, a_pad.sThumbRY / 32767.0);
		}

		void RestSnap(const XINPUT_GAMEPAD& a_pad, Clock::time_point a_now)
		{
			const bool open = quickkeys::RadialOpen();
			if (open != g_radialWasOpen) {
				g_radialWasOpen = open;
				g_radialAway = false;
				g_radialResting = true;
				g_radialRestSince = a_now;
				quickkeys::RestorePointer(open ? "the wheel opened" : "the wheel closed");   // a clear on a close hid the arrow: back for every opening
				g_snapped = false;
				g_unreported = 0;
			}
			if (open && keyboard::MouseOwnsRadial()) {
				if (g_snapped) quickkeys::RestorePointer("mouse owns the keyboard-toggle wheel");
				g_snapped = false;
				g_unreported = 0;
				g_radialAway = false;
				g_radialResting = true;
				g_radialRestSince = a_now;
				return;
			}
			if (!open || !settings::Get().centreRestSnap || g_closingMagic) {
				return;
			}
			const double m = std::hypot(a_pad.sThumbRX / 32767.0, a_pad.sThumbRY / 32767.0);
			if (m >= kStickOn) {
				g_radialAway = true;
				g_radialResting = false;
				// A fresh outward stick position is authoritative in this input read. The game's
				// pointed event arrives later; waiting for it dropped a quick A after rest.
				if (g_snapped) {
					g_snapped = false;
					g_unreported = 0;
					quickkeys::Repoint(KeyFromStick(a_pad));
					quickkeys::RestorePointer("the stick points again");
				}
			} else if (g_snapped) {
				quickkeys::HoldCleared();   // the stick still rests: the arrow stays hidden
			}
			if (m < kStickRest) {
				if (!g_radialResting) {
					g_radialResting = true;
					g_radialRestSince = a_now;
				}
				if (g_radialAway && a_now - g_radialRestSince >= std::chrono::milliseconds(settings::Get().restSnapMs)) {
					g_radialAway = false;
					if (quickkeys::PointedSlot() >= 1) {
						logger::info("pad: the right stick came back to rest - the wheel points at no slot (centre rest snap)");
						quickkeys::ClearPointer();   // the choice AND the wheel's own highlight
						g_snapped = true;
						g_unreported = 0;
					}
				}
			}
		}

		// The wheel (HUD radial and menu panels alike) numbers its slots 1-8 as drawn, 1 at the top: key = number - 1
		// (read 2026-09-29: pointing at the game's key 0 reported 1, key 7 reported 8; 0 / -1 = none)
		int g_liveStickKey = -1;   // current HUD read, never a menu-panel or mouse selection

		int PointedKey()
		{
			if (g_liveStickKey >= 0) return g_liveStickKey;
			// after a rest snap nothing is pointed until the stick points again, whatever the game re-reports (2026-10-01:
			// LT / RT right after a snap still cycled the old slot, and A 14 ms after one used it)
			if (g_snapped && quickkeys::RadialOpen()) {
				return -1;
			}
			const int p = quickkeys::PointedSlot();
			return p >= 1 && p <= 8 ? p - 1 : -1;
		}

		void SetMagicPanel(bool a_up)
		{
			if (quickkeys::MagicPanelUp() == a_up) {
				return;
			}
			quickkeys::SetMagicPanelUp(a_up);
			if (a_up) {
				wheels::PanelShown(menus::Menu::kMagic);
			} else {
				wheels::PanelHidden();
			}
		}

		// the HUD radial is closing on a slot of the Magic wheel: the game must use nothing, the spell is ours to set
		void CloseOnAmmo(int a_slot)
		{
			slowtime::CloseRequested();
			quickkeys::ClearPointer();   // the game's own pointed key too: its close must use nothing
			wheels::HoldKey(a_slot);     // and nothing on its key of that slot to use if it points there again
			wheels::UseAmmo(a_slot);
			g_closingMagic = true;   // held clear until the radial has closed
			keyboard::CancelNativeWheel(); // release the native R toggle too; removing the virtual LB alone cannot close it
			logger::info("pad: Ammo selection requested native R release");
		}

		void CloseOnMagic(int a_slot)
		{
			slowtime::CloseRequested();
			quickkeys::ClearPointer();   // the game's own pointed key too: a potion on the same slot of its own wheel was used by its close
			wheels::HoldKey(a_slot);
			wheels::UseMagic(a_slot);
			// the radial takes a moment to close and the stick still points meanwhile: the game pointed the slot again and
			// used ITS key there (2026-09-30 04:37:34, "the game uses slot 3" right after a Magic choice) - held off until closed
			g_closingMagic = true;
			keyboard::CancelNativeWheel(); // match Ammo: finish the close instead of leaving Q/E blocked behind held R
			logger::info("pad: Magic selection requested native R release");
		}

		// The radial closing by the wheel button - let go after a hold, or pressed again after a tap - uses NOTHING, on either
		// wheel (the owner, 2026-09-30: "now that we have multiple items per slot will have to have the right bumper activate
		// the item and the right trigger and left trigger navigate which item they want from the slot and we'll need the
		// center rest snap because otherwise it'll just release to use instead of needing to be activated"). RB (and A) use
		// the pointed slot; the choice is kept cleared, with the stick held centred, until the radial has closed.
		void CloseOnNothing(const char* a_how)
		{
			slowtime::CloseRequested();
			const int pointed = PointedKey();
			quickkeys::ClearPointer();
			// the game re-points within a frame and its close uses the quick key there: with the stick on a slot of the Magic wheel and the
			// button let go, a potion on the same slot of the Equipment wheel was drunk (2026-10-19, first wheel use after loading)
			wheels::HoldKey(pointed);
			g_closingMagic = true;
			logger::info("pad: the wheel closed by {} - nothing used{}", a_how,
				pointed >= 0 ? std::format(" (slot {} was pointed: RB or A uses a slot)", pointed + 1) : std::string());
		}

		// RB or A on a slot of the inventory wheel: the radial closes on NOTHING (the choice cleared, the stick held centred
		// until it has closed), and the slot's item is used by wheels::UseNow - equipment at once through Actor::EquipObject,
		// anything else through the game's own quick-key press after the close. The radial's own close did not use the
		// pointed key (2026-10-01 01:52:39: RB on slot 2, the claymore, "the game uses slot 2" - and the bow stayed in hand).
		// A key holding a spell is drawn empty (MagicWheel.cpp) and uses nothing - spells are the Magic wheel's.
		void CloseOnEquipment(int a_slot, const char* a_how)
		{
			slowtime::CloseRequested();
			quickkeys::ClearPointer();   // every wheel is its own: the game's close uses nothing on any of them
			g_closingMagic = true;   // held clear and centred until the radial has closed
			if (a_slot < 0 || !inventory::Keys()[static_cast<std::size_t>(a_slot)] || magicwheel::IsSpellKey(a_slot)) {
				// nothing to use: no B fallback - its B reached the game as crouch after a click on an empty slot of a wheel opened with R (2026-10-18)
				if (!magicwheel::IsSpellKey(a_slot)) wheels::UseNow(a_slot, a_how);   // logs "holds nothing"
				return;
			}
			g_forceClose = true;     // a wheel opened with a tap and clicked with the mouse did not close by itself: a potion was never used
			g_forceCloseAt = GetTickCount64() + 400;
			g_forceTries = 0;
			if (magicwheel::IsSpellKey(a_slot)) {
				logger::info("pad: the inventory wheel closed on slot {}, whose game key holds a spell - nothing used", a_slot + 1);
				return;
			}
			wheels::UseNow(a_slot, a_how);
		}

		// The radial points only at slots that hold something (the Ammo shortcut works this way already): the right stick's direction is moved to
		// the nearest filled slot of the wheel that is up, its push kept. Nothing filled, or INI [Wheel] bPointFilledOnly=0: the stick is as it is.
		void SnapToFilled(XINPUT_GAMEPAD& a_pad)
		{
			if (!settings::Get().pointFilledOnly || keyboard::ScriptRunning()) return;
			const double mx = a_pad.sThumbRX / 32767.0, my = a_pad.sThumbRY / 32767.0;
			const double m = std::hypot(mx, my);
			if (m < kStickOn) return;
			std::array<bool, 8> filled{};
			if (wheels::Active() == wheels::Wheel::kMagic) {
				const auto spells = wheels::MagicSlots();
				for (int s = 0; s < 8; ++s) filled[static_cast<std::size_t>(s)] = spells[static_cast<std::size_t>(s)] != 0;
			} else if (wheels::Active() == wheels::Wheel::kAmmo) {
				const auto arrows = wheels::AmmoSlots();
				for (int s = 0; s < 8; ++s) filled[static_cast<std::size_t>(s)] = arrows[static_cast<std::size_t>(s)] != 0;
			} else {
				const auto keys = inventory::Keys();
				for (int s = 0; s < 8; ++s) filled[static_cast<std::size_t>(s)] = keys[static_cast<std::size_t>(s)] != 0 && !magicwheel::IsSpellKey(s);
			}
			if (std::none_of(filled.begin(), filled.end(), [](bool b) { return b; })) return;
			const double ang = radialselection::ClockwiseDegrees(mx, my);   // slot n sits 45 * (n - 1) degrees round
			int best = -1;
			double bestD = 1e9;
			for (int s = 0; s < 8; ++s) {
				if (!filled[static_cast<std::size_t>(s)]) continue;
				const double d = std::abs(std::remainder(ang - 45.0 * s, 360.0));
				if (d < bestD) {
					bestD = d;
					best = s;
				}
			}
			double dx = 0, dy = 0;
			radialselection::KeyDirection(best, dx, dy);
			const double push = std::min(m, 1.0);
			a_pad.sThumbRX = static_cast<SHORT>(std::lround(dx * push * 32767.0));
			a_pad.sThumbRY = static_cast<SHORT>(std::lround(dy * push * 32767.0));
		}

		// The wheel closing by its button (let go after a hold, or pressed again after a tap): uses NOTHING by default; with INI [Wheel]
		// bChooseOnClose=1 it uses the slot the stick points at, on whichever wheel is up - the stick alone chooses, as on the game's own wheel.
		// B still backs out.
		// The slot just used with the wheel staying open (bStayOpenAfterUse): letting go of the wheel button right after (the keyboard's slot-key script does,
		// a mouse keeps pointing at it) must not use it again through bChooseOnClose - a weapon was put on and taken off, or a second potion drunk.
		// Forgotten as soon as the pointer moves to another slot or the radial closes.
		int g_stayUsedSlot = -1;

		// A close that never ended: the wheel button went down again (or the radial reported another opening) before the radial reported closed,
		// so the hold of the close - stick centred, nothing pointed, the game's key off its item, a pending B - would stay on the NEW wheel
		// (IWP-024: the stick could point nothing, and the Equipment cell drew black). Every piece of close-side state is let go at once.
		void AbandonClose(const char* a_why)
		{
			slowtime::Reopened();
			g_closingMagic = false;
			g_closeEpoch.Reset();
			g_forceClose = false;
			g_forceTries = 0;
			g_bPulse = 0;
			g_leftToGame = false;
			g_centreStick = 0;
			g_stayUsedSlot = -1;
			g_snapped = false;
			g_unreported = 0;
			g_radialAway = false;
			g_radialResting = true;
			wheels::ReleaseKey();
			quickkeys::RestorePointer(a_why);
			logger::info("pad: the wheel closing was abandoned - {}", a_why);
		}

		void CloseOnPointed(const char* a_how)
		{
			const int slot = settings::Get().chooseOnClose ? PointedKey() : -1;
			if (slot >= 0 && slot == g_stayUsedSlot) {
				g_stayUsedSlot = -1;
				CloseOnNothing(a_how);
				logger::info("pad: slot {} was used just before (the wheel stayed open) - the close uses it no more", slot + 1);
				return;
			}
			if (slot < 0) {
				CloseOnNothing(a_how);
				return;
			}
			logger::info("pad: the wheel closed by {} with slot {} pointed - chosen (bChooseOnClose)", a_how, slot + 1);
			if (wheels::Active() == wheels::Wheel::kMagic) {
				CloseOnMagic(slot);
			} else if (wheels::Active() == wheels::Wheel::kAmmo) {
				CloseOnAmmo(slot);
			} else {
				CloseOnEquipment(slot, "close");
			}
		}

		void ResetMenuState()
		{
			g_menuDownHeld = false;
			g_menuDownFired = false;
		}

		// One controller read as the rules see it: the buttons as read, what went down and up, the time, the menu up, and the
		// buttons the game will see (out). The steps below are the parts of Rewrite, in its order.
		struct Read
		{
			XINPUT_GAMEPAD&   pad;
			WORD              raw;
			WORD              pressed;
			WORD              released;
			bool              ltPressed;
			bool              rtPressed;
			Clock::time_point now;
			menus::Menu       menu;
			WORD&             out;
		};

		// a wheel's own button (INI [Wheel] uEquipmentWheelButton / uMagicWheelButton / uAmmoWheelButton): that wheel is made the
		// active one, and the wheel button is held for as long as it is - so one press opens the radial on that wheel
		void DirectWheelButtons(XINPUT_GAMEPAD& a_pad)
		{
			static WORD s_prevDirect = 0;
			const auto&  st = settings::Get();
			const WORD   wheelBtn = static_cast<WORD>(st.wheelButton ? st.wheelButton : XINPUT_GAMEPAD_DPAD_DOWN);
			const std::array<std::pair<WORD, wheels::Wheel>, 3> direct{ {
				{ static_cast<WORD>(st.equipmentWheelButton), wheels::Wheel::kEquipment },
				{ static_cast<WORD>(st.magicWheelButton), wheels::Wheel::kMagic },
				{ static_cast<WORD>(st.ammoWheelButton), wheels::Wheel::kAmmo } } };
			WORD held = 0;
			const bool inGame = (menus::Active() == menus::Menu::kNone && !menus::AnyOpen()) || quickkeys::RadialOpen();
			for (const auto& [mask, wheel] : direct) {
				if (!mask || mask == wheelBtn || !inGame || !(a_pad.wButtons & mask)) continue;
				held |= mask;
				if (!(s_prevDirect & mask)) {
					if (wheels::Active() != wheel) g_stayUsedSlot = -1;
					wheels::SetActive(wheel);
				}
			}
			s_prevDirect = held;
			if (held) a_pad.wButtons = static_cast<WORD>((a_pad.wButtons & ~held) | wheelBtn);
		}

		// a menu opened or closed: the menu rules start again, and a menu ends a latched wheel
		void TrackMenu(menus::Menu a_menu)
		{
			if (a_menu != g_prevMenu) {
				ResetMenuState();
				SetMagicPanel(false);
				if (a_menu != menus::Menu::kNone) {
					g_latched = false;       // a menu opening ends a latched wheel
					g_pendingLatch = false;
					g_suppressDown = false;
				}
				g_prevMenu = a_menu;
			}
		}

		// the Inventory and the Magic menu: favourites, the assign panel (A, X held, D-pad / triggers cycle), D-pad down
		void MenuRules(Read& r)
		{
			XINPUT_GAMEPAD&   a_pad = r.pad;
			const WORD        raw = r.raw;
			const WORD        pressed = r.pressed;
			const WORD        released = r.released;
			const bool        ltPressed = r.ltPressed;
			const bool        rtPressed = r.rtPressed;
			const auto        now = r.now;
			const menus::Menu menu = r.menu;
			WORD&             out = r.out;
			// the magic menu's panel, read from the widgets themselves five times a second (the owner, 2026-09-30:
			// "I switched tabs to the magic and the magic wheel doesn't appear" - the panel stayed up across the tab
			// switch, and the old toggle-tracking only learned of it from a D-pad down hold)
			if (menu == menus::Menu::kMagic) {
				static Clock::time_point s_nextLook{};
				if (now >= s_nextLook) {
					s_nextLook = now + 200ms;
					const bool up = quickkeys::AnyWheelVisible();
					if (up != quickkeys::MagicPanelUp()) {
						logger::info("pad: the magic menu's wheel panel is {}", up ? "showing - the Magic wheel" : "hidden");
					}
					SetMagicPanel(up);
				}
			}
			// the favourite button (uFavouriteButton): Select by default, so Y stays the game's own. With 0x8000 (Y) it is the
			// original behaviour: Y favourites and the game's sorting moves to the left stick click. 0 = no controller button.
			const WORD favButton = static_cast<WORD>(settings::Get().favouriteButton);
			if (favButton) {
				out &= ~favButton;   // the game never sees it
				if (pressed & favButton) {
					wheels::Favourite(menu);
				}
			}
			// a favourite cannot be dropped (X drops, holding X drops the stack) - the owner, 2026-09-29
			if (menu == menus::Menu::kInventory && (pressed & XINPUT_GAMEPAD_X)) {
				const auto item = rows::HighlightedItem();
				if (wheels::IsFavourite(item)) {
					if (settings::Get().blockFavouriteDropSell) {
						g_swallowX = true;
						logger::info("pad: X on {} - a favourite cannot be dropped", inventory::NameOf(item));
					} else {
						wheels::NoteLeaving(item);   // dropped: it leaves the wheel when none is carried
					}
				}
			}
			if (favButton == XINPUT_GAMEPAD_Y) {   // only when Y was taken for favourites
				out &= ~XINPUT_GAMEPAD_LEFT_THUMB;
				if (raw & XINPUT_GAMEPAD_LEFT_THUMB) {
					out |= XINPUT_GAMEPAD_Y;   // the game's sorting, moved from Y to the stick click
				}
			}
			if (settings::Get().dpadDownPanel) {   // bDpadDownPanel=0: D-pad down stays the game's; L3 (Show Shortcuts) opens the panel natively
				out &= ~XINPUT_GAMEPAD_DPAD_DOWN;
				if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) {
					g_menuDownHeld = true;
					g_menuDownFired = false;
					g_menuDownAt = now;
				}
				if (g_menuDownHeld && !g_menuDownFired && (raw & XINPUT_GAMEPAD_DPAD_DOWN) && now - g_menuDownAt >= kHoldThreshold) {
					g_menuDownFired = true;
					g_pulseLS = kPulseReads;   // the game's own Show/Hide Shortcuts
					logger::info("pad: D-pad down held in the {} - assign panel toggled", menus::Name(menu));
					}
				if (released & XINPUT_GAMEPAD_DPAD_DOWN) {
					if (g_menuDownHeld && !g_menuDownFired) {
						g_pulseDown = kPulseReads;   // a tap: the list moves one row, as before
					}
					ResetMenuState();
				}
			}
			if (quickkeys::PanelOpen() || (menu == menus::Menu::kMagic && quickkeys::MagicPanelUp())) {
				// B backs out of the panel (the game's own toggle, as the hold on D-pad down); the next B leaves the menu
				if (pressed & XINPUT_GAMEPAD_B) {
					g_swallowB = true;
					g_pulseLS = kPulseReads;
					SetMagicPanel(false);
					logger::info("pad: B with the assign panel showing - panel closed");
				}
				// A: in the inventory the game's Assign Item, watched (again on the same item removes it); in the magic
				// menu the Magic wheel's own, and the game never sees it
				// with no slot pointed there is nothing to assign: A stays the game's, and the spell under it is equipped
				// (the owner, 2026-09-30: "I tried to select the alteration spell in there, but it wouldn't let me select
				// it" - the panel read as showing and every A went to "assign with no slot pointed")
				if (pressed & XINPUT_GAMEPAD_A) {
					if (!wheels::Ready()) g_swallowA = true;
					if (wheels::AssignmentWheel(menu) == wheels::Wheel::kAmmo) g_swallowA = true;
					const int pointed = PointedKey();
					if (pointed >= 0) {
						if (wheels::AssignmentWheel(menu) != wheels::Wheel::kEquipment) {
							g_swallowA = true;
						}
						wheels::AssignPressed(menu, pointed);
					}
				}
				// X held on a pointed slot: every entry comes off it. X is swallowed while a slot is pointed, so the game's
				// Drop never sees it (a tap does nothing).
				if ((pressed & XINPUT_GAMEPAD_X) && PointedKey() >= 0) {
					g_panelX = true;
					g_panelXFired = false;
					g_panelXAt = now;
					g_swallowX = true;
				}
				if (g_panelX) {
					if (!(raw & XINPUT_GAMEPAD_X)) {
						g_panelX = false;
					} else if (!g_panelXFired && now - g_panelXAt >= 600ms) {
						g_panelXFired = true;
						const int key = PointedKey();
						if (key >= 0) {
							wheels::ClearSlot(wheels::AssignmentWheel(menu), key);
						}
					}
				}
				out &= ~(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
				const auto wheel = wheels::AssignmentWheel(menu);
				// Assign-panel triggers belong to entry cycling, never to the menu's tab navigation.
				if (messagediscovery::InputAllowed((pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) || ltPressed || rtPressed, [] { return quickkeys::GameMessageUp(); })) {
					if ((pressed & XINPUT_GAMEPAD_DPAD_LEFT) || ltPressed) { wheels::CycleEntry(wheel, PointedKey(), -1); }
					if ((pressed & XINPUT_GAMEPAD_DPAD_RIGHT) || rtPressed) { wheels::CycleEntry(wheel, PointedKey(), +1); }
				}
				a_pad.bLeftTrigger = 0;
				a_pad.bRightTrigger = 0;
			}
		}

		// any other menu (a container or barter list): a favourite cannot be sold or handed over. The HUD radial is a Gamebryo
		// menu too, so this checks for it first.
		void ContainerRules(Read& r)
		{
			const WORD        pressed = r.pressed;
			WORD&             out = r.out;
			if (menus::AnyOpen() && !quickkeys::RadialOpen()) {
				const WORD favButton = static_cast<WORD>(settings::Get().favouriteButton);
				if (favButton && (pressed & favButton)) {
					// the favourite button on the player's own row of a container or barter list: on or off the wheel, as in the Inventory (the star
					// there was only clickable); the other side's rows (the container's, the merchant's) give no item, so nothing happens there
					if (const auto own = rows::HighlightedPlayerItemInContainer()) {
						out &= ~favButton;   // the game never sees it here
						wheels::ToggleItem(own);
						logger::info("pad: the favourite button on {} in a container or barter list - toggled", inventory::NameOf(own));
					}
				}
			}
			if (menus::AnyOpen() && !quickkeys::RadialOpen() && (pressed & XINPUT_GAMEPAD_A)) {
				const auto item = rows::HighlightedPlayerItemInContainer();
				if (wheels::IsFavourite(item)) {
					if (settings::Get().blockFavouriteDropSell) {
						g_swallowA = true;
						logger::info("pad: A on {} in a container or barter list - a favourite cannot be sold or handed over", inventory::NameOf(item));
					} else {
						wheels::NoteLeaving(item);   // sold or handed over: it leaves the wheel when none is carried
					}
				}
			}
		}

		// the radial is up: D-pad L/R switch wheels, the stick points (filled slots only, the centre rest snap)
		void PointRadial(Read& r)
		{
			XINPUT_GAMEPAD&   a_pad = r.pad;
			const WORD        pressed = r.pressed;
			const auto        now = r.now;
			const auto previousWheel = wheels::Active();
			if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) wheels::SwitchWheel(-1);
			if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) wheels::SwitchWheel(+1);
			if (wheels::Active() != previousWheel) g_stayUsedSlot = -1;
			SnapToFilled(a_pad);
			RestSnap(a_pad, now);
			if (!keyboard::MouseOwnsRadial() && !keyboard::ScriptRunning()) {
				g_liveStickKey = radialselection::LiveStickKey(a_pad.sThumbRX, a_pad.sThumbRY);
				if (g_liveStickKey >= 0 && quickkeys::PointedSlot() != g_liveStickKey + 1)
					quickkeys::Repoint(g_liveStickKey);
			}
		}

		// the wheel button in gameplay: tapped (the wheel stays open until the next press), held, or pressed before the wheel showed
		void WheelButton(Read& r, WORD a_wheelBtn, bool a_radialOpen)
		{
			XINPUT_GAMEPAD&   a_pad = r.pad;
			const WORD        raw = r.raw;
			const WORD        pressed = r.pressed;
			const WORD        released = r.released;
			const auto        now = r.now;
			WORD&             out = r.out;
			const auto sinceDown = [&]() { return std::chrono::duration_cast<std::chrono::milliseconds>(now - g_gameDownAt).count(); };
			if (a_radialOpen && !g_sawRadialOpen) {
				logger::info("pad: the wheel showed {} ms after the wheel button went down", sinceDown());
			}
			if (!a_radialOpen && g_sawRadialOpen && g_latched) {
				g_latched = false;   // the game closed a latched wheel by itself (a message box): let go, or the held button reopened it again and again
				logger::info("pad: the latched wheel was closed by the game - the wheel button is let go");
			}
			g_sawRadialOpen = a_radialOpen;
			if (g_pendingLatch) {
				if (a_radialOpen) {
					g_pendingLatch = false;
					g_latched = true;        // the tap that came before the wheel: it stays open until the next press
					logger::info("pad: the tapped wheel showed - it stays open until the next press");
				} else if (now - g_pendingSince > kPendingLatchLimit) {
					g_pendingLatch = false;  // never leave the button held for the game when no wheel came
					logger::warn("pad: no wheel showed within {} ms of the tap - the wheel button is let go", kPendingLatchLimit.count());
				}
			}
			if (pressed & a_wheelBtn) {
				if (g_latched) {
					g_latched = false;
					g_suppressDown = true;   // the game sees the button go up: the wheel closes - on nothing
					if (a_radialOpen && !(pressed & XINPUT_GAMEPAD_B)) {
						CloseOnPointed("the wheel button pressed again");
					}
				} else {
					if (g_pendingLatch) {
						g_pendingLatch = false;   // pressed again before the wheel showed: this press decides
						logger::info("pad: wheel button pressed again before the wheel showed - this press is timed instead");
					}
					g_gameDownAt = now;
					// The wheel is a TES menu too, but it is not open yet at its own press, so a menu up now is a real
					// one (the system menu, a container): D-pad down there is list navigation and is never held.
					g_downInMenu = !a_radialOpen && menus::AnyOpen() && menus::MsSinceClosed() > 600;   // a menu still closing (the game reports menu mode a moment longer) is gameplay
					logger::debug("pad: wheel button down (wheel open: {}, in a menu: {})", a_radialOpen, g_downInMenu);
				}
			}
			if (released & a_wheelBtn) {
				if (g_suppressDown) {
					g_suppressDown = false;
					logger::debug("pad: wheel button up after {} ms - already handled", sinceDown());
				} else if (now - g_gameDownAt < kHoldThreshold && a_radialOpen) {
					g_latched = true;        // a tap: the wheel stays open until the next press
					logger::info("pad: wheel button tapped ({} ms) - the wheel stays open until the next press", sinceDown());
				} else if (now - g_gameDownAt < kHoldThreshold && !g_downInMenu) {
					g_pendingLatch = true;   // a tap let go before the wheel showed: keep it held for the game until it does
					g_pendingSince = now;
					logger::info("pad: wheel button tapped ({} ms) before the wheel showed - held for the game until it opens", sinceDown());
				} else if (a_radialOpen && !(pressed & XINPUT_GAMEPAD_B)) {
					CloseOnPointed("the wheel button let go");   // a hold let go: nothing used, on either wheel
				} else {
					logger::info("pad: wheel button up after {} ms and no wheel showed", sinceDown());
				}
			}
			out &= ~a_wheelBtn;
			if (((raw & a_wheelBtn) && !g_suppressDown) || g_latched || g_pendingLatch) {
				out |= a_wheelBtn;
			}
			if (!quickkeys::RadialOpen()) {
				RestSnap(a_pad, now);   // only notes that it closed
			}
		}

		// the radial is up: B backs out; LT / RT cycle the pointed slot; A and RB choose (unless a message of the game is up)
		void RadialChoices(Read& r, WORD a_wheelBtn)
		{
			XINPUT_GAMEPAD&   a_pad = r.pad;
			const WORD        raw = r.raw;
			const WORD        pressed = r.pressed;
			const bool        ltPressed = r.ltPressed;
			const bool        rtPressed = r.rtPressed;
			WORD&             out = r.out;
			if (quickkeys::RadialOpen() && (pressed & XINPUT_GAMEPAD_B)) {
				slowtime::CloseRequested();
				// B backs out: nothing is used, the wheel closes
				const int bPointed = PointedKey();
				quickkeys::ClearPointer();
				wheels::HoldKey(bPointed);   // as CloseOnNothing: the game's close must find no item on the key it re-points to
				g_closingMagic = true;       // held clear until the radial has closed, then the key goes back (ReleaseKey)
				g_latched = false;
				if (raw & a_wheelBtn) { g_suppressDown = true; }
				g_swallowB = true;
				g_centreStick = kPulseReads * 2;
				out &= ~a_wheelBtn;
				logger::info("pad: B on the wheel - closed without using a slot");
			} else if (quickkeys::RadialOpen() && !g_leftToGame && !g_closingMagic) {
				const int slot = PointedKey();
				if (g_stayUsedSlot >= 0 && slot != g_stayUsedSlot && slot >= 0) g_stayUsedSlot = -1;   // pointed elsewhere: a later close may use that one
				out &= ~((XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER) & ~a_wheelBtn);   // never the wheel button itself
				const int stepSlot = wheels::Active() == wheels::Wheel::kAmmo ? wheels::AmmoSlotAtRing(slot) : slot;   // the Ammo view draws its arrows at ring positions
									if (ltPressed && stepSlot >= 0) { wheels::CycleEntry(wheels::Active(), stepSlot, -1); }
									if (rtPressed && stepSlot >= 0) { wheels::CycleEntry(wheels::Active(), stepSlot, +1); }
				// LB is held from the game and does nothing on the wheel (the owner, 2026-10-01: "I pressed left bumper and it
				// removed the selected magic from the wheel. But left bumper shouldn't do anything like that") - Y in the
				// menus is how an entry comes off a wheel
				// A chooses and closes, as on the game's own wheel (the owner, 2026-09-30: "I tried selecting clairvoyance
				// with pressing A like the vanilla game does and it didn't close the wheel and select the magic it only
				// selected it when I pressed the right bumper it should do both"). On the Magic wheel it is ours - the spell
				// is set and the game never sees A, so it cannot use its own key there; on the inventory wheel A stays the
				// game's, and a key holding a spell still uses nothing.
				// A is ours on both wheels now (the game never sees it): on the inventory wheel it uses the pointed slot
				// exactly as RB does (2026-10-01: "make RB (and A on the inventory wheel) use the pointed key")
				// a message of the game is up over the wheel (its question or its notice): A and RB answer it, they choose nothing - every answer was a
				// new choice and brought the message back ("apply the poison?", "already poisoned", 2026-10-18)
				const bool messageUp = (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_RIGHT_SHOULDER)) && quickkeys::GameMessageUp();
				if (messageUp) {
					logger::info("pad: a message of the game is up over the wheel - A / RB answer it, nothing is chosen");
				}
				if ((pressed & XINPUT_GAMEPAD_A) && !messageUp) {
					bool stayOpen = false;
					if (wheels::Active() == wheels::Wheel::kMagic) {
						CloseOnMagic(slot);
						logger::info("pad: A on the Magic wheel - slot {} chosen, the wheel closes", slot + 1);
					} else if (wheels::Active() == wheels::Wheel::kAmmo) {
						CloseOnAmmo(slot);
					} else if (settings::Get().stayOpenAfterUse && (slot < 0 || !inventory::Keys()[static_cast<std::size_t>(slot)])) {
						stayOpen = true;   // A or a click on an empty slot of a wheel that stays open: nothing happens, it stays open
					} else if (wheels::StaysOpenAfterUse(slot) && !magicwheel::IsSpellKey(slot)) {
						wheels::UseNow(slot, keyboard::ScriptRunning() ? "click" : "A", true);   // bStayOpenAfterUse: the item is put on or taken off and the wheel stays up
						stayOpen = true;
						g_stayUsedSlot = slot;
					} else {
						CloseOnEquipment(slot, keyboard::ScriptRunning() ? "click" : "A");   // the mouse click is a script of the keyboard's
					}
					g_swallowA = true;
					if (stayOpen) {
						out &= ~XINPUT_GAMEPAD_A;   // the game never sees A; the wheel button is left as it is
					} else {
						g_latched = false;
						if (raw & a_wheelBtn) { g_suppressDown = true; }
						out &= ~(a_wheelBtn | XINPUT_GAMEPAD_A);   // let go now: the wheel closes
					}
				}
				if ((pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) && !messageUp) {
					bool stayOpenRB = false;
					if (wheels::Active() == wheels::Wheel::kMagic) {
						CloseOnMagic(slot);
					} else if (wheels::Active() == wheels::Wheel::kAmmo) {
						CloseOnAmmo(slot);
					} else if (settings::Get().stayOpenAfterUse && (slot < 0 || !inventory::Keys()[static_cast<std::size_t>(slot)])) {
						stayOpenRB = true;   // RB on an empty slot of a wheel that stays open: nothing happens
					} else if (wheels::StaysOpenAfterUse(slot) && !magicwheel::IsSpellKey(slot)) {
						wheels::UseNow(slot, "RB", true);   // bStayOpenAfterUse: the wheel stays up
						stayOpenRB = true;
						g_stayUsedSlot = slot;
					} else {
						CloseOnEquipment(slot, "RB");
					}
					if (!stayOpenRB) {
						g_latched = false;
						if (raw & a_wheelBtn) { g_suppressDown = true; }
						out &= ~a_wheelBtn;   // let go now: the wheel closes (on nothing - the use follows)
					}
				}
				a_pad.bLeftTrigger = 0;
				a_pad.bRightTrigger = 0;
			}
		}

		// gameplay: the wheel button, the radial's pointing and its choices
		void GameplayRules(Read& r)
		{
			const WORD        pressed = r.pressed;
			// the button that opens the game's wheel (uWheelButton): D-pad down by default; LB is the game's own binding
			const WORD wheelBtn = settings::Get().wheelButton ? static_cast<WORD>(settings::Get().wheelButton) : static_cast<WORD>(XINPUT_GAMEPAD_DPAD_DOWN);
			const bool radialOpen = quickkeys::RadialOpen();
			// The wheel button went down again while the last wheel was still closing (a quick reopen): its close hold must not carry over (IWP-024)
			if (g_closeEpoch.Superseded(g_closingMagic, quickkeys::OpenCount(), (pressed & wheelBtn) != 0)) {
				AbandonClose("the wheel button went down again before the radial reported closed");
			}
			// Resolve wheel and pointing before a same-read release or A/RB. Native hover
			// events still describe the previous game input when XInput is being read.
			if (radialOpen && !g_leftToGame && !g_closingMagic && !(pressed & XINPUT_GAMEPAD_B)) {
				PointRadial(r);
			}

			WheelButton(r, wheelBtn, radialOpen);
			RadialChoices(r, wheelBtn);
		}

		// buttons a rule used are held from the game until they are let go
		void SwallowHeldButtons(Read& r)
		{
			const WORD        raw = r.raw;
			WORD&             out = r.out;
			if (g_swallowB) {
				if (raw & XINPUT_GAMEPAD_B) {
					out &= ~XINPUT_GAMEPAD_B;
				} else {
					g_swallowB = false;
				}
			}
			if (g_swallowA) {
				if (raw & XINPUT_GAMEPAD_A) {
					out &= ~XINPUT_GAMEPAD_A;
				} else {
					g_swallowA = false;
				}
			}
			if (g_swallowX) {
				if (raw & XINPUT_GAMEPAD_X) {
					out &= ~XINPUT_GAMEPAD_X;
				} else {
					g_swallowX = false;
				}
			}
		}

		// a use on the Equipment wheel waits for the radial to close: if it has not shortly, B closes it (twice at most)
		void ForceClose(Read& r)
		{
			WORD&             out = r.out;
			if (g_leftToGame && !quickkeys::RadialOpen()) {
				g_leftToGame = false;
			}
			if (g_forceClose) {
				if (!quickkeys::RadialOpen()) {
					g_forceClose = false;
					g_bPulse = 0;
				} else if (GetTickCount64() >= g_forceCloseAt && g_bPulse == 0) {
					if (++g_forceTries > 2) {
						g_forceClose = false;   // the game keeps it open (a message waits for A): left to the player
						g_leftToGame = true;
						logger::info("pad: the radial stays open after {} B presses - left as it is", g_forceTries - 1);
					} else {
						keyboard::CancelNativeWheel();   // a wheel opened with the native R key ignores a lone B: it closes as Esc does (B and the key-up of R), IWP-011
						g_bPulse = 2;   // short: the game starts closing on the first read and gives the controls back at once, a longer B reached it as crouch (2026-10-18)
						g_forceCloseAt = GetTickCount64() + 400;
						logger::info("pad: the radial is still open 400 ms after a use on the Equipment wheel - closed with B, so the item is used");
					}
				}
			}
			if (g_bPulse > 0) {
				// only while the radial is still up: a B that arrives after it has closed is the game's crouch (B on a controller) - seen with the wheel opened
				// by the game's own keyboard key (R), 2026-10-18
				if (quickkeys::RadialOpen()) {
					out |= XINPUT_GAMEPAD_B;
					--g_bPulse;
				} else {
					g_bPulse = 0;
				}
			}
		}

		// while the radial closes on a Magic or Ammo slot (or after B) the stick is held centred and no slot is pointed
		void HoldWhileClosing(Read& r)
		{
			XINPUT_GAMEPAD&   a_pad = r.pad;
			if (g_closeEpoch.Superseded(g_closingMagic, quickkeys::OpenCount(), false)) {
				AbandonClose("the radial reported another opening before it reported closed");
			}
			if (g_closingMagic) {
				if (!quickkeys::RadialOpen()) {
					g_closingMagic = false;
					wheels::ReleaseKey();
					quickkeys::RestorePointer("the radial closed");   // the selector arrow back for the next opening
				} else {
					a_pad.sThumbRX = 0;
					a_pad.sThumbRY = 0;
					if (quickkeys::PointedSlot() >= 1) {
						quickkeys::CancelChoice();
					}
				}
			}
			g_closeEpoch.Note(g_closingMagic, quickkeys::OpenCount());
			if (g_centreStick > 0) {
				a_pad.sThumbRX = 0;
				a_pad.sThumbRY = 0;
				--g_centreStick;
			}
		}

		void Rewrite(XINPUT_GAMEPAD& a_pad)
		{
			g_liveStickKey = -1;
			DirectWheelButtons(a_pad);
			const WORD raw = a_pad.wButtons;
			const WORD pressed = raw & ~g_prevRaw;
			const WORD released = ~raw & g_prevRaw;
			const bool ltPressed = a_pad.bLeftTrigger >= kTriggerOn && g_prevLT < kTriggerOn;
			const bool rtPressed = a_pad.bRightTrigger >= kTriggerOn && g_prevRT < kTriggerOn;
			g_prevRaw = raw;
			g_prevLT = a_pad.bLeftTrigger;
			g_prevRT = a_pad.bRightTrigger;

			const auto now = Clock::now();
			if (interop::AmfMenuOpen() || interop::TweenMenuOpen()) {
				// the framework's window (or Tween Menu's menu) has the pad: no wheel rule runs, the read goes on untouched (its gate hides it
				// from the game); an open ammo wheel closes, a latched wheel lets go
				WORD untouched = raw;
				ammo::Rewrite(a_pad, raw, 0, untouched, false, false);
				a_pad.wButtons = raw;
				g_latched = false;
				g_pendingLatch = false;
				g_suppressDown = false;
				return;
			}
			const menus::Menu menu = menus::Active();
			TrackMenu(menu);
			WORD out = raw;
			Read r{ a_pad, raw, pressed, released, ltPressed, rtPressed, now, menu, out };

			// the ammo wheel (its button in gameplay): while it is open it takes the read (Ammo.cpp)
			quickkeys::BoostAnimations();
			wheelname::Tick();   // "Inventory Wheel" / "Magic Wheel" over the wheel on screen
			magicwheel::Tick();   // the Magic wheel's own widget over the game's wheel; no spells on the inventory wheel
			const bool ammoTook = ammo::Rewrite(a_pad, raw, pressed, out, menu == menus::Menu::kNone && !menus::AnyOpen(), quickkeys::RadialOpen());

			if (ammoTook) {
				// the D-pad, A, B and the right stick were the ammo wheel's
			} else if (interop::ControllerKeyboardOpen()) {
				// the search keyboard has the pad: the menu rules stand aside
			} else if (menu != menus::Menu::kNone) {
				MenuRules(r);
			} else {
				ContainerRules(r);
				GameplayRules(r);
			}

			SwallowHeldButtons(r);
			ForceClose(r);
			HoldWhileClosing(r);
			wheels::Tick((raw & XINPUT_GAMEPAD_A) != 0);

			// synthetic presses
			if (g_pulseDown > 0) { out |= XINPUT_GAMEPAD_DPAD_DOWN; --g_pulseDown; }
			if (g_pulseLS > 0) { out |= XINPUT_GAMEPAD_LEFT_THUMB; --g_pulseLS; }

			a_pad.wButtons = out;
		}

		DWORD WINAPI Chained(DWORD a_user, XINPUT_STATE* a_state)
		{
			if (GetCurrentThreadId() != g_gameThread) {
				return g_previous ? g_previous(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			}
			readmemo::Scope readScope;
			if (a_user == 0) {
				if (auto cb = g_frameCallback.load(std::memory_order_acquire)) {
					cb();
				}
			}
			DWORD rc = g_previous ? g_previous(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			if (a_user != 0 && a_user < 4 && rc == ERROR_SUCCESS) {
				// Only user 0 is rewritten. Under Proton the one pad can sit on two indices (Steam's virtual pad and the
				// Deck's own HID device), and a second index would hand the game the buttons the wheel holds back.
				static std::atomic<std::uint32_t> s_seen{ 0 };
				const std::uint32_t bit = 1u << a_user;
				if (!(s_seen.fetch_or(bit, std::memory_order_relaxed) & bit)) {
					logger::warn("pad: controller {} is connected too - only controller 0's reads are rewritten, so a second "
								 "pad (or the same pad seen twice under Proton) reaches the game untouched", a_user);
				}
			}
			if (!a_state || a_user != 0) {
				return rc;
			}
			if (rc != ERROR_SUCCESS) {
				XINPUT_STATE blank{};
				XINPUT_GAMEPAD probe{};
				const bool keys = keyboard::Apply(probe);   // the keyboard as a virtual controller (INI [Keyboard])
				if (!keys && !keyboard::PresentWhenNoPad()) {
					return rc;
				}
				static bool s_said = false;
				if (!s_said) {
					s_said = true;
					logger::info("pad: no controller is connected - the game is handed an idle pad so the keyboard rules run every frame (INI [Keyboard] bVirtualPadWhenNone)");
				}
				*a_state = blank;   // no pad connected: hand over the keyboard state alone
				a_state->Gamepad = probe;
				rc = ERROR_SUCCESS;
			} else {
				keyboard::Apply(a_state->Gamepad);
			}
			const SHORT rawRX = a_state->Gamepad.sThumbRX, rawRY = a_state->Gamepad.sThumbRY;
			Rewrite(a_state->Gamepad);
			const auto& g = a_state->Gamepad;
			// the game only processes a state whose packet number moved: move it whenever what we hand over changes
			if (g.wButtons != g_prevOutButtons || g.bLeftTrigger != g_prevOutLT || g.bRightTrigger != g_prevOutRT || g.sThumbRX != rawRX || g.sThumbRY != rawRY) {
				++g_packetOffset;
				g_prevOutButtons = g.wButtons;
				g_prevOutLT = g.bLeftTrigger;
				g_prevOutRT = g.bRightTrigger;
			}
			a_state->dwPacketNumber += g_packetOffset;
			return rc;
		}

		std::string ModuleOf(const void* a_p)
		{
			HMODULE m = nullptr;
			wchar_t w[MAX_PATH]{};
			if (a_p && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(a_p), &m) && m &&
				GetModuleFileNameW(m, w, MAX_PATH)) {
				return std::filesystem::path(w).filename().string();
			}
			return "an unknown module";
		}
	}

	bool Install()
	{
		if (g_installed.load()) {
			return true;
		}
		static bool s_tried = false;
		if (s_tried) {
			return false;
		}
		s_tried = true;
		g_gameThread = GetCurrentThreadId();   // Install runs at OBSE's post-load, on the game's main thread
		// Wine and Proton answer this ntdll export; a Steam Deck report then says what it runs on.
		if (const auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
			using WineVersion_t = const char*(__cdecl*)();
			if (const auto wineVersion = reinterpret_cast<WineVersion_t>(GetProcAddress(ntdll, "wine_get_version"))) {
				const char* v = wineVersion();
				logger::info("pad: running under Wine / Proton {} (the game thread is {})", v ? v : "(no version)", g_gameThread);
			}
		}
		auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress) {
			logger::error("pad: the game has no import table - no controller rules");
			return false;
		}
		for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
			const char* dll = reinterpret_cast<const char*>(base + desc->Name);
			if (_strnicmp(dll, "xinput", 6) != 0) {
				continue;
			}
			auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
			auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
			for (; names->u1.AddressOfData; ++names, ++slots) {
				const bool match = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
				                       ? IMAGE_ORDINAL64(names->u1.Ordinal) == 2
				                       : std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, "XInputGetState") == 0;
				if (!match) {
					continue;
				}
				auto* slot = reinterpret_cast<XInputGetState_t*>(&slots->u1.Function);
				DWORD old = 0;
				if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
					logger::error("pad: the import slot could not be made writable ({})", GetLastError());
					return false;
				}
				g_previous = *slot;
				*slot = &Chained;
				VirtualProtect(slot, sizeof(void*), old, &old);
				g_previousTarget = ModuleOf(reinterpret_cast<const void*>(g_previous));
				g_installed.store(true);
				logger::info("pad: the game's {} XInputGetState import now runs the wheel's controller rules (previously {} in {})",
					dll, reinterpret_cast<const void*>(g_previous), g_previousTarget);
				return true;
			}
		}
		logger::error("pad: the game imports no XInputGetState - no controller rules");
		return false;
	}

	void SetFrameCallback(FrameCallback a_callback)
	{
		g_frameCallback.store(a_callback, std::memory_order_release);
	}

}
