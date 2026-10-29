#include "overlays/RingOverlay.h"

#include "game/QuickKeys.h"
#include "engine/Reflect.h"
#include "core/Settings.h"
#include "engine/Ui.h"

#include <numbers>

namespace ringoverlay
{
	namespace
	{
		struct Picture
		{
			reflect::Handle texture;
			bool            failed = false;
		};
		struct Overlay
		{
			const wchar_t*  file;        // the wheel's picture
			const wchar_t*  arrowFile;   // the selector arrow drawn over the game's own
			reflect::Handle root, image, slot, arrowImage, arrowSlot;
			Picture         wheel, arrow;
			bool            shown = false, arrowShown = false;
			double          x = -1e9, y = -1e9, w = -1, rot = -1e9, arrowW = -1, arrowX = -1e9;
			ULONGLONG       lastBuild = 0;
			ULONGLONG       nextMeasure = 0;
			reflect::Handle measuredImage;
			double          measuredX = 0, measuredY = 0, measuredW = 0, measuredH = 0;
		};
		// the Magic wheel in blue with a gold arrow, the Equipment wheel in red with a silver one (the owner's pictures)
		Overlay g_overlay[2] = { { L"MagicWheel.png", L"gold_arrow.png" }, { L"EquipmentWheel.png", L"silver_arrow.png" } };
		// the arrow on the game's wheel, measured on the HUD wheel (2026-10-22): its middle sits 0.49 of the half width out and the
		// 128-pixel picture is drawn 0.171 of the half width across; ours is a little larger so it covers the game's whole arrow
		constexpr double kArrowRadius = 0.502, kArrowSize = 0.171 * 1.06 * 1.35;   // 1.35 times larger and a little further out (the owner, 2026-10-22)
		int     g_builds = 0;

		// ours, from the plugin folder: a texture the engine imports from the PNG (as the favourites star's fill)
		UE::UObject* Texture(Picture& pic, const wchar_t* a_file)
		{
			if (auto* t = reflect::Get(pic.texture)) return t;
			if (pic.failed) return nullptr;
			const auto path = settings::PluginFolder() / L"ImprovedWheelsPlus" / a_file;
			static auto* lib = ui::Class(L"/Script/Engine.KismetRenderingLibrary");
			ui::Call c(lib ? lib->GetDefaultObject(false) : nullptr, L"ImportFileAsTexture2D");
			void* file = c.At("Filename");
			UE::UObject* tex = nullptr;
			if (c && file && std::filesystem::exists(path)) {
				c.Set("WorldContextObject", ui::PlayerController());
				auto* s = new (file) UE::FString(path.wstring().c_str());
				tex = c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
				s->~FString();
			}
			if (!tex) {
				pic.failed = true;
				logger::warn("ring overlay: the Magic wheel picture could not be loaded ({}{}) - the game's own picture stays",
					path.string(), std::filesystem::exists(path) ? "" : ", missing");
				return nullptr;
			}
			pic.texture = reflect::Hold(tex);
			logger::info("ring overlay: the wheel picture is loaded from {}", path.filename().string());
			return tex;
		}

