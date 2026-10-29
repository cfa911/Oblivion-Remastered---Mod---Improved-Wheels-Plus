#include "game/Menus.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"

namespace menus
{
	namespace
	{
		constexpr const wchar_t* kInventoryPath = L"/Game/UI/Original/GameMenuLayer/Inventory/WBP_OriginalMenu_Inventory.WBP_OriginalMenu_Inventory_C";
		constexpr const wchar_t* kMagicPath = L"/Game/UI/Modern/GameMenuLayer/Magic/WBP_ModernMenu_MagicMenu.WBP_ModernMenu_MagicMenu_C";

		std::atomic<UE::UClass*> g_inventoryClass{ nullptr };
		std::atomic<UE::UClass*> g_magicClass{ nullptr };
		std::atomic<bool>        g_inventoryActive{ false };
		std::atomic<bool>        g_magicActive{ false };

		reflect::Handle g_inventoryWidget, g_magicWidget;   // the menu widgets last activated (game thread)

		std::atomic<ULONGLONG> g_lastClosed{ 0 };
		std::atomic<UE::UFunction*> g_fnActivated{ nullptr };
		std::atomic<UE::UFunction*> g_fnDeactivated{ nullptr };

		bool InMenuMode()
		{
			auto* im = RE::InterfaceManager::GetInstance(false, false);
			return im && im->menuMode != 1;
		}

		void OnMenuEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			if (!a_obj || !a_fn) {
				return;
			}
			bool activated = a_fn == g_fnActivated.load();
			bool deactivated = a_fn == g_fnDeactivated.load();
			if (!activated && !deactivated) {
				const std::string n = pe::FunctionName(a_fn);
				if (n == "BP_OnActivated") {
					g_fnActivated.store(a_fn);
					activated = true;
				} else if (n == "BP_OnDeactivated") {
					g_fnDeactivated.store(a_fn);
					deactivated = true;
				} else {
					return;
				}
			}
			if (deactivated) g_lastClosed.store(GetTickCount64());
			auto* cls = a_obj->GetClass();
			auto& flag = cls == g_inventoryClass.load() ? g_inventoryActive : g_magicActive;
			flag.store(activated);
			if (activated) (cls == g_inventoryClass.load() ? g_inventoryWidget : g_magicWidget) = reflect::Hold(a_obj);
			logger::info("menus: {} {}", cls == g_inventoryClass.load() ? "inventory" : "magic menu", activated ? "activated" : "deactivated");
		}

		void TryWatch(std::atomic<UE::UClass*>& a_slot, const wchar_t* a_path, std::atomic<bool>& a_active, const char* a_what)
		{
			if (a_slot.load()) {
				return;
			}
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_path);
			if (!cls) {
				return;   // not opened yet this session
			}
			if (!pe::Watch(cls, &OnMenuEvent)) {
				return;
			}
			a_slot.store(cls);
			// the class loads as the menu opens for the first time: that menu is the one on screen now
			if (InMenuMode()) {
				a_active.store(true);
			}
			logger::info("menus: watching the {} ({})", a_what, InMenuMode() ? "open now" : "closed");
		}
	}

	void Tick()
	{
		TryWatch(g_inventoryClass, kInventoryPath, g_inventoryActive, "inventory");
		TryWatch(g_magicClass, kMagicPath, g_magicActive, "magic menu");
	}

	std::uint64_t MsSinceClosed()
	{
		const auto t = g_lastClosed.load();
		return t ? GetTickCount64() - t : ~0ull;
	}

	bool AnyOpen()
{
		return InMenuMode();
	}

	Menu Active()
	{
		if (!InMenuMode()) {
			// back in gameplay: nothing can be active (clears a missed deactivation)
			g_inventoryActive.store(false);
			g_magicActive.store(false);
			return Menu::kNone;
		}
		if (g_magicActive.load()) {
			return Menu::kMagic;
		}
		if (g_inventoryActive.load()) {
			return Menu::kInventory;
		}
		return Menu::kNone;
	}

	const char* Name(Menu a_menu)
	{
		switch (a_menu) {
		case Menu::kInventory: return "inventory";
		case Menu::kMagic: return "magic menu";
		default: return "none";
		}
	}

	UE::UClass* ClassOf(Menu a_menu)
	{
		return a_menu == Menu::kMagic ? g_magicClass.load() : a_menu == Menu::kInventory ? g_inventoryClass.load() : nullptr;
	}

	UE::UObject* Widget(Menu a_menu)
	{
		reflect::Handle& h = a_menu == Menu::kMagic ? g_magicWidget : g_inventoryWidget;
		if (a_menu != Menu::kMagic && a_menu != Menu::kInventory) return nullptr;
		if (auto* w = reflect::Get(h)) return w;
		// opened before the class was watched: the live instance of the class
		if (auto* cls = ClassOf(a_menu)) {
			for (auto* o : reflect::Instances(cls)) {
				h = reflect::Hold(o);
				return o;
			}
		}
		return nullptr;
	}
}
