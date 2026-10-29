#include "overlays/WheelName.h"
#include "engine/UiText.h"

#include "game/Inventory.h"
#include "game/Menus.h"
#include "game/QuickKeys.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "game/Rows.h"
#include "core/Settings.h"
#include "overlays/SlotCounts.h"
#include "engine/Ui.h"
#include "wheels/Wheels.h"

namespace wheelname
{
	namespace
	{
		reflect::Handle g_root, g_label, g_slot;
		int         g_builds = 0;
		ULONGLONG   g_lastBuild = 0, g_nextLook = 0;
		bool        g_shown = false;
		std::string g_text;
		int             g_detailsHidden = -1;
		reflect::Handle g_details;   // the game's own centre text of the wheel (quickKeys_details_text): "Shortcut" until set
		constexpr const wchar_t* kFullNames = L"/Game/Localization/StringTables/ST_FullNames.ST_FullNames";
		double      g_x = -1e9, g_y = -1e9;

		constexpr float kTitleInk[4] = { 0.80f, 0.62f, 0.28f, 1.0f };   // the gold of the wheel's ring
		constexpr float kTitleInkRed[4] = { 0.515f, 0.010f, 0.013f, 1.0f };    // the Equipment wheel's red (the redesign's crimson, lifted so it reads on a dark scene), linear
		constexpr float kTitleInkBlue[4] = { 0.013f, 0.090f, 0.644f, 1.0f };   // the Magic wheel's blue (the redesign's navy, lifted), linear
		constexpr float kTitleOutline[4] = { 0.0f, 0.0f, 0.0f, 1.0f };   // black round it, so it reads on any scene (the Ammo wheel)
		constexpr float kTitleOutlineGold[4] = { 0.831f, 0.429f, 0.038f, 1.0f };     // the Magic wheel's gold rim, linear
		constexpr float kTitleOutlineSilver[4] = { 0.610f, 0.644f, 0.730f, 1.0f };   // the Equipment wheel's silver rim, linear

		// the title's own look: the wheel's gold with a soft shadow
		void TitleInk(UE::UObject* a_label)
		{
			constexpr float kSoftShadow[4] = { 0.0f, 0.0f, 0.0f, 0.55f };
			uix::Ink(a_label, kTitleInk);
			uix::Shadow(a_label, kSoftShadow, 1.0);
		}

		bool Build()
		{
			const ULONGLONG now = GetTickCount64();
			if (g_lastBuild && now - g_lastBuild < 2000) return false;
			g_lastBuild = now;
			auto* root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			const std::wstring n = std::to_wstring(++g_builds);
			auto* canvas = root ? ui::RootCanvas(root, (L"IwmNameCanvas" + n).c_str()) : nullptr;
			if (!canvas) return false;
			UE::UObject* slot = nullptr;
			auto* label = ui::AddToCanvas(canvas, L"/Script/UMG.TextBlock", (L"IwmNameLabel" + n).c_str(), &slot);
			if (!label || !slot) {
				logger::info("wheel name: the game's text prefab is not loaded yet");
				return false;
			}
			ui::Anchors(slot, 0, 0, 0, 0);
			ui::Vec2(slot, L"SetAlignment", 0.5, 1.0);   // centred, its bottom on the point given
			const bool yes = true;
			ui::CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			ui::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 60);   // above the menus' own layers
			vp.Run();
			TitleInk(label);
			uix::ApplyBoldFont(label, 30.0, kTitleOutline, 2);   // the owner, 2026-10-22: one pixel thicker
			ui::Visible(root, false);
			g_root = reflect::Hold(root);
			g_label = reflect::Hold(label);
			g_slot = reflect::Hold(slot);
			g_shown = false;
			g_text.clear();
			g_x = g_y = -1e9;
			logger::info("wheel name: the label is built (build {})", g_builds);
			return true;
		}

		void SetText(UE::UObject* a_label, const std::string& a_text)
		{
			if (!uix::SetText(a_label, a_text)) return;
			TitleInk(a_label);
			uix::ApplyBoldFont(a_label, 0, kTitleOutline, 2);
		}

