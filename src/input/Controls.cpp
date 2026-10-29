#include "input/Controls.h"
#include "core/FunctionClassCache.h"

#include "game/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "core/Settings.h"
#include "engine/Ui.h"

#include <Xinput.h>   // constants only

namespace controls
{
	namespace
	{
		constexpr const wchar_t* kIMC = L"/Game/Dev/Input/GamePlay/InputMappingContexts/IMC_Game_Default.IMC_Game_Default";
		constexpr const wchar_t* kPadTable = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/DT_Modern_Settings_GamepadRebind_Data.DT_Modern_Settings_GamepadRebind_Data";
		constexpr const char*    kFallbackBefore = "IA_Game_Default_OpenStatsMenu";   // Tween Menu's place: before the menu section
		constexpr const wchar_t* kOurAction = L"IA_IWM_AmmoWheel";

		struct RawArray
		{
			std::uint8_t* data;
			std::int32_t  num;
			std::int32_t  max;
		};

		// the XInput buttons a Controls-page key can be (the triggers are axes: not usable as the wheel's button)
		constexpr std::pair<const char*, WORD> kPad[] = {
			{ "Gamepad_DPad_Right", XINPUT_GAMEPAD_DPAD_RIGHT }, { "Gamepad_DPad_Left", XINPUT_GAMEPAD_DPAD_LEFT },
			{ "Gamepad_DPad_Up", XINPUT_GAMEPAD_DPAD_UP }, { "Gamepad_DPad_Down", XINPUT_GAMEPAD_DPAD_DOWN },
			{ "Gamepad_FaceButton_Bottom", XINPUT_GAMEPAD_A }, { "Gamepad_FaceButton_Right", XINPUT_GAMEPAD_B },
			{ "Gamepad_FaceButton_Left", XINPUT_GAMEPAD_X }, { "Gamepad_FaceButton_Top", XINPUT_GAMEPAD_Y },
			{ "Gamepad_LeftShoulder", XINPUT_GAMEPAD_LEFT_SHOULDER }, { "Gamepad_RightShoulder", XINPUT_GAMEPAD_RIGHT_SHOULDER },
			{ "Gamepad_LeftThumbstick", XINPUT_GAMEPAD_LEFT_THUMB }, { "Gamepad_RightThumbstick", XINPUT_GAMEPAD_RIGHT_THUMB },
			{ "Gamepad_Special_Left", XINPUT_GAMEPAD_BACK }, { "Gamepad_Special_Right", XINPUT_GAMEPAD_START },
		};

		struct Status
		{
			bool        actionCreated = false;
			bool        rowAdded = false;
			std::string boundKey;   // the Gamepad_ key our action has now
			std::string problem;
		};
		Status        g_status;
		bool          g_setUp = false;
		UE::UObject*  g_action = nullptr;   // rooted: never collected
		UE::UObject*  g_imc = nullptr;      // an asset, loaded for the session
		std::uint64_t g_ticks = 0;
		bool          g_wasInMenu = true;


		std::string KeyFor(WORD a_mask)
		{
			for (const auto& [n, bit] : kPad) {
				if (bit == a_mask) return n;
			}
			return "Gamepad_DPad_Right";
		}

		WORD MaskFor(const std::string& a_key)
		{
			for (const auto& [n, bit] : kPad) {
				if (a_key == n) return bit;
			}
			return 0;
		}

		void SetKey(void* a_at, const std::string& a_keyName)
		{
			// FKey: FName + TSharedPtr<FKeyDetails> (resolved lazily by the engine); written as a fresh key
			std::memset(a_at, 0, sizeof(UE::FKey));
			new (a_at) UE::FKey(UE::FName(std::wstring(a_keyName.begin(), a_keyName.end()).c_str()));
		}

		struct MappingLayout
		{
			std::int32_t size = 0, action = -1, key = -1;
		};

		MappingLayout Mappings()
		{
			MappingLayout m;
			if (auto* st = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/EnhancedInput.EnhancedActionKeyMapping")) {
				m.size = st->propertiesSize;
				m.action = reflect::Offset(st, "Action");
				m.key = reflect::Offset(st, "Key");
			}
			return m;
		}

		RawArray* MappingArray()
		{
			return g_imc ? reflect::At<RawArray>(g_imc, reflect::Offset(g_imc->GetClass(), "Mappings")) : nullptr;
		}

