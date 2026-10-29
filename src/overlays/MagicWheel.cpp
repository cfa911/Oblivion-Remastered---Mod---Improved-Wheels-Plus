#include "overlays/MagicWheel.h"

#include "game/Inventory.h"
#include "game/Menus.h"
#include "engine/PEHook.h"
#include "game/QuickKeys.h"
#include "engine/Reflect.h"
#include "overlays/RingOverlay.h"
#include "game/Rows.h"
#include "overlays/SlotCounts.h"
#include "engine/Ui.h"
#include "wheels/Wheels.h"

namespace magicwheel
{
	namespace
	{
		constexpr const wchar_t* kWheelMic = L"/Game/UI/Materials/QuickKeys/MIC_UI_QuickKeys.MIC_UI_QuickKeys";

		// ours
		reflect::Handle g_root, g_img, g_imgSlot, g_mid;
		UE::UObject*    g_midParent = nullptr;   // compared only, never read through
		int             g_builds = 0;
		ULONGLONG       g_lastBuild = 0;
		bool            g_shown = false;
		std::array<UE::UObject*, 8> g_tex{};
		std::array<float, 8>        g_op{};
		std::unordered_map<std::uint64_t, float> g_mirrored;   // the game's scalars as last copied (by FName number)
		double g_x = -1e9, g_y = -1e9, g_w = -1, g_h = -1;
		ULONGLONG g_nextMeasure = 0;

		// the game's wheel image hidden under ours, and its own opacity to put back
		reflect::Handle g_gameImg;
		float           g_gameOpacity = 1.0f;

		// the inventory wheel: keys that hold a spell
		std::array<bool, 8> g_spellKey{};
		ULONGLONG           g_nextEquip = 0;
		std::string         g_status = "not shown yet";

		std::string NameOf(UE::UObject* a_o) { return a_o ? pe::Utf8(a_o->GetFName().ToString()) : std::string("none"); }

		// the game's wheel picture inside the visible wheel widget: quickKeys_material > Image (the primary session's
		// reads, 2026-09-30: ...WBP_ModernMenu_QuickKeys.WidgetTree.quickKeys_material.WidgetTree.Image, drawn by a
		// MaterialInstanceDynamic of MIC_UI_QuickKeys)
		UE::UObject* GameImage(UE::UObject* a_wheel)
		{
			auto* material = ui::ChildNamed(a_wheel, L"quickKeys_material");
			if (!material) material = ui::FindInTree(a_wheel, "quickKeys_material");
			auto* img = material ? ui::ChildNamed(material, L"Image") : nullptr;
			if (!img && material) img = ui::FindInTree(material, "Image");
			static bool logged = false;
			if (!logged) {
				logged = true;
				logger::info("magic wheel: the game's wheel picture is {} > {} (drawn by {})", material ? NameOf(material) : "NOT FOUND (quickKeys_material)",
					img ? NameOf(img) : "NOT FOUND (Image)", NameOf(img ? ui::BrushResource(img) : nullptr));
			}
			return img;
		}