		// a string-table key (LOC_FN_...) as the game's table gives it; anything else unchanged
		std::string Resolve(const std::string& a_text)
		{
			if (!a_text.starts_with("LOC_")) return a_text;
			static auto* lib = ui::Class(L"/Script/Engine.KismetTextLibrary");
			ui::Call t(lib ? lib->GetDefaultObject(false) : nullptr, L"TextFromStringTable");
			void* id = t.At("TableId");
			void* key = t.At("Key");
			void* ret = t.At("ReturnValue");
			if (!id || !key || !ret) return a_text;
			const std::wstring w = pe::Wide(a_text);
			new (id) UE::FName(kFullNames, UE::EFindName::Add);
			auto* k = new (key) UE::FString(w.c_str());
			t.Run();
			auto* got = static_cast<UE::FText*>(ret);
			std::string out = reflect::Text(*got);
			got->~FText();
			k->~FString();
			return out.empty() ? a_text : out;
		}

		// The name of what the pointed slot holds, in the wheel's own centre text (the game left "Shortcut" there: it names
		// its own key data, which the Magic wheel does not use and the inventory wheel's panel does not fill). Nothing
		// pointed: the game's text is left alone.
		void UpdateDetails(UE::UObject* a_wheel, int a_mode)   // 0 Equipment keys, 1 Magic spells, 2 Ammo
		{
			// the label of THIS wheel (the radial, the inventory's panel and the magic menu's panel are three instances)
			static UE::UObject* s_wheel = nullptr;
			auto* label = reflect::Get(g_details);
			if (!label || s_wheel != a_wheel) {
				label = ui::FindInTree(a_wheel, "quickKeys_details_text");
				s_wheel = a_wheel;
				g_details = label ? reflect::Hold(label) : reflect::Handle{};
				g_detailsHidden = -1;
				if (!label) return;
			}
			// the Magic wheel's picture covers the game's centre text and the game rewrites it ("Shortcut") as it likes: hidden there,
			// the name is drawn above the picture instead
			const bool overlay = true;   // every wheel's centre name is drawn by us (SlotCounts.cpp): one font, colour and size on all three
			if (g_detailsHidden != (overlay ? 1 : 0)) {
				ui::Visible(label, !overlay);
				g_detailsHidden = overlay ? 1 : 0;
			}
			const int pointed = quickkeys::PointedSlot();   // 1..8, 0 / -1 none
			// nothing pointed, or an empty slot: blank (the game's own text there is "Shortcut")
			const auto id = pointed >= 1 && pointed <= 8 ? (a_mode == 1 ? wheels::MagicSlots()[static_cast<std::size_t>(pointed - 1)] :a_mode == 2 ? wheels::AmmoSlots()[static_cast<std::size_t>(pointed - 1)] : (wheels::Ready() ? inventory::Keys()[static_cast<std::size_t>(pointed - 1)] : 0u)) : 0u;
			std::string text = " ";
			if (id) {
				std::string name = a_mode == 1 ? rows::SpellName(id) : inventory::NameOf(id);
				if (name.starts_with("UI_UserInputText")) {   // a name the player gave: the form keeps a placeholder
					name = rows::DisplayName(id);
				}
				if (!name.empty()) text = name;
			}
			if (overlay) {
				// the Magic wheel's picture covers the game's centre text: the name is drawn above it (SlotCounts.cpp), the game's blanked
				slotcounts::SetCentre(text == " " ? std::string() : Resolve(text));
			} else {
				slotcounts::SetCentre({});
				uix::SetItemName(label, text);
			}
		}

