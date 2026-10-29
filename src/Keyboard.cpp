#include "Keyboard.h"

#include "Ammo.h"
#include "Menus.h"
#include "Pad.h"
#include "PEHook.h"
#include "QuickKeys.h"
#include "MessageDiscovery.h"
#include "Reflect.h"
#include "Rows.h"
#include "Wheels.h"
#include "Ui.h"
#include "Settings.h"
#include "SlowTime.h"

#include <atomic>

namespace keyboard
{
	namespace
	{
		std::array<bool, 256> g_down{};
		ULONGLONG             g_lastActive = 0;
		bool g_mouseRadial = false;
		bool g_mouseRadialSeen = false;

		HWND g_keyboardWindow = nullptr;
		WNDPROC g_previousWindowProc = nullptr;
		UINT g_releaseWheelMessage = 0;
		std::atomic<bool> g_nativeWheelAllowed{ false };
		std::atomic<bool> g_nativeWheelHeld{ false };
		std::atomic<bool> g_nativeWheelStarted{ false };
		std::atomic<bool> g_nativeWheelCancel{ false };
		std::atomic<bool> g_suppressPhysicalRUp{ false };
		std::atomic<ULONGLONG> g_nativeWheelAt{ 0 };
		bool g_nativeWheelSeen = false;

		int NativeWheelKey()
		{
			const int key = settings::Get().keyboard.nativeWheelKey;
			return key ? key : 'R';
		}

		LPARAM NativeKeyData(bool down)
		{
			const auto scan = MapVirtualKeyW(NativeWheelKey(), MAPVK_VK_TO_VSC_EX);
			return static_cast<LPARAM>(1u | ((scan & 0xffu) << 16) |
				((scan & 0xff00u) ? (1u << 24) : 0u) | (down ? 0u : 0xc0000000u));
		}

		LRESULT ForwardMessage(HWND window, UINT message, WPARAM key, LPARAM data)
		{
			const auto previous = g_previousWindowProc;   // null after the hook was taken off: the default procedure then
			return previous ? CallWindowProcW(previous, window, message, key, data) : DefWindowProcW(window, message, key, data);
		}

