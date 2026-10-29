#include "ammo/Ammo.h"
#include "ammo/AmmoLayout.h"
#include "engine/UiText.h"

#include "game/Inventory.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "game/Rows.h"
#include "core/Settings.h"
#include "slowtime/SlowTime.h"
#include "engine/TesThread.h"
#include "engine/Ui.h"
#include "wheels/Wheels.h"

#include <numbers>

namespace ammo
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr int    kSlots = 8;
		constexpr double kStickOn = 0.45;     // the right stick points once it is this far out
		constexpr double kStickRest = 0.25;   // and is at rest inside this
		constexpr WORD   kDpad = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT;
		// the main wheel's own slot circle (a gold rim, a dark inside): the texture its material draws each slot with,
		// found in the paks with uetex (/Game/Art/UI/Modern/HUD/QuickKeys/NewTextures) - the owner, 2026-09-30: "each arrow
		// should have the same circle art as the main wheel for each arrow slot". The visible circle is 160 of its 256 px.
		constexpr const wchar_t* kSlotCircle = L"/Game/Art/UI/Modern/HUD/QuickKeys/NewTextures/T_QuickKeys_SingleCircle_D.T_QuickKeys_SingleCircle_D";
		constexpr double         kCircleShare = 160.0 / 256.0;

		// ---- the widget (game thread; kept across frames as slot-checked handles) ----
		reflect::Handle g_root, g_label;
		std::array<reflect::Handle, kSlots> g_icon, g_iconSlot, g_ring, g_ringSlot;   // each entry: its circle, its arrows on it
		bool      g_centred = true;   // full ring; legacy half layout falls back here above five entries
		std::array<int, kSlots> g_savedSlots{};
		int       g_layoutCount = -1;   // the entry count the positions were last laid out for

		// The game's own wheel, sliced in half (the owner, 2026-09-30: "the arrows icons and slot outline is too small ... the
		// highlighting effect is too dim ... basically just a carbon copy of the game's wheel menu just sliced in half"). The
		// HUD wheel is ONE image drawn by a dynamic instance of MIC_UI_QuickKeys (634 across; parameters ID1..ID8Texture,
		// ID1..ID8_Opacity, SelectorRotator, SelectorArrowAlpha - the primary session's reads). A second instance on an image,
		// clipped just past the wheel's middle, keeps the left five slots whole: 1 at the top, 8, 7 at the left, 6, 5 at the
		// bottom (slot n sits 45 * (n - 1) degrees clockwise from the top). The arrows go into those slots' textures, the
		// game's own selector points at the chosen one. The drawn circles below stay as the fallback.
		constexpr const wchar_t* kWheelMic = L"/Game/UI/Materials/QuickKeys/MIC_UI_QuickKeys.MIC_UI_QuickKeys";
		constexpr double         kOverhang = 0.14;   // past the middle, in wheel radii: the top and bottom slots stay whole
		constexpr int            kHalfSlots = 5;
		constexpr float          kHiddenSelector = 2.0f / 8.0f;   // slot 3, straight right: outside the half that shows
		reflect::Handle          g_mid, g_wheelImg;
		bool                     g_material = false;
		std::array<int, kSlots>  g_entryId{};            // the wheel slot (1-8) each entry sits in
		std::array<UE::UObject*, 9> g_midTex{};          // the texture last set in each slot (1-8)
		std::array<float, 9>        g_midOpacity{};
		int                         g_midPointed = -2;

		// entries fill the half from its middle: one at 7, two at 8 / 6, then out to 1 and 5
		std::array<int, kHalfSlots> HalfSlots(int a_n)
		{
			switch (a_n) {
			case 1: return { 7, 0, 0, 0, 0 };
			case 2: return { 8, 6, 0, 0, 0 };
			case 3: return { 8, 7, 6, 0, 0 };
			case 4: return { 1, 8, 6, 5, 0 };
			default: return { 1, 8, 7, 6, 5 };
			}
		}

		// slot n's direction as a math angle (0 right, 90 up, counter-clockwise)
		double SlotMathAngle(int a_slot)
		{
			double a = 90.0 - 45.0 * (a_slot - 1);
			while (a < 0.0) a += 360.0;
			return a;
		}

		void MidScalar(UE::UObject* a_mid, const wchar_t* a_name, float a_v)
		{
			ui::Call c(a_mid, L"SetScalarParameterValue");
			c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
			c.Set("Value", a_v);
			c.Run();
		}

		void MidTexture(UE::UObject* a_mid, const wchar_t* a_name, UE::UObject* a_tex)
		{
			ui::Call c(a_mid, L"SetTextureParameterValue");
			c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
			c.Set("Value", a_tex);
			c.Run();
		}
		// A dynamic instance of the wheel's material on a_image: every slot empty, the selector on the hidden half. The
		// caches of what was written are reset, so the next draw writes everything again.
		UE::UObject* MakeMid(UE::UObject* a_image, UE::UObject* a_mic)
		{
			static auto* mlib = ui::Class(L"/Script/Engine.KismetMaterialLibrary");
			ui::Call c(mlib ? mlib->GetDefaultObject(false) : nullptr, L"CreateDynamicMaterialInstance");
			c.Set("WorldContextObject", ui::PlayerController());
			c.Set("Parent", a_mic);
			auto* mid = c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
			if (!mid || !a_image) return nullptr;
			ui::Call b(a_image, L"SetBrushFromMaterial");
			b.Set("Material", mid);
			if (!b.Run()) return nullptr;
			for (int k = 1; k <= 8; ++k) MidScalar(mid, (L"ID" + std::to_wstring(k) + L"_Opacity").c_str(), 0.0f);
			MidScalar(mid, L"SelectorArrowAlpha", 0.0f);
			MidScalar(mid, L"SelectorRotator", kHiddenSelector);
			g_midTex = {};
			g_midOpacity.fill(-1.0f);
			g_midPointed = -2;
			return mid;
		}

		// what the wheel's image draws now: its brush's ResourceObject (rule 30 - asked of the object, not assumed)
		UE::UObject* DrawnResource(UE::UObject* a_image)
		{
			static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
			if (!a_image || !brushStruct) return nullptr;
			const auto brushOff = reflect::Offset(reinterpret_cast<UE::UStruct*>(a_image->GetClass()), "Brush");
			const auto resOff = reflect::Offset(brushStruct, "ResourceObject");
			if (brushOff < 0 || resOff < 0) return nullptr;
			return *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_image) + brushOff + resOff);
		}

		// On every open: the image must still draw OUR instance (the owner, 2026-09-30: no arrows and "box number eight is
		// highlighted permanently" - the primary's read found no instance of ours while the wheel was open, so every
		// parameter written went nowhere and the image showed the material's defaults). Made again when it does not.
		void EnsureMid()
		{
			if (!g_material) return;
			auto* img = reflect::Get(g_wheelImg);
			auto* mid = reflect::Get(g_mid);
			auto* drawn = DrawnResource(img);
			if (img && mid && drawn == mid) return;
			auto* mic = ui::Load(kWheelMic);
			auto* made = img && mic ? MakeMid(img, mic) : nullptr;
			g_mid = made ? reflect::Hold(made) : reflect::Handle{};
			logger::warn("ammo: the wheel's material instance was {} (its image draws {}) - {}", mid ? "not the one drawn" : "gone",
				drawn ? pe::Utf8(drawn->GetFName().ToString()) : std::string("nothing"), made ? "made again" : "could NOT be made again");
		}

		std::array<std::uint32_t, kSlots>   g_shownIds{};
		int       g_builds = 0;
		ULONGLONG g_lastBuild = 0;
		std::string g_shownText;

		// ---- state (game thread) ----
		bool              g_open = false;
		int               g_pointed = -1;
		bool              g_pointedByStick = false;
		bool              g_resting = true;
		Clock::time_point g_restSince{};
		std::array<std::uint32_t, kSlots> g_ids{};   // what the wheel holds while it is open: the favourited arrows, packed
		int               g_count = 0;               // how many of g_ids are entries
		std::uint32_t     g_worn = 0;                // the arrows worn when it opened
		int               g_drawnPointed = -2;
		WORD              g_swallow = 0;             // buttons the wheel used: the game never sees them, until let go

		double Radius() { return 317.0 * settings::Get().ammoScalePercent / 100.0; }   // half of the HUD wheel's 634

		// The entries, as in the Skyrim Perfected Wheeler's ammo wheel (the owner, 2026-09-30: "if you only have one arrow
		// ... favorited to it, then it centers to the middle of the semicircle and for every additional favorited arrow type
		// it adds an additional radial entry"): n entries centred on 180 degrees (the middle of the half circle, straight
		// left; straight up is 90), 22.5 degrees apart, closer when that would reach past 112.5 / 247.5 - where the end
		// ones would touch the screen's edge ("the arrows appear slightly off screen").
		constexpr double kArcFrom = 112.5, kArcTo = 247.5, kStep = 22.5;
		// The half circle divided evenly (the owner, 2026-09-30: "The iron and steel arrow overlap in the ammo wheel ... It
		// should be dividing up the circumference evenly not pushing them both into the center"): n equal sectors from
		// straight up (90) to straight down (270), each entry at its sector's middle - one entry sits in the middle (180),
		// two at 135 / 225, eight 22.5 apart.
		double EntryAngle(int a_i, int a_n)
		{
			if (a_n <= 1) return 180.0;
			return 90.0 + (a_i + 0.5) * 180.0 / a_n;
		}

		std::array<double, 2> Place(int a_i, int a_n)
		{
			const double R = Radius(), r = R * 0.62;
			const double th = (g_centred ? SlotMathAngle(g_savedSlots[a_i] + 1) : EntryAngle(a_i, a_n)) * std::numbers::pi / 180.0;
			return { R + r * std::cos(th), R - r * std::sin(th) };
		}

		// each entry's circle and arrows placed for a_n entries; the rest hidden
		void Layout(int a_n)
		{
			if (a_n == g_layoutCount) return;
			if (g_material) {
				const auto slots = HalfSlots(a_n);
				g_entryId.fill(0);
				for (int i = 0; i < a_n && i < kSlots; ++i) g_entryId[i] = g_centred ? g_savedSlots[i] + 1 : slots[i];
				g_layoutCount = a_n;
				g_shownIds = {};
				g_midPointed = -2;
				return;
			}
			for (int i = 0; i < kSlots; ++i) {
				const bool on = i < a_n;
				if (on) {
					const auto p = Place(i, a_n);
					if (auto* rs = reflect::Get(g_ringSlot[i])) ui::Vec2(rs, L"SetPosition", p[0], p[1]);
					if (auto* is = reflect::Get(g_iconSlot[i])) ui::Vec2(is, L"SetPosition", p[0], p[1]);
				}
				ui::Visible(reflect::Get(g_ring[i]), on);
				if (!on) ui::Visible(reflect::Get(g_icon[i]), false);
			}
			g_layoutCount = a_n;
			g_shownIds = {};   // the pictures are drawn again for the new places
		}

		bool Build()
		{
			const ULONGLONG now = GetTickCount64();
			if (g_lastBuild && now - g_lastBuild < 2000) {
				return false;
			}
			g_lastBuild = now;
			auto* root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			const std::wstring n = std::to_wstring(++g_builds);
			auto* canvas = root ? ui::RootCanvas(root, (L"IwmAmmoCanvas" + n).c_str()) : nullptr;
			if (!canvas) {
				logger::info("ammo: the wheel cannot be built yet ({})", !ui::PlayerController() ? "no player controller" :
					!root ? "the user widget could not be created" : "its canvas could not be made");
				return false;
			}
			const double R = Radius();
			UE::UObject* panelSlot = nullptr;
			auto* panel = ui::AddToCanvas(canvas, L"/Script/UMG.CanvasPanel", (L"IwmAmmoPanel" + n).c_str(), &panelSlot);
			if (!panel || !panelSlot) {
				logger::warn("ammo: the wheel's panel could not be made");
				return false;
			}
			const bool centred = g_centred;   // INI [AmmoWheel] uPosition: the whole ring in the middle of the screen
			if (centred) {
				ui::Anchors(panelSlot, 0.5, 0.5, 0.5, 0.5);
				ui::Vec2(panelSlot, L"SetAlignment", 0.5, 0.5);
				ui::Vec2(panelSlot, L"SetPosition", 0.0, 0.0);
				ui::Vec2(panelSlot, L"SetSize", 2.0 * R, 2.0 * R);
			} else {
				ui::Anchors(panelSlot, 1.0, 0.5, 1.0, 0.5);   // locked to the middle of the right-hand edge
				ui::Vec2(panelSlot, L"SetAlignment", 1.0, 0.5);
				ui::Vec2(panelSlot, L"SetPosition", 0.0, 0.0);
				ui::Vec2(panelSlot, L"SetSize", R, 2.0 * R);
			}
			const std::uint8_t clip = 1;   // EWidgetClipping::ClipToBounds: the round back shows its left half - the semi-circle
			ui::CallFirst(panel, L"SetClipping", &clip, sizeof(clip));

			// the game's own wheel, when its material can be instanced
			g_material = false;
			g_mid = {};
			g_midTex = {};
			g_midOpacity.fill(-1.0f);
			g_midPointed = -2;
			g_wheelImg = {};
			if (auto* mic = ui::Load(kWheelMic)) {
				UE::UObject* ws = nullptr;
				auto* wheelImg = ui::AddToCanvas(panel, L"/Script/UMG.Image", (L"IwmAmmoWheel" + n).c_str(), &ws);
				if (wheelImg && ws) {
					ui::Anchors(ws, 0, 0, 0, 0);
					ui::Vec2(ws, L"SetPosition", 0.0, 0.0);
					ui::Vec2(ws, L"SetSize", 2.0 * R, 2.0 * R);
					if (auto* mid = MakeMid(wheelImg, mic)) {
						if (!centred) ui::Vec2(panelSlot, L"SetSize", R * (1.0 + kOverhang), 2.0 * R);
						g_mid = reflect::Hold(mid);
						g_wheelImg = reflect::Hold(wheelImg);
						g_material = true;
					} else {
						ui::Visible(wheelImg, false);
					}
				}
			}
			logger::info("ammo: the wheel is {}", g_material ? (g_centred ? "the game's full eight-cell Ammo ring" : "the game's own (MIC_UI_QuickKeys), its left half") : "drawn (the game's wheel material could not be instanced)");
			const double panelW = centred ? R : g_material ? R * (1.0 + kOverhang) : R;   // how far the name sits left of the wheel's edge

			UE::UObject* backSlot = nullptr;
			if (auto* back = g_material ? nullptr : ui::AddToCanvas(panel, L"/Script/UMG.Image", (L"IwmAmmoBack" + n).c_str(), &backSlot)) {
				ui::Anchors(backSlot, 0, 0, 0, 0);
				ui::Vec2(backSlot, L"SetPosition", 0.0, 0.0);
				ui::Vec2(backSlot, L"SetSize", 2.0 * R, 2.0 * R);
				const float fill[4] = { 0.05f, 0.04f, 0.03f, 0.72f };
				const float edge[4] = { 0.90f, 0.87f, 0.82f, 0.85f };
				if (!ui::RoundedBox(back, true, fill, edge, 3.0f)) {
					logger::warn("ammo: the wheel's round back could not be drawn (the brush fields were not found)");
				}
			}
			// every entry: the main wheel's own slot circle (a drawn disc with a light rim if the texture cannot be loaded),
			// and the arrows' picture on it. The circle shows 0.2 R across, so eight entries on the arc never overlap.
			auto* circle = ui::Load(kSlotCircle);
			logger::info("ammo: the slot circle is {}", circle ? "the main wheel's own (T_QuickKeys_SingleCircle_D)" : "drawn (the game's texture could not be loaded)");
			for (int i = 0; i < kSlots; ++i) {
				g_ring[i] = g_ringSlot[i] = {};
				UE::UObject* rs = nullptr;
				if (auto* ring = g_material ? nullptr : ui::AddToCanvas(panel, L"/Script/UMG.Image", (L"IwmAmmoRing" + n + L"_" + std::to_wstring(i)).c_str(), &rs)) {
					ui::Anchors(rs, 0, 0, 0, 0);
					ui::Vec2(rs, L"SetAlignment", 0.5, 0.5);
					const double side = circle ? R * 0.20 / kCircleShare : R * 0.20;
					ui::Vec2(rs, L"SetSize", side, side);
					if (circle) {
						ui::Call b(ring, L"SetBrushFromTexture");
						b.Set("Texture", circle);
						b.Set("bMatchSize", false);
						b.Run();
					} else {
						const float fill[4] = { 0.02f, 0.02f, 0.02f, 0.92f };
						const float rim[4] = { 0.80f, 0.76f, 0.68f, 0.95f };
						ui::RoundedBox(ring, true, fill, rim, 2.5f);
					}
					ui::Vec2(ring, L"SetRenderTransformPivot", 0.5, 0.5);
					ui::Visible(ring, false);
					g_ring[i] = reflect::Hold(ring);
					g_ringSlot[i] = reflect::Hold(rs);
				}
			}
			for (int i = 0; i < kSlots; ++i) {
				g_icon[i] = g_iconSlot[i] = {};
				UE::UObject* s = nullptr;
				auto* img = g_material ? nullptr : ui::AddToCanvas(panel, L"/Script/UMG.Image", (L"IwmAmmoIcon" + n + L"_" + std::to_wstring(i)).c_str(), &s);
				if (!img) {
					continue;
				}
				ui::Anchors(s, 0, 0, 0, 0);
				ui::Vec2(s, L"SetAlignment", 0.5, 0.5);
				ui::Vec2(s, L"SetSize", R * 0.155, R * 0.155);   // the arrows inside the circle's rim
				ui::Vec2(img, L"SetRenderTransformPivot", 0.5, 0.5);
				ui::Visible(img, false);
				g_icon[i] = reflect::Hold(img);
				g_iconSlot[i] = reflect::Hold(s);
			}
			g_layoutCount = -1;
			// the pointed arrows' name, left of the wheel (outside the clipped panel)
			UE::UObject* labelSlot = nullptr;
			auto* label = ui::AddToCanvas(canvas, L"/Script/UMG.TextBlock", (L"IwmAmmoLabel" + n).c_str(),
				&labelSlot);
			if (label) {
				// the pointed arrows' name inside the wheel, as the other wheels show theirs: on the ring's middle, or (the half wheel) inside the half ring
				if (centred) {
					ui::Anchors(labelSlot, 0.5, 0.5, 0.5, 0.5);
					ui::Vec2(labelSlot, L"SetPosition", 0.0, 0.0);
				} else {
					ui::Anchors(labelSlot, 1.0, 0.5, 1.0, 0.5);
					ui::Vec2(labelSlot, L"SetPosition", -0.3 * R, 0.0);
				}
				ui::Vec2(labelSlot, L"SetAlignment", 0.5, 0.5);
				const bool yes = true;
				ui::CallFirst(labelSlot, L"SetAutoSize", &yes, sizeof(yes));
			} else {
				logger::info("ammo: the game's text prefab is not loaded - the wheel shows no name");
			}
			ui::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 20);
			vp.Run();
			if (label) {
				uix::InsideWheelInk(label);   // after the Slate widget exists
				uix::ApplyFont(label, 25.0 * settings::Get().display.centreTextPercent / 100.0);   // the size the other wheels' centre names have
			}
			g_root = reflect::Hold(root);
			g_label = label ? reflect::Hold(label) : reflect::Handle{};
			g_shownIds = {};
			g_shownText.clear();
			g_drawnPointed = -2;
			ui::Visible(root, false);
			logger::info("ammo: the wheel is built (build {}; {} layout, {:.0f} tall)", g_builds, g_centred ? "full eight-cell" : "compact half", 2.0 * R);
			return true;
		}

		bool Built() { return reflect::Get(g_root) != nullptr; }

		// the name of what is pointed: a string-table key (LOC_FN_...) through the game's own table, as the UI shows it
		void SetLabel(const std::string& a_text)
		{
			auto* label = reflect::Get(g_label);
			if (!label || a_text == g_shownText) {
				return;
			}
			if (!uix::SetItemName(label, a_text)) {
				return;
			}
			g_shownText = a_text;
			uix::InsideWheelInk(label);
			uix::ApplyFont(label, 0);
		}

		void DrawMaterial()
		{
			auto* mid = reflect::Get(g_mid);
			if (!mid) return;
			std::array<UE::UObject*, 9> tex{};
			for (int i = 0; i < g_count && i < kSlots; ++i) {
				const int slot = g_entryId[static_cast<std::size_t>(i)];
				if (slot >= 1 && slot <= 8 && g_ids[i]) {
					tex[static_cast<std::size_t>(slot)] = rows::ItemIcon(g_ids[i]);
					if (!tex[static_cast<std::size_t>(slot)] && g_ids[i] != g_shownIds[i]) {
						logger::info("ammo: no picture for {} - no inventory row showed it and its form's icon did not load (see the rows: line)", inventory::NameOf(g_ids[i]));
					}
				}
			}
			g_shownIds = g_ids;
			for (int k = 1; k <= 8; ++k) {
				auto* t = tex[static_cast<std::size_t>(k)];
				if (t && t != g_midTex[static_cast<std::size_t>(k)]) {
					MidTexture(mid, (L"ID" + std::to_wstring(k) + L"Texture").c_str(), t);
					g_midTex[static_cast<std::size_t>(k)] = t;
				}
				const float op = t ? 1.0f : 0.0f;
				if (op != g_midOpacity[static_cast<std::size_t>(k)]) {
					MidScalar(mid, (L"ID" + std::to_wstring(k) + L"_Opacity").c_str(), op);
					g_midOpacity[static_cast<std::size_t>(k)] = op;
				}
			}
			if (g_pointed != g_midPointed) {
				// the game's own selector: SelectorRotator is a fraction of a turn clockwise from the top (slot n = (n-1)/8)
				const int slot = g_pointed >= 0 && g_pointed < kSlots ? g_entryId[static_cast<std::size_t>(g_pointed)] : 0;
				// nothing pointed (or an empty wheel): the selector goes to slot 3, in the clipped-off right half - the
				// material's own default lights slot 8 (the owner, 2026-09-30: "box number eight is highlighted permanently")
				MidScalar(mid, L"SelectorRotator", slot >= 1 ? static_cast<float>(slot - 1) / 8.0f : kHiddenSelector);
				MidScalar(mid, L"SelectorArrowAlpha", slot >= 1 ? 1.0f : 0.0f);
				g_midPointed = g_pointed;
			}
			SetLabel(g_pointed >= 0 && g_ids[g_pointed] ? inventory::NameOf(g_ids[g_pointed]) : std::string(" "));
			g_drawnPointed = g_pointed;
		}

		void Draw()
		{
			if (g_material) {
				DrawMaterial();
				return;
			}
			for (int i = 0; i < kSlots; ++i) {
				auto* img = reflect::Get(g_icon[i]);
				if (!img || g_ids[i] == g_shownIds[i]) {
					continue;
				}
				auto* icon = g_ids[i] ? rows::ItemIcon(g_ids[i]) : nullptr;
				if (icon) {
					ui::Call b(img, L"SetBrushFromTexture");
					b.Set("Texture", icon);
					b.Set("bMatchSize", false);
					b.Run();
				} else if (g_ids[i]) {
					logger::info("ammo: no picture for {} - no inventory row showed it and its form's icon did not load (see the rows: line)", inventory::NameOf(g_ids[i]));
				}
				ui::Visible(img, icon != nullptr);
				g_shownIds[i] = g_ids[i];
				g_drawnPointed = -2;
			}
			if (g_pointed == g_drawnPointed) {
				return;
			}
			for (int i = 0; i < g_count; ++i) {
				if (auto* ring = reflect::Get(g_ring[i])) {
					const bool on = i == g_pointed;
					ui::Vec2(ring, L"SetRenderScale", 1.0, 1.0);   // every entry the same size: the pointed one is lit, not grown
					ui::Colour(ring, on ? 1.0f : 0.8f, on ? 0.92f : 0.8f, on ? 0.7f : 0.8f, 1.0f);   // the pointed one brighter and warmer
				}
			}
			for (int i = 0; i < kSlots; ++i) {
				auto* img = reflect::Get(g_icon[i]);
				if (!img || !g_ids[i]) {
					continue;
				}
				const bool on = i == g_pointed;
				ui::Vec2(img, L"SetRenderScale", 1.0, 1.0);
				if (g_ids[i] == g_worn) {
					ui::Colour(img, 1.0f, 0.82f, 0.35f, on ? 1.0f : 0.85f);   // the arrows worn now: gold
				} else {
					ui::Colour(img, 1.0f, 1.0f, 1.0f, on ? 1.0f : 0.7f);
				}
			}
			SetLabel(g_pointed >= 0 && g_ids[g_pointed] ? inventory::NameOf(g_ids[g_pointed]) : std::string(" "));
			g_drawnPointed = g_pointed;
		}

		bool g_lookLocked = false;

		// While the wheel is open the mouse points at arrows: the game's look input is ignored (the camera used to turn with the mouse,
		// the owner, 2026-10-16) and the cursor shown. Always put back - once, balanced - when the wheel closes.
		void LockLook(bool a_on)
		{
			if (a_on == g_lookLocked) return;
			auto* pc = ui::PlayerController();
			if (!pc) return;
			g_lookLocked = a_on;
			ui::Call look(pc, L"SetIgnoreLookInput");
			if (look) {
				look.Set<bool>("bNewLookInput", a_on);
				look.Run();
			}
			ui::Call cursor(pc, L"SetShowMouseCursor");
			if (cursor) {
				cursor.Set<bool>("bShow", a_on);
				cursor.Run();
			}
		}

		void Close(const char* a_why)
		{
			slowtime::CloseRequested();
			g_open = false;
			g_pointed = -1;
			LockLook(false);
			ui::Visible(reflect::Get(g_root), false);
			logger::info("ammo: the wheel closed ({})", a_why);
		}

		// the next filled slot from a_from (-1 = none yet) in a_dir
		int Step(int a_from, int a_dir)
		{
			if (g_count <= 0) return -1;
			if (a_from < 0) return a_dir > 0 ? 0 : g_count - 1;
			return (a_from + a_dir + g_count) % g_count;
		}

		// the filled slot nearest where the right stick points (y up), or -1
		int Aim(double a_x, double a_y)
		{
			double th = std::atan2(a_y, a_x) * 180.0 / std::numbers::pi;
			if (th < 0) {
				th += 360.0;
			}
			if (!g_centred && (th < 90.0 || th > 270.0)) {
				th = a_y >= 0 ? kArcFrom : kArcTo;   // pointed right, off the wheel: the nearer end
			}
			int    i = -1;
			double best = 1e9;
			for (int k = 0; k < g_count; ++k) {   // the entry whose angle is nearest
				const double ang = g_centred ? SlotMathAngle(g_savedSlots[k] + 1) : g_material ? SlotMathAngle(g_entryId[k]) : EntryAngle(k, g_count);
				double d = std::fmod(std::abs(th - ang), 360.0);
				d = std::min(d, 360.0 - d);
				if (d < best) {
					best = d;
					i = k;
				}
			}
			return i;
		}
	}

	bool Rewrite(XINPUT_GAMEPAD& a_pad, WORD a_raw, WORD a_pressed, WORD& a_out, bool a_gameplay, bool a_radialOpen)
	{
		g_swallow &= a_raw;
		a_out &= ~g_swallow;
		const auto& s = settings::Get();
		const WORD button = static_cast<WORD>(s.ammoButton);

		if (!g_open) {
			if (!s.ammoWheel || !button || !a_gameplay || a_radialOpen || !(a_pressed & button)) {
				return false;
			}
			const auto worn = inventory::WornAmmo();
			// Keep true saved cell indices in full-ring mode. No arrow type is truncated.
			const auto slots = wheels::AmmoSlots();
			const auto layout = ammolayout::Build(slots, s.ammoCentred);
			const bool centred = layout.full;
			if (centred != g_centred) {
				if (auto* root = reflect::Get(g_root)) ui::Call(root, L"RemoveFromParent").Run();
				g_root = {};
				g_centred = centred;
				g_lastBuild = 0;
			}
			if (!Built() && !Build()) return false;
			g_ids = layout.ids;
			g_count = layout.count;
			g_savedSlots = layout.savedSlots;
			g_layoutCount = -1;   // saved positions can change without changing the entry count
			g_worn = worn;
			const int filled = g_count;
			EnsureMid();
			Layout(g_count);
			g_open = true;
			LockLook(true);
			g_pointed = -1;
			for (int i = 0; i < kSlots && g_pointed < 0; ++i) {
				g_pointed = g_ids[i] && g_ids[i] == worn ? i : -1;   // starts on the arrows worn now
			}
			g_pointedByStick = false;
			g_resting = true;
			g_drawnPointed = -2;
			Draw();
			ui::Visible(reflect::Get(g_root), true);
			g_swallow |= button;
			a_out &= ~button;
			logger::info("ammo: the wheel opened - {} arrow kind(s) on it{}", filled, filled ? "" : " (the favourite button on arrows in the inventory puts them here)");
			return true;
		}

		if (!a_gameplay || a_radialOpen) {
			Close("a menu opened");
			return false;
		}
		// open: the D-pad, A, B and the right stick are the wheel's
		a_out &= ~(kDpad | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B);
		const double x = a_pad.sThumbRX / 32767.0, y = a_pad.sThumbRY / 32767.0;
		const double m = std::hypot(x, y);
		a_pad.sThumbRX = 0;
		a_pad.sThumbRY = 0;
		const auto now = Clock::now();
		if (m >= kStickOn) {
			if (const int aim = Aim(x, y); aim >= 0) {
				g_pointed = aim;
				g_pointedByStick = true;
			}
			g_resting = false;
		} else if (m < kStickRest) {
			if (!g_resting) {
				g_resting = true;
				g_restSince = now;
			}
			// no centre rest snap on the ammo wheel (the owner, 2026-09-30: "We'll have to turn off the center rest snap for
			// the ammo wheel, otherwise it's inconvenient to use it") - the pointed arrows stay pointed; the main radial keeps it
		}
		if (a_pressed & XINPUT_GAMEPAD_DPAD_UP) {
			g_pointed = Step(g_pointed, -1);
			g_pointedByStick = false;
		}
		if (a_pressed & XINPUT_GAMEPAD_DPAD_DOWN) {
			g_pointed = Step(g_pointed, +1);
			g_pointedByStick = false;
		}
		if (a_pressed & XINPUT_GAMEPAD_B) {
			g_swallow |= XINPUT_GAMEPAD_B;
			Close("B - nothing equipped");
		} else if (a_pressed & (button | XINPUT_GAMEPAD_A)) {
			g_swallow |= a_pressed & (button | XINPUT_GAMEPAD_A);
			if (g_pointed >= 0 && g_ids[g_pointed]) {
				inventory::EquipAmmo(g_ids[g_pointed]);
				Close("the pointed arrows go on");
			} else {
				Close("nothing pointed");
			}
		} else {
			Draw();
		}
		return true;
	}

	bool Open() { return g_open; }


	void PointAt(int a_entry)
	{
		if (g_open && a_entry >= 0 && a_entry < g_count && g_ids[static_cast<std::size_t>(a_entry)] && g_pointed != a_entry) {
			g_pointed = a_entry;
			g_pointedByStick = false;
		}
	}

	int EntryUnderMouse(double a_x, double a_y)
	{
		auto* img = reflect::Get(g_wheelImg);
		double x = 0, y = 0, w = 0, h = 0;
		if (!g_open || !g_material || !img || !ui::Measure(img, x, y, w, h)) return -1;
		const double half = w * 0.5, cx = x + half, cy = y + h * 0.5;
		const double ring = settings::Get().display.cellRingPercent / 100.0;
		double       bestDist = half * settings::Get().display.cellHitPercent / 100.0;
		int          best = -1;
		for (int i = 0; i < g_count && i < kSlots; ++i) {
			const int slot = g_entryId[static_cast<std::size_t>(i)];
			if (slot < 1 || !g_ids[static_cast<std::size_t>(i)]) continue;
			const double ang = (slot - 1) * 45.0 * std::numbers::pi / 180.0;
			const double d = std::hypot(a_x - (cx + std::sin(ang) * ring * half), a_y - (cy - std::cos(ang) * ring * half));
			if (d < bestDist) bestDist = d, best = i;
		}
		return best;
	}
}