		// the controller keys our action has in the context now ("None" left out)
		std::vector<std::string> PadKeys()
		{
			std::vector<std::string> keys;
			const auto m = Mappings();
			auto* arr = MappingArray();
			for (std::int32_t i = 0; arr && arr->data && m.size > 0 && m.action >= 0 && m.key >= 0 && i < arr->num; ++i) {
				std::uint8_t* e = arr->data + static_cast<std::ptrdiff_t>(i) * m.size;
				if (*reinterpret_cast<UE::UObject**>(e + m.action) != g_action) continue;
				const auto name = pe::NameOf(*reinterpret_cast<const UE::FName*>(e + m.key));
				if (name.starts_with("Gamepad_") && std::find(keys.begin(), keys.end(), name) == keys.end()) keys.push_back(name);
			}
			return keys;
		}

		bool MapKey(const std::string& a_key)
		{
			ui::Call c(g_imc, L"MapKey");
			void* key = c ? c.At("ToKey") : nullptr;
			if (!key) return false;
			c.Set("Action", g_action);
			SetKey(key, a_key);
			c.Run();
			static_cast<UE::FKey*>(key)->~FKey();
			const auto now = PadKeys();
			return std::find(now.begin(), now.end(), a_key) != now.end();
		}

		// IA_Game_QuickKeys_Keyboard_1..8 taken off the keys in the context (INI [Keyboard] bReplaceQuickKeys): the plugin's slot keys
		// (Keyboard.cpp) replace them, so a digit no longer opens the radial or assigns to the game's own key storage
		int UnbindNativeQuickKeys()
		{
			static thread_local FunctionClassCache<UE::UObject*, std::pair<int, std::uint64_t>, bool> quickActions;
			// the keyboard quick keys are not in IMC_Game_Default (nothing of that name was found there): every mapping context the
			// game has loaded is looked through
			const auto m = Mappings();
			auto* imcClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.InputMappingContext");
			if (!imcClass || m.size <= 0 || m.action < 0) return 0;
			const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(imcClass), "Mappings");
			if (off < 0) return 0;
			int total = 0;
			for (auto* ctx : reflect::Instances(imcClass)) {
				if (!ctx) continue;
				const auto* arr = reinterpret_cast<const RawArray*>(reinterpret_cast<const std::uint8_t*>(ctx) + off);
				if (!arr->data || arr->num <= 0) continue;
				std::vector<UE::UObject*> actions;
				for (std::int32_t i = 0; i < arr->num; ++i) {
					const std::uint8_t* e = arr->data + static_cast<std::ptrdiff_t>(i) * m.size;
					auto* act = *reinterpret_cast<UE::UObject* const*>(e + m.action);
					if (act && quickActions.Get(act, { act->internalIndex, act->GetFName().ToUnstableInt() },
						[&] { return pe::NameOf(act).starts_with("IA_Game_QuickKeys_Keyboard_"); }) &&
						std::find(actions.begin(), actions.end(), act) == actions.end()) {
						actions.push_back(act);
					}
				}
				for (auto* act : actions) {
					ui::Call c(ctx, L"UnmapAllKeysFromAction");
					if (!c) {
						logger::warn("controls: a mapping context has no UnmapAllKeysFromAction - the game's keyboard quick keys stay");
						return total;
					}
					c.Set("Action", act);
					c.Run();
					++total;
				}
				if (!actions.empty()) {
					static int s_named = 0;
					if (s_named++ < 4) logger::info("controls: {} had {} keyboard quick key action(s) - taken off their keys", pe::NameOf(ctx), actions.size());
				}
			}
			return total;
		}

		void RebuildMappings()
		{
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");
			int n = 0;
			for (auto* sub : reflect::Instances(cls)) {
				// both parameters set: zeroed, RebuildType is None and the request does nothing (logic library 7736)
				ui::Call c(sub, L"RequestRebuildControlMappings");
				if (c) {
					c.Set("Options", std::uint8_t{ 1 });      // FModifyContextOptions: bIgnoreAllPressedKeysUntilRelease
					c.Set("RebuildType", std::uint8_t{ 1 });  // EInputMappingRebuildType::Rebuild
					c.Run();
					++n;
				}
			}
			logger::info("controls: Enhanced Input asked to rebuild its mappings ({} player subsystem{})", n, n == 1 ? "" : "s");
		}