		UE::UObject* ParentOf(UE::UObject* a_mid)
		{
			static auto* mi = ui::Class(L"/Script/Engine.MaterialInstance");
			const auto off = mi && a_mid && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(mi), "Parent") : -1;
			return off >= 0 ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_mid) + off) : nullptr;
		}

		bool IsMid(UE::UObject* a_o)
		{
			static auto* mid = ui::Class(L"/Script/Engine.MaterialInstanceDynamic");
			return a_o && mid && a_o->GetClass() == mid;
		}

		bool Build()
		{
			const ULONGLONG now = GetTickCount64();
			if (g_lastBuild && now - g_lastBuild < 2000) return false;
			g_lastBuild = now;
			auto* root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			const std::wstring n = std::to_wstring(++g_builds);
			auto* canvas = root ? ui::RootCanvas(root, (L"IwmMagicCanvas" + n).c_str()) : nullptr;
			if (!canvas) return false;
			UE::UObject* slot = nullptr;
			auto* img = ui::AddToCanvas(canvas, L"/Script/UMG.Image", (L"IwmMagicWheel" + n).c_str(), &slot);
			if (!img || !slot) return false;
			ui::Anchors(slot, 0, 0, 0, 0);
			ui::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 59);   // over the menus' wheel, under the wheel's name (60)
			vp.Run();
			ui::Visible(root, false);
			g_root = reflect::Hold(root);
			g_img = reflect::Hold(img);
			g_imgSlot = reflect::Hold(slot);
			g_mid = {};
			g_midParent = nullptr;
			g_shown = false;
			g_x = g_y = -1e9, g_w = g_h = -1;
			logger::info("magic wheel: built (build {})", g_builds);
			return true;
		}

		// our instance, of the same parent as the game's (the HUD and the menus may use different ones)
		UE::UObject* EnsureMid(UE::UObject* a_gameMid)
		{
			auto* img = reflect::Get(g_img);
			if (!img) return nullptr;
			auto* parent = a_gameMid ? ParentOf(a_gameMid) : nullptr;
			if (!parent) parent = ui::Load(kWheelMic);
			auto* mid = reflect::Get(g_mid);
			if (mid && parent == g_midParent && ui::BrushResource(img) == mid) return mid;
			static auto* mlib = ui::Class(L"/Script/Engine.KismetMaterialLibrary");
			ui::Call c(mlib ? mlib->GetDefaultObject(false) : nullptr, L"CreateDynamicMaterialInstance");
			c.Set("WorldContextObject", ui::PlayerController());
			c.Set("Parent", parent);
			mid = parent && c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
			if (!mid) {
				logger::warn("magic wheel: its material instance could not be made (parent {})", NameOf(parent));
				return nullptr;
			}
			ui::Call b(img, L"SetBrushFromMaterial");
			b.Set("Material", mid);
			b.Run();
			g_mid = reflect::Hold(mid);
			g_midParent = parent;
			g_tex.fill(nullptr);
			g_op.fill(-1.0f);
			g_mirrored.clear();
			logger::info("magic wheel: its material is an instance of {}", NameOf(parent));
			return mid;
		}

		// every scalar the game's instance holds, copied onto ours - except the slots' own (IDn_Opacity): the selector,
		// its arrow and whatever the game animates follow the game's wheel exactly
		void MirrorScalars(UE::UObject* a_gameMid, UE::UObject* a_mid)
		{
			static auto* mi = ui::Class(L"/Script/Engine.MaterialInstance");
			static auto* elem = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Engine.ScalarParameterValue"));
			static auto* info = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Engine.MaterialParameterInfo"));
			if (!mi || !elem || !info || !a_gameMid || !a_mid || !reflect::Ok()) return;
			static const auto arrOff = reflect::Offset(reinterpret_cast<UE::UStruct*>(mi), "ScalarParameterValues");
			static const auto infoOff = reflect::Offset(elem, "ParameterInfo");
			static const auto valueOff = reflect::Offset(elem, "ParameterValue");
			static const auto nameOff = reflect::Offset(info, "Name");
			const auto size = elem->propertiesSize;
			if (arrOff < 0 || infoOff < 0 || valueOff < 0 || nameOff < 0 || size <= 0) return;
			struct RawArray { const std::uint8_t* data; std::int32_t num; std::int32_t max; };
			const auto* arr = reinterpret_cast<const RawArray*>(reinterpret_cast<const std::uint8_t*>(a_gameMid) + arrOff);
			for (std::int32_t i = 0; arr->data && i < arr->num && i < 64; ++i) {
				const auto* e = arr->data + static_cast<std::size_t>(i) * static_cast<std::size_t>(size);
				const auto& name = *reinterpret_cast<const UE::FName*>(e + infoOff + nameOff);
				const float v = *reinterpret_cast<const float*>(e + valueOff);
				std::uint64_t key = 0;
				std::memcpy(&key, &name, std::min(sizeof(key), sizeof(name)));
				const auto it = g_mirrored.find(key);
				if (it != g_mirrored.end() && it->second == v) continue;
				const auto text = pe::Utf8(name.ToString());
				if (text.starts_with("ID")) {
					g_mirrored[key] = v;   // a slot's own: ours decides
					continue;
				}
				ui::Call c(a_mid, L"SetScalarParameterValue");
				c.Set("ParameterName", name);
				c.Set("Value", v);
				c.Run();
				g_mirrored[key] = v;
			}
		}

		void RestoreGame()
		{
			if (auto* game = reflect::Get(g_gameImg)) {
				ui::Float(game, L"SetRenderOpacity", g_gameOpacity);
				logger::info("magic wheel: the game's wheel picture shown again (opacity {:.2f}, now {:.2f})", g_gameOpacity, ui::RenderOpacity(game));
			}
			g_gameImg = {};
		}

		void Hide(const char* a_why)
		{
			if (!g_shown) return;
			ui::Visible(reflect::Get(g_root), false);
			RestoreGame();
			g_shown = false;
			g_status = std::string("hidden - ") + a_why;
			logger::info("magic wheel: hidden ({})", a_why);
		}

		void Show(UE::UObject* a_wheel, int a_mode)   // 0 Magic spells, 1 the Equipment keys (the Inventory panel), 2 the Ammo wheel
		{
			auto* gameImg = GameImage(a_wheel);
			if (!gameImg) {
				Hide("the game's wheel picture was not found");
				return;
			}
			if (!reflect::Get(g_root) && !Build()) return;
			auto* root = reflect::Get(g_root);
			auto* slot = reflect::Get(g_imgSlot);
			auto* gameMid = ui::BrushResource(gameImg);
			if (!IsMid(gameMid)) gameMid = nullptr;
			auto* mid = EnsureMid(gameMid);
			if (!root || !slot || !mid) return;

			// over the game's picture, exactly
			const ULONGLONG now = GetTickCount64();
			if (quickkeys::Animating() || now >= g_nextMeasure || !g_shown) {   // every read while the game's wheel pops up or out: our picture follows it, not 100 ms behind
				g_nextMeasure = now + 100;
				double x = 0, y = 0, w = 0, h = 0;
				if (!ui::Measure(gameImg, x, y, w, h)) {
					Hide("the game's wheel is not laid out");
					return;
				}
				if (std::abs(x - g_x) > 0.25 || std::abs(y - g_y) > 0.25) {
					ui::Vec2(slot, L"SetPosition", x, y);
					g_x = x, g_y = y;
				}
				if (std::abs(w - g_w) > 0.25 || std::abs(h - g_h) > 0.25) {
					ui::Vec2(slot, L"SetSize", w, h);
					g_w = w, g_h = h;
				}
			}

			// the game's picture hidden under ours (a different one after a load or in another menu: the old one back first)
			if (reflect::Get(g_gameImg) != gameImg) {
				RestoreGame();
				// read as the panel may still be fading in: anything short of full is taken as full - the picture itself is
				// never faded by the game (its parents are), and a part-way value put back left the inventory wheel faint
				const float had = ui::RenderOpacity(gameImg);
				g_gameOpacity = had >= 0.99f ? had : 1.0f;
				logger::debug("magic wheel: the game's wheel picture hidden (it had opacity {:.2f})", had);
				ui::Float(gameImg, L"SetRenderOpacity", 0.0f);
				g_gameImg = reflect::Hold(gameImg);
			}

			// the eight: the Magic wheel's spells, or (the Inventory's assign panel) the items on the game's keys - the game redraws its
			// own wheel as an assign lands and, drawn under ours, its pictures flashed through (the owner, 2026-10-16)
			static int s_mode = 0;
			if (s_mode != a_mode) {
				s_mode = a_mode;
				g_tex.fill(nullptr);
				g_op.fill(-1.0f);
			}
			const std::array<std::uint32_t, 8> spells = a_mode == 1 ? (wheels::Ready() ? inventory::Keys() : inventory::KeyMap{}) :a_mode == 2 ? wheels::AmmoRingSlots() : wheels::MagicSlots();
			for (int k = 0; k < 8; ++k) {
				const auto here = spells[static_cast<std::size_t>(k)];
				auto* tex = here ? (a_mode != 0 ? rows::ItemIcon(here) : rows::SpellIcon(here)) : nullptr;
				const std::wstring id = L"ID" + std::to_wstring(k + 1);
				if (tex && tex != g_tex[static_cast<std::size_t>(k)]) {
					ui::SetMidTexture(mid, (id + L"Texture").c_str(), tex);
					g_tex[static_cast<std::size_t>(k)] = tex;
				}
				const float op = tex ? 1.0f : 0.0f;
				if (op != g_op[static_cast<std::size_t>(k)]) {
					ui::SetMidScalar(mid, (id + L"_Opacity").c_str(), op);
					g_op[static_cast<std::size_t>(k)] = op;
				}
			}
			MirrorScalars(gameMid, mid);

			if (!g_shown) {
				ui::Visible(root, true);
				g_shown = true;
				int filled = 0;
				for (const auto s : spells) filled += s ? 1 : 0;
				g_status = std::format("shown over {} ({} of 8 slots hold a spell)", NameOf(gameImg), filled);
				logger::info("magic wheel: {}", g_status);
			}
		}

		// A picture is a spell's when it is one of the game's magic icons (/Game/Art/UI/Icons/Dynamic_Icons/menus/icons/magic/
		// ...) - asked of the texture's own path, once per texture. A key with a picture and no item is NOT enough: the game
		// keeps a slot's old picture after an item leaves its key, and treating that as a spell blanked the slot for good
		// (the owner, 2026-09-30: weapons favourited back "didn't appear in the wheel menu", after 04:18:54 read the mace's
		// old slot 7 as a spell).
		bool IsSpellPicture(UE::UObject* a_icon)
		{
			static std::unordered_map<UE::UObject*, bool> s_known;
			if (!a_icon) return false;
			if (const auto it = s_known.find(a_icon); it != s_known.end()) return it->second;
			std::string path = pe::Utf8(a_icon->GetFullName());
			for (auto& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			const bool spell = path.find("/icons/magic/") != std::string::npos;
			s_known[a_icon] = spell;
			return spell;
		}


		// The inventory wheel, drawn from the game's keys on the wheel's own material instance: a slot whose key holds an
		// item shows that item's picture at full opacity; a slot without one is empty (a spell the game kept included).
		// Set on the instance directly - silent, unlike SetQuickKeyByIndex - and read back every 100 ms while the wheel is
		// up, because the game puts its own values back whenever it redraws (from a picture list found empty in game,
		// 2026-09-30 04:37 - every slot's opacity was 0 and the equipment never showed: "the inventory wheel is not showing
		// the equipment on the wheel").
		ULONGLONG g_equipLogged = 0;

		void HideSpellKeys(UE::UObject* a_wheel)
		{
			const ULONGLONG now = GetTickCount64();
			if (!quickkeys::TakeRedrew() && now < g_nextEquip) return;
			g_nextEquip = now + 100;   // the inventory walk is not free
			auto* gameImg = GameImage(a_wheel);
			auto* gameMid = gameImg ? ui::BrushResource(gameImg) : nullptr;
			if (!IsMid(gameMid)) return;
			const auto keys = wheels::Ready() ? inventory::Keys() : inventory::KeyMap{};
			const auto icons = quickkeys::ReadIcons();
			int shown = 0, set = 0;
			for (int k = 0; k < 8; ++k) {
				const auto u = static_cast<std::size_t>(k);
				const bool spell = !keys[u] && IsSpellPicture(icons[u]);
				if (spell != g_spellKey[u]) {
					g_spellKey[u] = spell;
					if (spell) logger::info("magic wheel: the game's key {} holds a spell - drawn empty on the inventory wheel", k + 1);
				}
				const std::wstring id = L"ID" + std::to_wstring(k + 1);
				auto* tex = keys[u] ? rows::ItemIcon(keys[u]) : nullptr;
				if (tex && ui::MidTexture(gameMid, (id + L"Texture").c_str()) != tex) {
					ui::SetMidTexture(gameMid, (id + L"Texture").c_str(), tex);
					++set;
				}
				const float want = tex ? 1.0f : 0.0f;
				if (ui::MidScalar(gameMid, (id + L"_Opacity").c_str()) != want) {
					ui::SetMidScalar(gameMid, (id + L"_Opacity").c_str(), want);
					++set;
				}
				shown += tex ? 1 : 0;
			}
			if (set && now - g_equipLogged > 5000) {   // what the game had differed: said at most every 5 s
				g_equipLogged = now;
				logger::info("magic wheel: the inventory wheel drawn from the keys - {} of 8 slots hold an item ({} values set on its material)", shown, set);
			}
		}
	}

	void Tick()
	{
		// a message of the game over the wheel ("Do you want to poison your next shot"): our counters and centre name, drawn above it, would
		// ghost through its paper, so they wait until it is answered (the owner's screenshot, 2026-10-22)
		const bool messageUp = quickkeys::GameMessageUp(false);
		if (messageUp) slotcounts::Hide();
		const auto menu = menus::Active();
		const bool radial = quickkeys::RadialOpen();
		const bool magic = (radial && wheels::Active() == wheels::Wheel::kMagic) || (menu == menus::Menu::kMagic && quickkeys::MagicPanelUp());
		const bool equip = (radial && wheels::Active() == wheels::Wheel::kEquipment) || (menu == menus::Menu::kInventory && quickkeys::PanelOpen());
		auto* wheel = magic || equip || (radial && wheels::Active() == wheels::Wheel::kAmmo) ? quickkeys::VisibleWheel() : nullptr;
		const bool equipPanel = !magic && menu == menus::Menu::kInventory && quickkeys::PanelOpen();   // the Inventory's assign panel
		const bool ammoView = (radial && wheels::Active() == wheels::Wheel::kAmmo) ||
			(equipPanel && wheels::AssignmentWheel(menu) == wheels::Wheel::kAmmo);                      // the radial's third view
		if (ammoView && !wheel) wheel = quickkeys::VisibleWheel();
		if ((magic || equipPanel || ammoView) && wheel) {
			const int mode = ammoView ? 2 : equipPanel ? 1 : 0;
			Show(wheel, mode);
			if (auto* img = GameImage(wheel)) {
				if (!messageUp) slotcounts::Show(img, mode == 2 ? wheels::Wheel::kAmmo : mode == 1 ? wheels::Wheel::kEquipment : wheels::Wheel::kMagic);   // "2/3" on each slot
				if (mode == 2) ringoverlay::Hide(); else ringoverlay::Show(img, mode == 1 ? ringoverlay::Kind::kEquipment : ringoverlay::Kind::kMagic);   // Magic blue, Inventory red
			} else {
				slotcounts::Hide();
				ringoverlay::Hide();
			}
			return;
		}
		Hide(magic ? "no wheel on screen" : "the Magic wheel is not up");
		if (equip && wheel) {
			HideSpellKeys(wheel);
			if (auto* img = GameImage(wheel)) {
				if (!messageUp) slotcounts::Show(img);   // "3/5" on each slot
				ringoverlay::Show(img, ringoverlay::Kind::kEquipment);   // the Equipment wheel is red
			} else {
				slotcounts::Hide();
				ringoverlay::Hide();
			}
		} else {
			slotcounts::Hide();
				ringoverlay::Hide();
		}
	}

	bool IsSpellKey(int a_slot)
	{
		return a_slot >= 0 && a_slot < 8 && g_spellKey[static_cast<std::size_t>(a_slot)];
	}

	std::string Status()
	{
		int spells = 0;
		for (const bool s : g_spellKey) spells += s ? 1 : 0;
		return std::format("{}; {} of the game's keys hold a spell (drawn empty on the inventory wheel)", g_status, spells);
	}
}
