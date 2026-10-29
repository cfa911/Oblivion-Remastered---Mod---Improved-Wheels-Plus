#include "input/Interop.h"

namespace interop
{
	namespace
	{
		using Answer = bool (*)();

		// an export looked up once it exists, again at most every 2 s until it does
		struct CachedExport
		{
			const wchar_t* module;
			const char*    name;
			const char*    found;   // logged once when the export is found (nullptr: nothing logged)
			Answer         fn = nullptr;
			ULONGLONG      nextLook = 0;

			bool Ask()
			{
				if (!fn) {
					const ULONGLONG now = GetTickCount64();
					if (now < nextLook) {
						return false;
					}
					nextLook = now + 2000;
					if (HMODULE m = ::GetModuleHandleW(module)) {
						fn = reinterpret_cast<Answer>(::GetProcAddress(m, name));
						if (fn && found) {
							logger::info("{}", found);
						}
					}
					if (!fn) {
						return false;
					}
				}
				return fn();
			}
		};

		// Menus Revamped's answers asked on every call (its typing state)
		bool MenusRevampedAnswers(const char* a_export)
		{
			const HMODULE m = GetModuleHandleW(L"MenusRevamped.dll");
			if (!m) return false;
			const auto fn = reinterpret_cast<Answer>(GetProcAddress(m, a_export));
			return fn && fn();
		}
	}

	bool AmfMenuOpen()
	{
		static CachedExport s_amf{ L"ApocryphaMenuFramework.dll", "AMF_IsMenuOpen", "pad: the menu framework reports its window - the wheels stand down while it is open" };
		return s_amf.Ask();
	}

	bool TweenMenuOpen()
	{
		static CachedExport s_tween{ L"TweenMenu.dll", "TweenMenu_IsOpen", "pad: Tween Menu reports its menu - the wheels stand down while it is open" };
		return s_tween.Ask();
	}

	bool ControllerKeyboardOpen()
	{
		static CachedExport s_keyboard{ L"MenusRevamped.dll", "MenusRevamped_IsControllerKeyboardOpen", nullptr };
		return s_keyboard.Ask();
	}

	bool SearchLightTyping() { return MenusRevampedAnswers("MenusRevamped_IsTyping") || ControllerKeyboardOpen(); }

	bool SearchLightCursorOverBar() { return MenusRevampedAnswers("MenusRevamped_CursorOverBar"); }

	// Without them there is nothing to find and the object array is never walked: the walk builds two name strings per object and
	// took most of the game thread while the Inventory or Magic menu was open (capture 2026-10-20: 93 % of one core).
	bool OldSearchPluginsLoaded()
	{
		static const bool loaded = GetModuleHandleW(L"OblivionInventorySearch.dll") != nullptr || GetModuleHandleW(L"OblivionMagicSearch.dll") != nullptr;
		return loaded;
	}
}