		// the "Ammo Wheel" row on the Controller Controls page: after the game's quick keys row (else before the menu rows)
		bool AddRow()
		{
			auto* table = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, kPadTable);
			auto* rowStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/Altar.ModernRebindSettingTableRow");
			auto* dataStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/Altar.ModernRebindData");
			if (!table || !rowStruct || !dataStruct) {
				g_status.problem = "the Controls page's table or row structs are not loaded";
				return false;
			}
			auto* arr = reflect::At<RawArray>(table, reflect::Offset(table->GetClass(), "RebindSettings"));
			const std::int32_t size = rowStruct->propertiesSize;
			const auto oLabel = reflect::Offset(rowStruct, "Label");
			const auto oType = reflect::Offset(rowStruct, "Type");
			const auto oData = reflect::Offset(rowStruct, "RebindData");
			const auto oAction = reflect::Offset(dataStruct, "InputAction");
			const auto oContext = reflect::Offset(dataStruct, "MappingContext");
			const auto oCategory = reflect::Offset(dataStruct, "DefaultCategory");
			const auto oPad = reflect::Offset(dataStruct, "DefaultPrimaryGamepadKey");
			const auto oKey1 = reflect::Offset(dataStruct, "DefaultPrimaryKeyboardKey");
			const auto oKey2 = reflect::Offset(dataStruct, "DefaultSecondaryKeyboardKey");
			if (!arr || !arr->data || size <= 0 || oLabel < 0 || oType < 0 || oData < 0 || oAction < 0 || oContext < 0 || oCategory < 0 || oPad < 0 ||
				oKey1 < 0 || oKey2 < 0) {
				g_status.problem = std::format("the Controls table's row layout is not as read (size {}, Label {}, Type {}, RebindData {})", size, oLabel, oType, oData);
				logger::error("controls: {} - no row", g_status.problem);
				return false;
			}
			std::int32_t at = -1, model = -1, before = -1;
			for (std::int32_t i = 0; i < arr->num; ++i) {
				std::uint8_t* row = arr->data + static_cast<std::ptrdiff_t>(i) * size;
				auto* act = *reinterpret_cast<UE::UObject**>(row + oData + oAction);
				if (act == g_action) return true;   // already there
				if (!act) continue;
				const auto name = pe::NameOf(act);
				if (name.find("QuickKey") != std::string::npos || name.find("Quickkey") != std::string::npos) {
					at = i + 1;   // after the last quick keys row
					model = i;
				}
				if (before < 0 && name == kFallbackBefore) before = i;
				if (model < 0 && at < 0) model = i;
			}
			if (at < 0) at = before >= 0 ? before : arr->num;
			if (model < 0) {
				g_status.problem = "the Controls table has no action row to model ours on";
				logger::error("controls: {}", g_status.problem);
				return false;
			}
			std::vector<std::uint8_t> ours(static_cast<std::size_t>(size), 0);
			const std::uint8_t* like = arr->data + static_cast<std::ptrdiff_t>(model) * size;
			new (ours.data() + oLabel) UE::FText(UE::FText::AsCultureInvariant(UE::FString(L"Ammo Wheel")));
			std::memcpy(ours.data() + oType, like + oType, 1);   // an action row, as the model
			std::uint8_t* data = ours.data() + oData;
			*reinterpret_cast<UE::UObject**>(data + oAction) = g_action;
			*reinterpret_cast<UE::UObject**>(data + oContext) = g_imc;
			std::memcpy(data + oCategory, like + oData + oCategory, 1);
			SetKey(data + oPad, KeyFor(static_cast<WORD>(settings::Get().ammoButton)));
			SetKey(data + oKey1, "None");
			SetKey(data + oKey2, "None");
			// the array grows by one, ours at `at` (elements relocated bitwise, never copied)
			auto* grown = static_cast<std::uint8_t*>(UE::FMemory::Malloc(static_cast<std::size_t>(arr->num + 1) * size, 16));
			if (!grown) return false;
			std::memcpy(grown, arr->data, static_cast<std::size_t>(at) * size);
			std::memcpy(grown + static_cast<std::ptrdiff_t>(at) * size, ours.data(), static_cast<std::size_t>(size));
			std::memcpy(grown + static_cast<std::ptrdiff_t>(at + 1) * size, arr->data + static_cast<std::ptrdiff_t>(at) * size,
				static_cast<std::size_t>(arr->num - at) * size);
			UE::FMemory::Free(arr->data);
			arr->data = grown;
			arr->num += 1;
			arr->max = arr->num;
			logger::info("controls: \"Ammo Wheel\" row added to the Controller Controls page at row {} of {} (modelled on {})", at + 1, arr->num,
				pe::NameOf(*reinterpret_cast<UE::UObject* const*>(like + oData + oAction)));
			return true;
		}

		void Setup()
		{
			g_imc = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, kIMC);
			auto* padTable = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, kPadTable);
			auto* actionClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.InputAction");
			if (!g_imc || !padTable || !actionClass || !reflect::Ok()) {
				return;   // not loaded yet: again later
			}
			g_setUp = true;
			const auto flags = static_cast<UE::EObjectFlags>(static_cast<std::int32_t>(UE::EObjectFlags::Public) |
															  static_cast<std::int32_t>(UE::EObjectFlags::Standalone) |
															  static_cast<std::int32_t>(UE::EObjectFlags::MarkAsRootSet));
			g_action = UE::NewObject<UE::UObject>(UE::GetTransientPackage(), actionClass, UE::FName(kOurAction), flags);
			g_status.actionCreated = g_action != nullptr;
			if (!g_action) {
				g_status.problem = "the input action could not be created";
				logger::error("controls: {}", g_status.problem);
				return;
			}
			if (settings::Get().ammoButton == settings::Get().wheelButton || settings::Get().ammoButton == settings::Get().favouriteButton) {
				logger::warn("controls: the ammo wheel button 0x{:04X} is also the wheel / favourite button - D-pad right is used instead", settings::Get().ammoButton);
				settings::SetAmmoButton(XINPUT_GAMEPAD_DPAD_RIGHT);
			}
			const auto key = KeyFor(static_cast<WORD>(settings::Get().ammoButton));
			const bool mapped = MapKey(key);
			g_status.rowAdded = AddRow();
			logger::info("controls: {} created, mapped in {} to {} ({}), Controls row {}", pe::NameOf(g_action), pe::NameOf(g_imc), key, mapped ? "ok" : "FAILED",
				g_status.rowAdded ? "added" : "NOT added");
			RebuildMappings();
		}

		// the button our action has now: a rebind on the Controls page is kept; a context re-applied without it gets it back
		void Follow()
		{
			const auto keys = PadKeys();
			const auto wanted = KeyFor(static_cast<WORD>(settings::Get().ammoButton));
			if (keys.empty()) {
				if (MapKey(wanted)) {
					logger::info("controls: the ammo wheel's {} was missing from the context (the game re-applied its map) - put back", wanted);
					RebuildMappings();
				}
				g_status.boundKey = wanted;
				return;
			}
			const auto& now = keys.front();
			g_status.boundKey = now;
			if (now != wanted) {
				const WORD mask = MaskFor(now);
				if (mask && (mask == static_cast<WORD>(settings::Get().wheelButton) || mask == static_cast<WORD>(settings::Get().favouriteButton))) {
					// the wheel's own button (LB on a default controller) is never the ammo wheel's: an earlier build copied it into the INI
					static bool warned = false;
					if (!warned) {
						warned = true;
						logger::warn("controls: the ammo wheel showed {} - that is the wheel / favourite button, so it stays on {}", now, wanted);
					}
				} else if (mask) {
					settings::SetAmmoButton(mask);
					logger::info("controls: the ammo wheel was rebound on the Controls page: {} (kept in the INI)", now);
				} else {
					logger::warn("controls: the ammo wheel was bound to {}, which is not a button this plugin reads - it stays on {}", now, wanted);
				}
			}
		}
	}

	void Tick()
	{
		++g_ticks;
		if (!settings::Get().ammoWheel) return;
		if (!g_setUp) {
			if (g_ticks % 10 == 0) Setup();   // about every 2 s until the context and the table are loaded
			return;
		}
		if (!g_action) return;
		const bool inMenu = menus::AnyOpen();
		const bool backInGameplay = g_wasInMenu && !inMenu;
		g_wasInMenu = inMenu;
		if (!inMenu && (backInGameplay || g_ticks % 60 == 0)) {
			Follow();
		}
		if (settings::Get().keyboard.enabled && settings::Get().keyboard.replaceQuickKeys && (g_ticks % 25 == 0 || backInGameplay)) {
			if (const int n = UnbindNativeQuickKeys(); n > 0) {
				static int s_logged = 0;
				if (s_logged++ < 3) logger::info("controls: the game's keyboard quick keys ({} actions) taken off the keys - the plugin's slot keys replace them", n);
				RebuildMappings();
			}
		}
	}

}