		LRESULT CALLBACK KeyboardWindowProc(HWND window, UINT message, WPARAM key, LPARAM data)
		{
			// The window is going away: take our procedure off again (only when it is still the first in the chain; a hook installed over ours stays
			// and, with the previous procedure cleared, forwards to the default procedure).
			if (message == WM_NCDESTROY) {
				if (GetWindowLongPtrW(window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&KeyboardWindowProc) && g_previousWindowProc) {
					SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_previousWindowProc));
				}
				const auto result = ForwardMessage(window, message, key, data);
				g_nativeWheelAllowed = false;
				g_nativeWheelHeld = false;
				g_nativeWheelStarted = false;
				g_nativeWheelCancel = false;
				g_suppressPhysicalRUp = false;
				g_keyboardWindow = nullptr;
				g_previousWindowProc = nullptr;
				return result;
			}
			// Preserve the native keyboard press across the physical key release. No game objects are touched here.
			if (message == g_releaseWheelMessage && g_releaseWheelMessage != 0) {
				// Private messages reach the original window procedure on its owning thread.
				// Drop a queued press after focus loss/cancellation, but always deliver the release.
				if (key && (!g_nativeWheelHeld || GetForegroundWindow() != window)) return 0;
				return ForwardMessage(window, key ? WM_KEYDOWN : WM_KEYUP, NativeWheelKey(), NativeKeyData(key != 0));
			}
			if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && key == FALSE)) {
				if (g_nativeWheelHeld.exchange(false)) {
					ForwardMessage(window, WM_KEYUP, NativeWheelKey(), NativeKeyData(false));
				}
				g_nativeWheelAllowed = false;
				g_nativeWheelHeld = false;
				g_nativeWheelStarted = false;
				g_nativeWheelCancel = false;
				g_suppressPhysicalRUp = false;
			}
			const bool physicalNativeKey = settings::Get().keyboard.lb == NativeWheelKey();
			if (physicalNativeKey && key == NativeWheelKey() && message == WM_KEYUP && g_suppressPhysicalRUp.exchange(false)) return 0;
			if (physicalNativeKey && key == NativeWheelKey() && message == WM_KEYDOWN) {
				if ((data & (static_cast<LPARAM>(1) << 30)) && (g_nativeWheelHeld || g_suppressPhysicalRUp)) return 0;
				if (!(data & (static_cast<LPARAM>(1) << 30)) && g_nativeWheelAllowed) {
					g_suppressPhysicalRUp = true;
					if (g_nativeWheelHeld.load()) {
						g_nativeWheelCancel = true;
						return 0;
					}
					g_nativeWheelHeld = true;
					g_nativeWheelStarted = true;
					g_nativeWheelAt = GetTickCount64();
					// Forward the real native key-down once. The held keyboard action keeps mouse input working.
				}
			}
			return ForwardMessage(window, message, key, data);
		}

		bool InstallNativeWheelToggle()
		{
			if (g_keyboardWindow && IsWindow(g_keyboardWindow)) return true;
			HWND window = GetForegroundWindow();
			DWORD pid = 0;
			if (!window || !GetWindowThreadProcessId(window, &pid) || pid != GetCurrentProcessId()) return false;
			if (!g_releaseWheelMessage) g_releaseWheelMessage = RegisterWindowMessageW(L"cfa911.IWM.NativeWheelRelease");
			if (!g_releaseWheelMessage) return false;
			SetLastError(0);
			const auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&KeyboardWindowProc));
			if (!previous) return false;
			g_previousWindowProc = reinterpret_cast<WNDPROC>(previous);
			g_keyboardWindow = window;
			logger::info("keyboard: native wheel toggle installed (trigger 0x{:02X}, game key 0x{:02X})", settings::Get().keyboard.lb, NativeWheelKey());
			return true;
		}

		void ReleaseNativeWheel()
		{
			if (g_nativeWheelHeld.exchange(false) && g_keyboardWindow) {
				PostMessageW(g_keyboardWindow, g_releaseWheelMessage, 0, 0);
			}
			g_nativeWheelSeen = false;
		}


		bool GameHasFocus()
		{
			const HWND fg = GetForegroundWindow();
			DWORD      pid = 0;
			if (!fg) return false;
			GetWindowThreadProcessId(fg, &pid);
			return pid == GetCurrentProcessId();
		}

		bool Down(int a_vk) { return a_vk > 0 && a_vk < 256 && (GetAsyncKeyState(a_vk) & 0x8000) != 0; }

		// Is this array slot's object one of the search plugins' text boxes? The array is walked while the game loads and frees
		// objects: a slot can hold an object already torn down, whose class or name index is garbage and the engine's name lookup
		// faults on it (crash 2026-10-19 18:30, SearchBoxFocused on a digit key). The reads are fault-guarded; such a slot is skipped.
		struct BoxCtx
		{
			UE::UObject* obj;
			bool         isBox;
		};

		void IsSearchBoxRun(void* a_ctx)
		{
			auto* c = static_cast<BoxCtx*>(a_ctx);
			c->isBox = pe::Utf8(c->obj->GetClass()->GetFName().ToString()) == "EditableTextBox" &&
				pe::Utf8(c->obj->GetFName().ToString()).find("OIS_SearchBox") != std::string::npos;
		}

		bool IsSearchBox(UE::UObject* a_obj)   // no C++ objects here: __try cannot unwind them
		{
			BoxCtx ctx{ a_obj, false };
			__try {
				IsSearchBoxRun(&ctx);
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
			return ctx.isBox;
		}

		// The text boxes only exist when the Inventory Search / Magic Search plugins are loaded (SearchLight's bars are not text boxes).
		// Without them there is nothing to find and the object array is never walked: the walk builds two name strings per object and
		// took most of the game thread while the Inventory or Magic menu was open (capture 2026-10-20: 93 % of one core, 40 % of the
		// samples in SearchBoxFocused).
		bool SearchPluginLoaded()
		{
			static const bool loaded = GetModuleHandleW(L"OblivionInventorySearch.dll") != nullptr || GetModuleHandleW(L"OblivionMagicSearch.dll") != nullptr;
			return loaded;
		}

		// SearchLight types into a bar of its own (not a text box the array walk could find) and takes the window's keys itself, but
		// the keys are still down for GetAsyncKeyState: a digit typed into the bar would assign the highlighted item to that slot, and
		// a typed R, Z, C or F would press the wheel's virtual buttons. SearchLight says when a bar has the typing (MenuSearch_IsTyping,
		// the SearchLight release after 1.2.1; an older one has no such export and the answer is no, as before).
		bool MenusRevampedAnswers(const char* a_export)
		{
			const HMODULE m = GetModuleHandleW(L"MenusRevamped.dll");
			if (!m) return false;
			const auto fn = reinterpret_cast<bool (*)()>(GetProcAddress(m, a_export));
			return fn && fn();
		}

		bool SearchLightTyping() { return MenusRevampedAnswers("MenusRevamped_IsTyping") || MenusRevampedAnswers("MenusRevamped_IsControllerKeyboardOpen"); }

		// the mouse is over a SearchLight bar: a click there is the bar's (it starts the typing), not a click on the wheel's last pointed slot
		bool SearchLightBarUnderMouse() { return MenusRevampedAnswers("MenusRevamped_CursorOverBar"); }

		// a search box (the Inventory Search / Magic Search plugins') has the keyboard: digits are typing, not slot keys.
		// The boxes found are kept (as Handles: the slot says whether the object is still the same); the array is walked again only when
		// no kept box is alive, at most once a second, and once when the open menu changes (each menu builds its own box).
		bool SearchBoxFocused()
		{
			if (SearchLightTyping()) return true;
			if (!SearchPluginLoaded()) return false;
			static std::vector<reflect::Handle> s_boxes;
			static ULONGLONG                    s_scanned = 0;
			static int                          s_scanMenu = -1;

			bool alive = false, focused = false;
			for (const auto& h : s_boxes) {
				auto* o = reflect::Get(h);
				if (!o) continue;
				alive = true;
				ui::Call c(o, L"HasKeyboardFocus");
				if (c && c.RunGuarded() && c.Get<bool>("ReturnValue")) focused = true;
			}
			const int       menu = static_cast<int>(menus::Active());
			const ULONGLONG now = GetTickCount64();
			if (focused || (alive && menu == s_scanMenu) || now - s_scanned < 1000) return focused;
			s_scanned = now;
			s_scanMenu = menu;

			auto* arr = UE::FUObjectArray::GetSingleton();
			if (!arr) return focused;
			s_boxes.clear();
			focused = false;
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n; ++i) {
				auto* item = arr->IndexToObject(i);
				if (!item || !item->object) continue;
				auto* o = reinterpret_cast<UE::UObject*>(item->object);
				if (!IsSearchBox(o)) continue;
				s_boxes.push_back(reflect::Hold(o));
				ui::Call c(o, L"HasKeyboardFocus");
				if (c && c.RunGuarded() && c.Get<bool>("ReturnValue")) focused = true;
			}
			arr->UnlockInternalArray();
			return focused;
		}

		// the mouse is over the wheel on screen (viewport units, as the widget is measured)
		// the mouse is reported in screen pixels, the widgets are measured in viewport units (pixels / the UI's DPI scale: the log
		// showed the cursor at the exact middle of a 2560 x 1440 screen with the wheel's middle at (960, 540)); the scale comes from the
		// game (GetViewportScale), failing that the engine's default rule of the window height over 1080, or the INI's own value
		double ViewportScale()
		{
			auto* pc = ui::PlayerController();
			double scale = 1.0;
			if (pc) {
				static auto* lib = ui::Class(L"/Script/UMG.WidgetLayoutLibrary");
				ui::Call vs(lib ? lib->GetDefaultObject(false) : nullptr, L"GetViewportScale");
				bool got = false;
				if (vs) {
					vs.Set("WorldContextObject", pc);
					if (vs.RunGuarded()) {
						const float v = vs.Get<float>("ReturnValue");
						if (v > 0.1f && v < 10.0f) {
							scale = v;
							got = true;
						}
					}
				}
				if (!got) {
					RECT rc{};
					const HWND fg = GetForegroundWindow();
					if (fg && GetClientRect(fg, &rc) && rc.bottom > 0) scale = rc.bottom / 1080.0;
				}
			}
			if (settings::Get().display.uiScalePercent > 0) scale = settings::Get().display.uiScalePercent / 100.0;
			return scale;
		}

		bool MouseUnitsImpl(double& a_mx, double& a_my, double& a_scale)
		{
			auto* pc = ui::PlayerController();
			if (!pc) return false;
			ui::Call c(pc, L"GetMousePosition");
			if (!c || !c.RunGuarded()) return false;
			const double scale = ViewportScale();
			a_scale = scale;
			a_mx = c.Get<float>("LocationX") / scale;
			a_my = c.Get<float>("LocationY") / scale;
			return true;
		}

		// The Windows cursor in viewport units. While a menu owns the mouse (the assign panel) GetMousePosition stays frozen at
		// the spot the menu took the mouse, and only the Windows cursor follows the visible pointer (synchronized log, IWP-032,
		// 2026-10-28). Only when the game's own window is in front.
		bool CursorUnits(double& a_x, double& a_y)
		{
			const HWND fg = GetForegroundWindow();
			DWORD      pid = 0;
			if (!fg || !GetWindowThreadProcessId(fg, &pid) || pid != GetCurrentProcessId()) return false;
			POINT p{};
			if (!GetCursorPos(&p) || !ScreenToClient(fg, &p)) return false;
			const double scale = ViewportScale();
			a_x = p.x / scale;
			a_y = p.y / scale;
			return true;
		}

		// The cell (0..7, -1 = none) at a point in viewport units. The game points a slot from the mouse's ANGLE round the wheel's
		// middle, so hovering a cell does not point it (the owner, 2026-10-16: the clickable spots were inside the hole, not on the
		// cells): the cells are found from the wheel picture's own geometry - eight circles 45 degrees apart, 0.71 of the
		// picture's half-width out (the counters sit on the same spots).
		int CellAt(double mx, double my)
		{
			auto* wheel = quickkeys::VisibleWheel();
			if (!wheel) return -1;
			auto* material = ui::ChildNamed(wheel, L"quickKeys_material");
			if (!material) material = ui::FindInTree(wheel, "quickKeys_material");
			auto* image = material ? ui::ChildNamed(material, L"Image") : nullptr;
			if (!image && material) image = ui::FindInTree(material, "Image");
			double x = 0, y = 0, w = 0, h = 0;
			if (!image || !ui::Measure(image, x, y, w, h)) return -1;
			const double half = w * 0.5, cx = x + half, cy = y + h * 0.5;
			int          best = -1;
			const double ring = settings::Get().display.cellRingPercent / 100.0;
			double       bestDist = half * settings::Get().display.cellHitPercent / 100.0;   // a cell is about 0.18 of the half-width across; a little more is taken
			for (int k = 0; k < 8; ++k) {
				const double ang = k * 45.0 * 3.14159265358979323846 / 180.0;
				const double px = cx + std::sin(ang) * ring * half, py = cy - std::cos(ang) * ring * half;
				const double d = std::hypot(mx - px, my - py);
				if (d < bestDist) bestDist = d, best = k;
			}
			return best;
		}

		bool Printable(int a_vk)
		{
			return (a_vk >= 'A' && a_vk <= 'Z') || (a_vk >= '0' && a_vk <= '9') || a_vk == VK_SPACE || (a_vk >= 0xBA && a_vk <= 0xC0) || (a_vk >= 0xDB && a_vk <= 0xDE);
		}

		// typing into a search box in the Inventory / Magic menu (looked at most every 150 ms, and only when asked)
		bool Typing()
		{
			static ULONGLONG s_at = 0;
			static bool      s_typing = false;
			const auto menu = menus::Active();
			if (menu != menus::Menu::kInventory && menu != menus::Menu::kMagic) return false;
			const ULONGLONG now = GetTickCount64();
			if (now - s_at >= 150) {
				s_at = now;
				s_typing = SearchBoxFocused();
			}
			return s_typing;
		}

		// slot N (1 = top, clockwise, 45 degrees apart) as a right-stick deflection
		void SlotStick(int a_slot, SHORT& a_x, SHORT& a_y)
		{
			const double angle = (a_slot - 1) * 45.0 * 3.14159265358979323846 / 180.0;
			a_x = static_cast<SHORT>(std::lround(std::sin(angle) * 32000.0));
			a_y = static_cast<SHORT>(std::lround(std::cos(angle) * 32000.0));
		}

		// A slot key's script, advanced a read at a time from what the game shows (never blind timing: a stick held toward a slot
		// before the radial is up turns the camera that way instead - the owner, 2026-10-16: "the character looks in those 8
		// directions"):
		//   gameplay slot keys activate stored entries directly; only mouse-click and assign scripts remain
		//   assign - (the panel opened with L3 if it was not up) the stick at the slot until pointed, then A, (L3 again)
		struct Script
		{
			enum Kind { kNone, kAssign, kClick } kind = kNone;
			int       slot = 0;      // 0..7
			int       phase = 0;
			bool      openedPanel = false;
			ULONGLONG at = 0;
		} g_script;

		void Next(int a_phase)
		{
			g_script.phase = a_phase;
			g_script.at = GetTickCount64();
		}


		void End(const char* a_why)
		{
			logger::info("keyboard: slot key script ended ({})", a_why);
			g_script.kind = Script::kNone;
		}

		bool PanelUp(menus::Menu a_menu)
		{
			return quickkeys::PanelOpen() || (a_menu == menus::Menu::kMagic && pad::MagicPanelUp());
		}

		// lays the running script over this read; true while one is running
		bool RunScript(XINPUT_GAMEPAD& a_pad)
		{
			if (g_script.kind == Script::kNone) return false;
			const ULONGLONG elapsed = GetTickCount64() - g_script.at;
			SHORT           rx = 0, ry = 0;
			SlotStick(g_script.slot + 1, rx, ry);
			const auto stick = [&] { a_pad.sThumbRX = rx; a_pad.sThumbRY = ry; };
			const bool pointed = quickkeys::PointedSlot() == g_script.slot + 1;
			const auto menu = menus::Active();
			if (g_script.kind == Script::kClick) {   // a click is the A button on the slot the mouse already points (the game points by the mouse's angle, as by the stick's)
				const bool open = menu == menus::Menu::kInventory || menu == menus::Menu::kMagic ? PanelUp(menu) : quickkeys::RadialOpen();
				if (!open) {
					End("the wheel closed");
					return false;
				}
				a_pad.wButtons |= XINPUT_GAMEPAD_A;
				++g_script.phase;   // reads that carried the A: at least three, so the rules see a press even when the game reads the pad slowly
				if ((elapsed >= 50 && g_script.phase >= 3) || elapsed > 400) End("clicked");

			} else {
				switch (g_script.phase) {
				case 0:   // L3 opens the panel when it is not up
					if (PanelUp(menu)) {
						Next(1);
					} else {
						g_script.openedPanel = true;
						a_pad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
						if (elapsed >= 120) Next(10);
					}
					break;
				case 10:  // until it shows
					if (PanelUp(menu) && elapsed >= 30) Next(1);
					else if (elapsed > 900) End("the panel did not open");
					break;
				case 1:
					stick();
					if ((pointed && elapsed >= 25) || elapsed > 400) Next(2);
					break;
				case 2:
					a_pad.wButtons |= XINPUT_GAMEPAD_A;
					stick();
					if (elapsed >= 50) Next(3);
					break;
				case 3:
					if (elapsed >= 40) {
						if (g_script.openedPanel) Next(4);
						else End("assigned");
					}
					break;
				default:   // L3 closes the panel again
					a_pad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
					if (elapsed >= 120) End("assigned, panel closed");
					break;
				}
			}
			return g_script.kind != Script::kNone;
		}
	}

	bool MouseUnits(double& a_x, double& a_y)
	{
		double scale = 1.0;
		return MouseUnitsImpl(a_x, a_y, scale);
	}

	bool PresentWhenNoPad()
	{
		const auto& k = settings::Get().keyboard;
		return k.enabled && k.padWhenNone;
	}

	bool Apply(XINPUT_GAMEPAD& a_pad)
	{
		const auto& k = settings::Get().keyboard;
		if (!k.enabled) return false;
		if (!GameHasFocus()) {
			g_nativeWheelAllowed = false;
			ReleaseNativeWheel();
			g_down = {};
			return false;
		}
		bool any = false;
		const auto press = [&](int a_vk, WORD a_button) {
			if (Down(a_vk) && !(Printable(a_vk) && Typing())) {   // a letter typed into a search box is not a controller press
				a_pad.wButtons |= a_button;
				any = true;
			}
		};
		{   // a key per wheel: that wheel is made the active one and the wheel button is held while the key is
			static int s_prev = -1;
			int        now = -1;
			const auto menu = menus::Active();
			if ((menu == menus::Menu::kNone && !menus::AnyOpen()) || quickkeys::RadialOpen()) {
				if (Down(k.equipmentWheel)) now = 0;
				else if (Down(k.magicWheel)) now = 1;
				else if (Down(k.ammoWheel)) now = 2;
			}
			if (now >= 0) {
				if (now != s_prev) wheels::SetActive(now == 0 ? wheels::Wheel::kEquipment : now == 1 ? wheels::Wheel::kMagic : wheels::Wheel::kAmmo);
				a_pad.wButtons |= static_cast<WORD>(settings::Get().wheelButton ? settings::Get().wheelButton : XINPUT_GAMEPAD_DPAD_DOWN);
				any = true;
			}
			s_prev = now;
		}
		const auto context = menus::Active();
		const bool radial = quickkeys::RadialOpen();
		if (g_mouseRadial) {
			if (radial) g_mouseRadialSeen = true;
			if ((!radial && g_mouseRadialSeen) || std::hypot(a_pad.sThumbRX / 32767.0, a_pad.sThumbRY / 32767.0) > 0.35) {
				g_mouseRadial = false;
				g_mouseRadialSeen = false;
			}
		}

		const bool panelMenu = context == menus::Menu::kInventory || context == menus::Menu::kMagic;
		const bool panel = panelMenu && PanelUp(context);
		const bool gameplay = context == menus::Menu::kNone && !menus::AnyOpen();
		static bool previousWheel = false;
		const bool wheelDown = Down(k.lb);
		const bool mouseDown = Down(k.nextEntryMouse);
		const bool nativeToggle = k.wheelToggle && (k.nativeWheelKey != 0 || k.lb == 'R') && InstallNativeWheelToggle();
		if (nativeToggle) {
			g_nativeWheelAllowed = (gameplay || radial) && !ammo::Open();
			if (k.lb != NativeWheelKey() && wheelDown && !previousWheel && g_nativeWheelAllowed) {
				if (g_nativeWheelHeld || radial) {
					g_nativeWheelCancel = true;
				} else {
					g_nativeWheelHeld = true;
					g_nativeWheelAt = GetTickCount64();
					if (PostMessageW(g_keyboardWindow, g_releaseWheelMessage, 1, 0)) {
						g_nativeWheelStarted = true;
						logger::debug("keyboard: wheel opened through native key 0x{:02X}", NativeWheelKey());
					} else {
						g_nativeWheelHeld = false;
						logger::warn("keyboard: posting native wheel press failed ({})", GetLastError());
					}
				}
			}
			if (g_nativeWheelStarted.exchange(false)) {
				wheels::KeepActive();
				g_mouseRadial = true;
				g_mouseRadialSeen = false;
				g_nativeWheelSeen = false;
			}
			if (g_nativeWheelHeld && radial) g_nativeWheelSeen = true;
			if (g_nativeWheelCancel.exchange(false)) {
				slowtime::CloseRequested();
				if (radial) { a_pad.wButtons |= XINPUT_GAMEPAD_B; any = true; }
				ReleaseNativeWheel();
			} else if (g_nativeWheelHeld && ((!radial && g_nativeWheelSeen) || (!radial && !gameplay) ||
				(!radial && GetTickCount64() - g_nativeWheelAt.load() > 1200))) {
				ReleaseNativeWheel();
			}
		} else if (k.wheelToggle) {
			g_nativeWheelAllowed = false;
			if (wheelDown && !previousWheel && (gameplay || radial) && !ammo::Open()) {
				if (radial) {
					slowtime::CloseRequested();
					a_pad.wButtons |= XINPUT_GAMEPAD_B;
				} else {
					g_mouseRadial = true;
					g_mouseRadialSeen = false;
					wheels::KeepActive();
					a_pad.wButtons |= static_cast<WORD>(settings::Get().wheelButton);
				}
				any = true;
			}
		} else if (gameplay || radial) {
			if (wheelDown) wheels::KeepActive();
			press(k.lb, static_cast<WORD>(settings::Get().wheelButton));
		}
		previousWheel = wheelDown;
		const bool radialInputDown = radial && (Down(k.rb) || Down(k.a) || Down(k.dpadLeft) || Down(k.dpadRight) || Down(k.lt) || Down(k.rt) || mouseDown);
		if (radialInputDown && messagediscovery::InputAllowed(true, [] { return quickkeys::GameMessageUp(); })) {   // asked only when one of these keys is down: nothing else in the block acts
			press(k.rb, XINPUT_GAMEPAD_RIGHT_SHOULDER);
			press(k.a, XINPUT_GAMEPAD_A);
			press(k.dpadLeft, XINPUT_GAMEPAD_DPAD_LEFT);
			press(k.dpadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
			if (Down(k.lt)) { a_pad.bLeftTrigger = 255; any = true; }
			// Right click shares the next-entry key's trigger and edge handling; never cycle a mouse-derived slot directly.
			if (Down(k.rt) || mouseDown) { a_pad.bRightTrigger = 255; any = true; }
		}
		if (radial || panel || ammo::Open()) {
			press(k.dpadUp, XINPUT_GAMEPAD_DPAD_UP);
			press(k.dpadDown, XINPUT_GAMEPAD_DPAD_DOWN);
			press(k.y, XINPUT_GAMEPAD_Y);
			press(k.r3, XINPUT_GAMEPAD_RIGHT_THUMB);
			press(k.start, XINPUT_GAMEPAD_START);
		}

		if (panelMenu && !Typing()) {
			press(k.l3, XINPUT_GAMEPAD_LEFT_THUMB);
			if (panel) {
				press(k.x, XINPUT_GAMEPAD_X);
				press(k.a, XINPUT_GAMEPAD_A);
				press(k.dpadLeft, XINPUT_GAMEPAD_DPAD_LEFT);
				press(k.dpadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
				if (messagediscovery::InputAllowed(Down(k.lt) || Down(k.rt) || mouseDown, [] { return quickkeys::GameMessageUp(); })) {
					if (Down(k.lt)) { a_pad.bLeftTrigger = 255; any = true; }
					if (Down(k.rt) || mouseDown) { a_pad.bRightTrigger = 255; any = true; }
				}
			}
		}
		// IWP-030 select-key handling: call the same favourite action without generating pad input.
		{
			static bool selectWasDown = false;
			const bool selectDown = Down(k.select);
			const bool selectPressed = selectDown && !selectWasDown;
			selectWasDown = selectDown;
			if (selectDown && menus::AnyOpen() && !radial) {
				const bool typing = Printable(k.select) && Typing();
				if (settings::Get().favouriteButton == XINPUT_GAMEPAD_BACK && context != menus::Menu::kNone) {
					if (selectPressed && !typing) {
						logger::info("keyboard: native favourite action");
						wheels::Favourite(context);
					}
				} else {
					const auto item = settings::Get().favouriteButton == XINPUT_GAMEPAD_BACK ? rows::HighlightedPlayerItemInContainer() : 0;
					if (item) {
						if (selectPressed && !typing) wheels::ToggleItem(item);
					} else {
						press(k.select, XINPUT_GAMEPAD_BACK);
					}
				}
			}
		}
		// the right stick: four keys, two together make a diagonal
		const int sx = (Down(k.stickRight) ? 1 : 0) - (Down(k.stickLeft) ? 1 : 0);
		const int sy = (Down(k.stickUp) ? 1 : 0) - (Down(k.stickDown) ? 1 : 0);
		if (sx || sy) {
			const double len = std::hypot(static_cast<double>(sx), static_cast<double>(sy));
			a_pad.sThumbRX = static_cast<SHORT>(std::lround(sx / len * 32000.0));
			a_pad.sThumbRY = static_cast<SHORT>(std::lround(sy / len * 32000.0));
			any = true;
		}
		// a slot key (the number row or Numpad 1-8; the game's own quick keys are taken off them): in gameplay it uses slot N at
		// directly without opening the radial; in the Inventory / Magic menu it assigns the highlighted row to slot N through the assign panel - the controller's
		// own flow, so the Magic menu fills the Magic wheel and the Inventory the Equipment wheel (see RunScript)
		for (int i = 0; i < 8; ++i) {
			bool fired = false;
			for (const int vk : { k.slots[static_cast<std::size_t>(i)], k.slotsAlt[static_cast<std::size_t>(i)] }) {
				if (vk <= 0 || vk >= 256) continue;
				const bool now = Down(vk);
				fired = fired || (now && !g_down[static_cast<std::size_t>(vk)]);
				g_down[static_cast<std::size_t>(vk)] = now;
			}
			if (!fired || g_script.kind != Script::kNone) continue;
			const auto menu = menus::Active();
			if (menu == menus::Menu::kInventory || menu == menus::Menu::kMagic) {
				if (SearchBoxFocused()) continue;
				// directly, without playing L3 / the stick / A: a played controller press switched the game's button pictures to the controller's
				logger::info("keyboard: slot key {} in the {} - assigned to slot {} directly", i + 1, menus::Name(menu), i + 1);
				wheels::AssignHighlighted(menu, i);
			} else if (menu == menus::Menu::kNone && (!menus::AnyOpen() || quickkeys::RadialOpen())) {
				if (Typing() || quickkeys::GameMessageUp()) continue;
				wheels::QuickUse(i);
				any = true;
			}
		}
		// IWP-032 panel hover start
		// The mouse owns the selection from the moment it moves onto a cell until the stick, the d-pad or the shoulders (a
		// controller, or keys that press them) move it; while it owns it the hovered cell is held, because the game keeps
		// pointing the slot at the angle of its own frozen mouse position (it snapped back to slot 8: owner recording, 2026-10-28).
		{
			static bool  hadMouse = false, mouseOwns = false;
			static POINT last{};
			static int   held = -1;
			const bool   padMoves = (a_pad.wButtons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
			                                      XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) ||
			                     std::abs(a_pad.sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || std::abs(a_pad.sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
			                     std::abs(a_pad.sThumbRX) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || std::abs(a_pad.sThumbRY) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
			if (!panel) {
				hadMouse = mouseOwns = false;
				held = -1;
			} else {
				POINT p{};
				if (GetCursorPos(&p)) {   // only a move counts: a resting mouse never takes the selection from the controller
					const bool moved = hadMouse && (p.x != last.x || p.y != last.y);
					last = p;
					hadMouse = true;
					double x = 0, y = 0;
					if (moved && !SearchLightBarUnderMouse() && CursorUnits(x, y)) {   // the Windows cursor: GetMousePosition is frozen here
						const int cell = CellAt(x, y);
						if (cell >= 0) {
							if (cell != held) logger::info("keyboard: mouse hover selected panel slot {}", cell + 1);
							held = cell;
							mouseOwns = true;
						}
					}
				}
				if (padMoves) mouseOwns = false;
				if (mouseOwns && held >= 0 && quickkeys::PointedSlot() != held + 1 && !quickkeys::GameMessageUp()) quickkeys::Repoint(held);
			}
		}
		// IWP-032 panel hover end
		{   // the left mouse button on a cell of the open assign panel assigns the highlighted row to that cell
			static bool s_lmb = false;
			const bool now = Down(VK_LBUTTON);
			const auto menu = menus::Active();
			const bool inMenu = menu == menus::Menu::kInventory || menu == menus::Menu::kMagic;
			const bool where = inMenu ? PanelUp(menu) : quickkeys::RadialOpen();   // the assign panel, or the HUD radial
			if (now && !s_lmb && g_script.kind == Script::kNone && where && !quickkeys::GameMessageUp() && !SearchLightBarUnderMouse()) {   // a click on a message of the game is the message's
				int pointed = quickkeys::PointedSlot();   // 1..8, the slot the mouse (or the stick) points at now
				double cx = 0, cy = 0;
				if (inMenu && CursorUnits(cx, cy)) {   // the cell under the cursor wins over a selection the hover has not caught up with
					const int cell = CellAt(cx, cy);
					if (cell >= 0) pointed = cell + 1;
				}
				if (pointed >= 1) {
					if (inMenu) {
						wheels::AssignHighlighted(menu, pointed - 1);
						logger::info("keyboard: direct mouse assignment on slot {}", pointed);
					} else {
						g_script = { Script::kClick, pointed - 1, 0, false, GetTickCount64() };
						logger::info("keyboard: click - A on slot {} of the radial", pointed);
					}
				}
			}
			s_lmb = now;
		}
		if (ammo::Open()) {   // the ammo wheel: the mouse points (hover) and chooses (click); the arrow keys step, Enter equips, Backspace closes
			static bool      s_lmb = false;
			static ULONGLONG s_aUntil = 0;
			const bool       lmb = Down(VK_LBUTTON);
			double           mx = 0, my = 0;
			if (MouseUnits(mx, my)) {
				const int entry = ammo::EntryUnderMouse(mx, my);
				if (entry >= 0) {
					ammo::PointAt(entry);
					if (lmb && !s_lmb) s_aUntil = GetTickCount64() + 70;
				}
			}
			s_lmb = lmb;
			if (GetTickCount64() < s_aUntil) {
				a_pad.wButtons |= XINPUT_GAMEPAD_A;
				any = true;
			}
			press(VK_UP, XINPUT_GAMEPAD_DPAD_UP);
			press(VK_DOWN, XINPUT_GAMEPAD_DPAD_DOWN);
			press(VK_RETURN, XINPUT_GAMEPAD_A);
			press(VK_BACK, XINPUT_GAMEPAD_B);
		}
		if (RunScript(a_pad)) any = true;
		const ULONGLONG t = GetTickCount64();
		if (any) g_lastActive = t;
		return any || t - g_lastActive < 400;
	}

	bool MouseOwnsRadial()
	{
		return g_mouseRadial && quickkeys::RadialOpen() && !ScriptRunning();
	}

	bool ScriptRunning()
	{
		return g_script.kind != Script::kNone;
	}

	void CancelNativeWheel()
	{
		if (g_nativeWheelHeld) g_nativeWheelCancel = true;
	}
}
