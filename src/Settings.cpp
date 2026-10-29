#include "Settings.h"

namespace settings
{
	namespace
	{
		Values g_values;

		std::filesystem::path ThisModule()
		{
			HMODULE self = nullptr;
			wchar_t buf[MAX_PATH]{};
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&ThisModule), &self) &&
				GetModuleFileNameW(self, buf, MAX_PATH)) {
				return std::filesystem::path(buf);
			}
			return {};
		}

		int ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max)
		{
			const int v = static_cast<int>(GetPrivateProfileIntW(a_section, a_key, a_default, a_ini.c_str()));
			return std::clamp(v, a_min, a_max);
		}

		std::wstring ReadStr(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, const wchar_t* a_default)
		{
			wchar_t buf[256]{};
			GetPrivateProfileStringW(a_section, a_key, a_default, buf, 256, a_ini.c_str());
			return buf;
		}

		// "F", "7", "F5", "NUMPAD3", "INSERT", "LBRACKET", "0x41"... -> a virtual-key code; "" / "NONE" / "0" = 0 (unbound)
		int ParseKey(std::wstring a_name)
		{
			std::wstring n;
			for (const wchar_t c : a_name) {
				if (c != L' ' && c != L'	') n.push_back(static_cast<wchar_t>(std::towupper(c)));
			}
			if (n.empty() || n == L"NONE" || n == L"0") return 0;
			if (n.size() == 1 && ((n[0] >= L'A' && n[0] <= L'Z') || (n[0] >= L'0' && n[0] <= L'9'))) return n[0];
			if (n.size() >= 2 && n[0] == L'F' && std::iswdigit(n[1])) {
				const int f = _wtoi(n.c_str() + 1);
				if (f >= 1 && f <= 24) return VK_F1 + f - 1;
			}
			if (n.starts_with(L"NUMPAD") && n.size() == 7 && std::iswdigit(n[6])) return VK_NUMPAD0 + (n[6] - L'0');
			if (n.starts_with(L"0X")) return static_cast<int>(std::wcstol(n.c_str(), nullptr, 16));
			static const std::pair<const wchar_t*, int> names[] = {
				{ L"LBUTTON", VK_LBUTTON }, { L"RBUTTON", VK_RBUTTON }, { L"MBUTTON", VK_MBUTTON }, { L"INSERT", VK_INSERT }, { L"DELETE", VK_DELETE }, { L"HOME", VK_HOME }, { L"END", VK_END }, { L"PAGEUP", VK_PRIOR },
				{ L"PAGEDOWN", VK_NEXT }, { L"UP", VK_UP }, { L"DOWN", VK_DOWN }, { L"LEFT", VK_LEFT }, { L"RIGHT", VK_RIGHT },
				{ L"BACKSPACE", VK_BACK }, { L"ENTER", VK_RETURN }, { L"SPACE", VK_SPACE }, { L"TAB", VK_TAB }, { L"ESCAPE", VK_ESCAPE },
				{ L"COMMA", VK_OEM_COMMA }, { L"PERIOD", VK_OEM_PERIOD }, { L"MINUS", VK_OEM_MINUS }, { L"EQUALS", VK_OEM_PLUS },
				{ L"LBRACKET", VK_OEM_4 }, { L"RBRACKET", VK_OEM_6 }, { L"SEMICOLON", VK_OEM_1 }, { L"QUOTE", VK_OEM_7 },
				{ L"SLASH", VK_OEM_2 }, { L"BACKSLASH", VK_OEM_5 }, { L"GRAVE", VK_OEM_3 }, { L"NUMPADADD", VK_ADD },
				{ L"NUMPADSUBTRACT", VK_SUBTRACT }, { L"NUMPADMULTIPLY", VK_MULTIPLY }, { L"NUMPADDIVIDE", VK_DIVIDE }, { L"NUMPADDECIMAL", VK_DECIMAL },
			};
			for (const auto& [name, vk] : names) {
				if (n == name) return vk;
			}
			return -1;   // unknown
		}
	}

	std::filesystem::path PluginFolder()
	{
		return ThisModule().parent_path();
	}

	void Load()
	{
		const auto ini = PluginFolder() / L"ImprovedWheelMenu.ini";
		Values v;
		if (std::filesystem::exists(ini)) {
			v.slowTime = ReadInt(ini, L"Wheel", L"bSlowTime", v.slowTime ? 1 : 0, 0, 1) != 0;
			v.slowTimePercent = ReadInt(ini, L"Wheel", L"uSlowTimePercent", 10, 1, 99);
			v.logLevel = ReadInt(ini, L"Log", L"uLogLevel", v.logLevel, 0, 4);
			v.entriesPerSlot = ReadInt(ini, L"Wheel", L"uEntriesPerSlot", v.entriesPerSlot, 1, 16);
			v.pointFilledOnly = ReadInt(ini, L"Wheel", L"bPointFilledOnly", v.pointFilledOnly ? 1 : 0, 0, 1) != 0;
			v.blockFavouriteDropSell = ReadInt(ini, L"Wheel", L"bBlockFavouriteDropSell", v.blockFavouriteDropSell ? 1 : 0, 0, 1) != 0;
			v.starAtLeft = ReadInt(ini, L"Wheel", L"bFavouriteStarAtLeft", v.starAtLeft ? 1 : 0, 0, 1) != 0;
			v.starX = ReadInt(ini, L"Wheel", L"iFavouriteStarX", v.starX, 0, 400);
			v.starSpace = ReadInt(ini, L"Wheel", L"iFavouriteStarSpace", v.starSpace, 0, 200);
			v.stayOpenAfterUse = ReadInt(ini, L"Wheel", L"bStayOpenAfterUse", v.stayOpenAfterUse ? 1 : 0, 0, 1) != 0;
			v.armourOnWheel = ReadInt(ini, L"Wheel", L"bArmourOnWheel", v.armourOnWheel ? 1 : 0, 0, 1) != 0;
			v.rememberWheel = ReadInt(ini, L"Wheel", L"bRememberWheel", v.rememberWheel ? 1 : 0, 0, 1) != 0;
			v.showSlotCapacity = ReadInt(ini, L"Wheel", L"bShowSlotCapacity", v.showSlotCapacity ? 1 : 0, 0, 1) != 0;
			v.theme.magic = ReadInt(ini, L"Theme", L"bMagicWheel", 1, 0, 1) != 0;
			v.theme.inventory = ReadInt(ini, L"Theme", L"bInventoryWheel", 1, 0, 1) != 0;
			v.chooseOnClose = ReadInt(ini, L"Wheel", L"bChooseOnClose", v.chooseOnClose ? 1 : 0, 0, 1) != 0;
			v.centreRestSnap = ReadInt(ini, L"Wheel", L"bCentreRestSnap", v.centreRestSnap, 0, 1) != 0;
			v.restSnapMs = ReadInt(ini, L"Wheel", L"uRestSnapMs", v.restSnapMs, 30, 1000);
			v.ammoWheel = ReadInt(ini, L"AmmoWheel", L"bEnabled", v.ammoWheel, 0, 1) != 0;
			v.ammoButton = ReadInt(ini, L"AmmoWheel", L"uButton", v.ammoButton, 0, 0xFFFF);
			v.ammoCentred = ReadInt(ini, L"AmmoWheel", L"uPosition", v.ammoCentred ? 1 : 0, 0, 1) != 0;
			v.ammoScalePercent = ReadInt(ini, L"AmmoWheel", L"uScalePercent", v.ammoScalePercent, 50, 200);
			v.display.uiScalePercent = ReadInt(ini, L"Display", L"uUiScalePercent", v.display.uiScalePercent, 0, 500);
			v.display.cellRingPercent = ReadInt(ini, L"Display", L"uCellRingPercent", v.display.cellRingPercent, 30, 100);
			v.display.cellHitPercent = ReadInt(ini, L"Display", L"uCellHitPercent", v.display.cellHitPercent, 5, 60);
			v.display.countTextPercent = ReadInt(ini, L"Display", L"uCountTextPercent", v.display.countTextPercent, 20, 300);
			v.display.countBelowPercent = ReadInt(ini, L"Display", L"uCountBelowPercent", v.display.countBelowPercent, -50, 50);
			v.display.centreTextPercent = ReadInt(ini, L"Display", L"uCentreTextPercent", v.display.centreTextPercent, 20, 300);
			v.display.centreLineChars = ReadInt(ini, L"Display", L"uCentreLineChars", v.display.centreLineChars, 8, 80);
			v.equipmentWheelButton = ReadInt(ini, L"Wheel", L"uEquipmentWheelButton", 0, 0, 0xFFFF);
			v.magicWheelButton = ReadInt(ini, L"Wheel", L"uMagicWheelButton", 0, 0, 0xFFFF);
			v.ammoWheelButton = ReadInt(ini, L"Wheel", L"uAmmoWheelButton", 0, 0, 0xFFFF);
			v.animSpeedPercent = ReadInt(ini, L"Wheel", L"uAnimSpeedPercent", v.animSpeedPercent, 100, 1000);
			v.wheelButton = ReadInt(ini, L"Wheel", L"uWheelButton", v.wheelButton, 0, 0xFFFF);
			v.favouriteButton = ReadInt(ini, L"Controller", L"uFavouriteButton", v.favouriteButton, 0, 0xFFFF);
			v.dpadDownPanel = ReadInt(ini, L"Controller", L"bDpadDownPanel", v.dpadDownPanel ? 1 : 0, 0, 1) != 0;
			logger::info("settings: D-pad down hold opens the panel in menus: {}", v.dpadDownPanel ? "on" : "off (L3, the game's own)");
			logger::info("settings: the wheel button is 0x{:04X}", v.wheelButton);
			logger::info("settings: favourites on controller button 0x{:04X}", v.favouriteButton);
			{   // [Keyboard]
				auto& k = v.keyboard;
				k.enabled = ReadInt(ini, L"Keyboard", L"bEnabled", k.enabled ? 1 : 0, 0, 1) != 0;
				const auto key = [&](const wchar_t* a_key, int& a_into) {
					const auto text = ReadStr(ini, L"Keyboard", a_key, L"");
					if (text.empty()) return;   // not in the file: the compiled default stays
					const int vk = ParseKey(text);
					if (vk < 0) logger::warn("settings: [Keyboard] {}={} is not a key name - the default stays", std::string(a_key, a_key + std::wcslen(a_key)), std::string(text.begin(), text.end()));
					else a_into = vk;
				};
				key(L"sLB", k.lb); key(L"sRB", k.rb); key(L"sLT", k.lt); key(L"sRT", k.rt);
				key(L"sNextEntryMouse", k.nextEntryMouse);
				key(L"sNativeWheelKey", k.nativeWheelKey);
				if (k.nativeWheelKey > 0 && k.nativeWheelKey < VK_BACK) {
					logger::warn("settings: sNativeWheelKey must be a keyboard key, not a mouse button; disabled");
					k.nativeWheelKey = 0;
				}
				key(L"sA", k.a); key(L"sX", k.x); key(L"sY", k.y);
				key(L"sDpadUp", k.dpadUp); key(L"sDpadDown", k.dpadDown); key(L"sDpadLeft", k.dpadLeft); key(L"sDpadRight", k.dpadRight);
				key(L"sL3", k.l3); key(L"sR3", k.r3); key(L"sStart", k.start); key(L"sSelect", k.select);
				key(L"sEquipmentWheel", k.equipmentWheel); key(L"sMagicWheel", k.magicWheel); key(L"sAmmoWheel", k.ammoWheel);
				key(L"sStickUp", k.stickUp); key(L"sStickDown", k.stickDown); key(L"sStickLeft", k.stickLeft); key(L"sStickRight", k.stickRight);
				const auto slots = ReadStr(ini, L"Keyboard", L"sSlotKeys", L"");   // eight names, comma separated
				if (!slots.empty()) {
					std::size_t from = 0;
					for (int i = 0; i < 8 && from <= slots.size(); ++i) {
						const auto comma = slots.find(L',', from);
						const int  vk = ParseKey(slots.substr(from, comma == std::wstring::npos ? std::wstring::npos : comma - from));
						if (vk >= 0) k.slots[static_cast<std::size_t>(i)] = vk;
						if (comma == std::wstring::npos) break;
						from = comma + 1;
					}
				}
				const auto alt = ReadStr(ini, L"Keyboard", L"sSlotKeysAlt", L"");
				if (!alt.empty()) {
					std::size_t from = 0;
					for (int i = 0; i < 8 && from <= alt.size(); ++i) {
						const auto comma = alt.find(L',', from);
						const int  vk = ParseKey(alt.substr(from, comma == std::wstring::npos ? std::wstring::npos : comma - from));
						if (vk >= 0) k.slotsAlt[static_cast<std::size_t>(i)] = vk;
						if (comma == std::wstring::npos) break;
						from = comma + 1;
					}
				}
				k.wheelToggle = ReadInt(ini, L"Keyboard", L"bWheelToggle", k.wheelToggle ? 1 : 0, 0, 1) != 0;
				k.padWhenNone = ReadInt(ini, L"Keyboard", L"bVirtualPadWhenNone", k.padWhenNone ? 1 : 0, 0, 1) != 0;
				k.replaceQuickKeys = ReadInt(ini, L"Keyboard", L"bReplaceQuickKeys", k.replaceQuickKeys ? 1 : 0, 0, 1) != 0;
				logger::info("settings: keyboard cancel binding removed; direct slot activation, wheel shortcuts and panel entry cycling enabled");
				logger::info("settings: keyboard as a virtual controller {} (LB 0x{:02X}, RB 0x{:02X}, LT 0x{:02X}, RT 0x{:02X}, Select 0x{:02X}, slot 1 0x{:02X})",
					k.enabled ? "on" : "off", k.lb, k.rb, k.lt, k.rt, k.select, k.slots[0]);
			}
			g_values = v;
			logger::info("settings: {} read (uLogLevel={}, uEntriesPerSlot={}, bCentreRestSnap={}, uRestSnapMs={}, ammo wheel {} on button 0x{:04X} at {}%)",
				ini.string(), v.logLevel, v.entriesPerSlot, v.centreRestSnap, v.restSnapMs, v.ammoWheel ? "on" : "off", v.ammoButton, v.ammoScalePercent);
		} else {
			g_values = v;
			logger::warn("settings: {} not found - compiled defaults in use (uLogLevel={}, uEntriesPerSlot={})", ini.string(), v.logLevel, v.entriesPerSlot);
		}
	}

	const Values& Get()
	{
		return g_values;
	}

	void SetAmmoButton(int a_mask)
	{
		g_values.ammoButton = a_mask;
		const auto ini = PluginFolder() / L"ImprovedWheelMenu.ini";
		if (!WritePrivateProfileStringW(L"AmmoWheel", L"uButton", std::to_wstring(a_mask).c_str(), ini.c_str())) {
			logger::warn("settings: [AmmoWheel] uButton={} could not be written to {} ({})", a_mask, ini.string(), GetLastError());
		}
	}
}