		bool Build(Overlay& o)
		{
			const ULONGLONG now = GetTickCount64();
			if (o.lastBuild && now - o.lastBuild < 2000) return false;
			o.lastBuild = now;
			auto* tex = Texture(o.wheel, o.file);
			if (!tex) return false;
			auto* root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			const std::wstring n = std::to_wstring(++g_builds);
			auto* canvas = root ? ui::RootCanvas(root, (L"IwmRingCanvas" + n).c_str()) : nullptr;
			if (!canvas) return false;
			UE::UObject* slot = nullptr;
			UE::UObject* img = nullptr;
			if (tex) {   // the wheel picture (off in the INI: only the arrow is drawn)
				img = ui::AddToCanvas(canvas, L"/Script/UMG.Image", (L"IwmRing" + n).c_str(), &slot);
				if (!img || !slot) return false;
				ui::Anchors(slot, 0, 0, 0, 0);
				ui::Call b(img, L"SetBrushFromTexture");
				b.Set("Texture", tex);
				b.Set("bMatchSize", false);
				b.Run();
			}
			if (auto* arrowTex = Texture(o.arrow, o.arrowFile)) {   // the arrow, over the wheel picture (a missing arrow picture leaves the game's)
				UE::UObject* aslot = nullptr;
				auto*        aimg = ui::AddToCanvas(canvas, L"/Script/UMG.Image", (L"IwmRingArrow" + n).c_str(), &aslot);
				if (aimg && aslot) {
					ui::Anchors(aslot, 0, 0, 0, 0);
					ui::Vec2(aslot, L"SetAlignment", 0.5, 0.5);
					ui::Vec2(aimg, L"SetRenderTransformPivot", 0.5, 0.5);
					ui::Call b(aimg, L"SetBrushFromTexture");
					b.Set("Texture", arrowTex);
					b.Set("bMatchSize", false);
					b.Run();
					ui::Visible(aimg, false);
					o.arrowImage = reflect::Hold(aimg);
					o.arrowSlot = reflect::Hold(aslot);
				}
			}
			ui::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 60);   // the Inventory menu draws its own wheel above 59: level with the wheel name (60), under the slot counts (61)
			vp.Run();
			ui::Visible(root, false);
			o.root = reflect::Hold(root);
			if (img) {
				o.image = reflect::Hold(img);
				o.slot = reflect::Hold(slot);
			}
			o.shown = false;
			o.arrowShown = false;
			o.x = o.y = -1e9, o.w = -1, o.rot = -1e9;
			logger::info("ring overlay: built (build {})", g_builds);
			return true;
		}
	}

	void Show(UE::UObject* a_gameWheelImage, Kind a_kind, bool a_messageUp)
	{
		// a message of the game over the wheel ("Staffs cannot be poisoned") is on a layer under ours: while it is up the game's own wheel
		// shows and the message stays whole (the owner's screenshot, 2026-10-22)
		const auto& theme = settings::Get().theme;
		if (!(a_kind == Kind::kEquipment ? theme.inventory : theme.magic) || a_messageUp) {   // INI [Theme]: this wheel keeps the game's own look
			Hide();
			return;
		}
		auto& o = g_overlay[a_kind == Kind::kEquipment ? 1 : 0];
		auto& other = g_overlay[a_kind == Kind::kEquipment ? 0 : 1];
		if (other.shown) {
			ui::Visible(reflect::Get(other.root), false);
			other.shown = false;
		}
		if (!reflect::Get(o.root) && !Build(o)) return;
		auto* root = reflect::Get(o.root);
		auto* slot = reflect::Get(o.slot);
		double x = 0, y = 0, w = 0, h = 0;
		if (!root || !slot) {
			Hide();
			return;
		}
		const auto now = GetTickCount64();
		if (!o.shown || reflect::Get(o.measuredImage) != a_gameWheelImage || quickkeys::Animating() || now >= o.nextMeasure) {
			if (!ui::Measure(a_gameWheelImage, x, y, w, h)) { Hide(); return; }
			o.measuredImage = reflect::Hold(a_gameWheelImage);
			o.measuredX = x; o.measuredY = y; o.measuredW = w; o.measuredH = h;
			o.nextMeasure = now + 100;
		} else {
			x = o.measuredX; y = o.measuredY; w = o.measuredW; h = o.measuredH;
		}
		if (slot && (std::abs(x - o.x) > 0.5 || std::abs(y - o.y) > 0.5 || std::abs(w - o.w) > 0.5)) {
			ui::Vec2(slot, L"SetPosition", x, y);
			ui::Vec2(slot, L"SetSize", w, h);
			o.x = x, o.y = y, o.w = w;
		}
		if (!o.shown) {
			ui::Visible(root, true);
			o.shown = true;
		}
		// the arrow: where and how far round the game's arrow is, read from its material; shown only while the game shows its own
		auto* aimg = reflect::Get(o.arrowImage);
		auto* aslot = reflect::Get(o.arrowSlot);
		if (aimg && aslot) {
			static auto* midClass = ui::Class(L"/Script/Engine.MaterialInstanceDynamic");
			auto*        mid = ui::BrushResource(a_gameWheelImage);
			const bool   have = mid && midClass && mid->GetClass() == midClass;
			const bool   want = have && ui::MidScalar(mid, L"SelectorArrowAlpha") > 0.05f;
			if (want) {
				const double rot = ui::MidScalar(mid, L"SelectorRotator");   // a fraction of a turn clockwise from the top
				if (std::abs(rot - o.rot) > 0.0005 || std::abs(w - o.arrowW) > 0.5 || std::abs(x - o.arrowX) > 0.5 || !o.arrowShown) {
					const double half = w * 0.5, a = rot * 2.0 * std::numbers::pi, size = kArrowSize * half;
					ui::Vec2(aslot, L"SetPosition", x + half + std::sin(a) * kArrowRadius * half, y + h * 0.5 - std::cos(a) * kArrowRadius * half);
					ui::Vec2(aslot, L"SetSize", size, size);
					float deg = static_cast<float>(rot * 360.0);
					ui::CallFirst(aimg, L"SetRenderTransformAngle", &deg, sizeof(deg));
					o.rot = rot;
					o.arrowW = w;
					o.arrowX = x;
				}
				if (!o.arrowShown) {
					ui::Visible(aimg, true);
					o.arrowShown = true;
				}
			} else if (o.arrowShown) {
				ui::Visible(aimg, false);
				o.arrowShown = false;
			}
		}
	}

	void Hide()
	{
		for (auto& o : g_overlay) {
			if (!o.shown) continue;
			ui::Visible(reflect::Get(o.root), false);
			o.shown = false;
			o.arrowShown = false;
		}
	}
}