		// where the wheel widget is on the viewport (viewport units): its top-left and its width
		bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_width)
		{
			auto* pc = ui::PlayerController();
			static auto* lib = ui::Class(L"/Script/UMG.SlateBlueprintLibrary");
			auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			if (!a_w || !pc || !cdo) return false;
			ui::Call geo(a_w, L"GetCachedGeometry");
			const auto gsize = geo.Size("ReturnValue");
			if (!geo || gsize <= 0 || !geo.Run()) return false;
			ui::Call size(cdo, L"GetLocalSize");
			void* g = size.At("Geometry");
			if (!size || !g || size.Size("Geometry") != gsize) return false;
			std::memcpy(g, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
			size.Run();
			const auto local = size.Get<std::array<double, 2>>("ReturnValue");
			const auto toViewport = [&](double a_lx, double a_ly, double& a_vx, double& a_vy) {
				ui::Call c(cdo, L"LocalToViewport");
				void* gg = c.At("Geometry");
				if (!c || !gg || c.Size("Geometry") != gsize) return false;
				c.Set("WorldContextObject", pc);
				std::memcpy(gg, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
				const double lc[2] = { a_lx, a_ly };
				c.Set("LocalCoordinate", lc);
				if (!c.RunGuarded()) return false;   // a world-context call: fault-guarded (a quit, a load)
				const auto vpos = c.Get<std::array<double, 2>>("ViewportPosition");
				a_vx = vpos[0];
				a_vy = vpos[1];
				return true;
			};
			double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (!toViewport(0.0, 0.0, x0, y0) || !toViewport(local[0], 0.0, x1, y1)) return false;
			a_x = x0;
			a_y = y0;
			a_width = x1 - x0;
			return local[0] > 0.0 && a_width > 1.0;
		}

		void Hide()
		{
			if (!g_shown) return;
			ui::Visible(reflect::Get(g_root), false);
			g_shown = false;
		}
	}

	void Tick()
	{
		const ULONGLONG now = GetTickCount64();
		static int s_lastPointed = -9;
		const int  pointedNow = quickkeys::PointedSlot();
		const bool pointedMoved = pointedNow != s_lastPointed;   // the game rewrites its text as the pointer moves: answer at once
		if (now < g_nextLook && !pointedMoved) return;
		s_lastPointed = pointedNow;
		g_nextLook = now + 100;

		// which wheel is on screen
		const auto menu = menus::Active();
		const char* name = nullptr;
		if (quickkeys::RadialOpen()) {
			name = wheels::Active() == wheels::Wheel::kMagic ? "Magic Wheel" : wheels::Active() == wheels::Wheel::kAmmo ? "Ammo Wheel" : "Inventory Wheel";
		} else if (menu == menus::Menu::kInventory && quickkeys::PanelOpen()) {
			name = wheels::AssignmentWheel(menu) == wheels::Wheel::kAmmo ? "Ammo Wheel" : "Inventory Wheel";
		} else if (menu == menus::Menu::kMagic && quickkeys::MagicPanelUp()) {
			name = "Magic Wheel";
		}
		auto* wheel = name ? quickkeys::VisibleWheel() : nullptr;
		if (!wheel) {
			Hide();
			return;
		}
		UpdateDetails(wheel, std::string_view(name) == "Magic Wheel" ? 1 : std::string_view(name) == "Ammo Wheel" ? 2 : 0);
		if (!reflect::Get(g_root) && !Build()) return;
		auto* root = reflect::Get(g_root);
		auto* label = reflect::Get(g_label);
		auto* slot = reflect::Get(g_slot);
		if (!root || !label || !slot) return;
		double x = 0, y = 0, w = 0;
		if (!Measure(wheel, x, y, w)) {
			Hide();
			return;
		}
		if (g_text != name) {
			SetText(label, name);
			g_text = name;
			const auto&            theme = settings::Get().theme;   // INI [Theme]: a wheel switched off keeps the gold title with the black outline
			const std::string_view title = (std::string_view(name) == "Inventory Wheel" && !theme.inventory) || (std::string_view(name) == "Magic Wheel" && !theme.magic) ? std::string_view("Ammo Wheel") : std::string_view(name);
			uix::Ink(label, title == "Inventory Wheel" ? kTitleInkRed : title == "Magic Wheel" ? kTitleInkBlue : kTitleInk);   // SetText puts the gold back
			uix::ApplyBoldFont(label, 0, title == "Inventory Wheel" ? kTitleOutlineSilver : title == "Magic Wheel" ? kTitleOutlineGold : kTitleOutline, 2);   // the rim's metal round the letters (the owner, 2026-10-22)
		}
		const double px = x + w * 0.5, py = y - 6.0;   // centred over the wheel, just above its top
		if (std::abs(px - g_x) > 0.5 || std::abs(py - g_y) > 0.5) {
			ui::Vec2(slot, L"SetPosition", px, py);
			g_x = px, g_y = py;
		}
		if (!g_shown) {
			ui::Visible(root, true);
			g_shown = true;
		}
	}
}
