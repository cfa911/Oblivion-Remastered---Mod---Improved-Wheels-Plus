#include "overlays/SlotCounts.h"
#include "engine/UiText.h"

#include "game/QuickKeys.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "core/Settings.h"
#include "engine/Ui.h"
#include "wheels/Wheels.h"

#include <numbers>

namespace slotcounts
{
	namespace
	{
		// where a slot circle's middle sits on the wheel picture, as a share of half its width (measured on the owner's
		// screenshot of 2026-09-30 03:38:46: the slot centres about 0.71 of the way out), and the label just below it
		// where a slot circle's middle sits on the wheel picture, as a share of half its width (measured on the owner's screenshot of
		// 2026-09-30: the slot centres about 0.71 of the way out), the counter just below it, and the text sizes - INI [Display]
		double      SlotRadius() { return settings::Get().display.cellRingPercent / 100.0; }
		double      Below() { return settings::Get().display.countBelowPercent / 100.0; }
		double      TextScale() { return settings::Get().display.countTextPercent / 100.0; }
		double      CentreScale() { return settings::Get().display.centreTextPercent / 100.0; }
		std::size_t CentreLine() { return static_cast<std::size_t>(settings::Get().display.centreLineChars); }

		reflect::Handle g_root;
		std::array<reflect::Handle, 8> g_label, g_slot;
		std::array<std::string, 8> g_text;
		constexpr int   kCentreLines = 4;
		std::array<reflect::Handle, kCentreLines> g_centre, g_centreSlot;   // the Magic wheel's centre name: one label a line, each centred
		std::array<std::string, kCentreLines>     g_centreShown;
		std::vector<std::string>                  g_centreWanted;
		double          g_cw = -1, g_ch = -1;
		int             g_centreN = -1;   // lines placed
		int       g_builds = 0;
		ULONGLONG g_lastBuild = 0, g_next = 0;
		std::uint32_t g_gen = 0;   // wheels::Generation() at the last text update
		bool      g_shown = false;
		wheels::Wheel g_wheel = wheels::Wheel::kEquipment;
		double    g_x = -1e9, g_y = -1e9, g_w = -1;

		void White(UE::UObject* a_label)
		{
			struct SlateColor
			{
				float        rgba[4];
				std::uint8_t rule;
				std::uint8_t pad[7];
			} white{ { 0.50f, 0.49f, 0.46f, 1.0f }, 0, {} };   // a soft grey (linear), not white: the texts inside the wheels were too bright
			ui::CallFirst(a_label, L"SetColor", &white, sizeof(white));
			ui::CallFirst(a_label, L"SetColorAndOpacity", &white, sizeof(white));
			const float shadow[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // none: the texts inside the wheels carry no shadow
			ui::CallFirst(a_label, L"SetShadowColorAndOpacity", shadow, sizeof(shadow));
			ui::Vec2(a_label, L"SetShadowOffset", 0.0, 0.0);
		}

		void SetText(UE::UObject* a_label, const std::string& a_text)
		{
			auto* fn = a_label->FindFunction(UE::FName(L"SetText", UE::EFindName::Find));
			const auto params = fn ? reflect::Fields(reinterpret_cast<UE::UStruct*>(fn)) : std::vector<std::pair<std::string, std::int32_t>>{};
			if (params.empty()) return;
			std::vector<std::uint8_t> buf(static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(fn)->propertiesSize, 0)) + 16, 0);
			const std::wstring w = pe::Wide(a_text);
			auto* text = new (buf.data() + params.front().second) UE::FText(UE::FText::AsCultureInvariant(UE::FString(w.c_str())));
			a_label->ProcessEvent(fn, buf.data());
			text->~FText();
			White(a_label);
			uix::ApplyFont(a_label, 0);   // the game's UI font, as everywhere on the wheels (the size it was given)
		}

		bool Build()
		{
			const ULONGLONG now = GetTickCount64();
			if (g_lastBuild && now - g_lastBuild < 2000) return false;
			g_lastBuild = now;
			auto* root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			const std::wstring n = std::to_wstring(++g_builds);
			auto* canvas = root ? ui::RootCanvas(root, (L"IwmCountsCanvas" + n).c_str()) : nullptr;
			if (!canvas) return false;
			for (int k = 0; k < 8; ++k) {
				UE::UObject* slot = nullptr;
				auto* label = ui::AddToCanvas(canvas, L"/Script/UMG.TextBlock",
					(L"IwmCount" + n + L"_" + std::to_wstring(k)).c_str(), &slot);
				if (!label || !slot) {
					logger::info("slot counts: the game's text prefab is not loaded yet");
					return false;
				}
				ui::Anchors(slot, 0, 0, 0, 0);
				ui::Vec2(slot, L"SetAlignment", 0.5, 0.5);
				const bool yes = true;
				ui::CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
				ui::Vec2(label, L"SetRenderTransformPivot", 0.5, 0.5);
				ui::Vec2(label, L"SetRenderScale", TextScale(), TextScale());
				g_label[static_cast<std::size_t>(k)] = reflect::Hold(label);
				g_slot[static_cast<std::size_t>(k)] = reflect::Hold(slot);
				g_text[static_cast<std::size_t>(k)] = "\x01";   // matches no real text: the first Show sets every label
			}
			for (int i = 0; i < kCentreLines; ++i) {   // the centre name: a label a line, each centred on the middle of the wheel
				UE::UObject* slot = nullptr;
				auto* label = ui::AddToCanvas(canvas, L"/Script/UMG.TextBlock",
					(L"IwmCentre" + n + L"_" + std::to_wstring(i)).c_str(), &slot);
				if (!label || !slot) break;
				ui::Anchors(slot, 0, 0, 0, 0);
				ui::Vec2(slot, L"SetAlignment", 0.5, 0.5);
				const bool fit = true;
				ui::CallFirst(slot, L"SetAutoSize", &fit, sizeof(fit));
				ui::Vec2(label, L"SetRenderTransformPivot", 0.5, 0.5);
				ui::Vec2(label, L"SetRenderScale", CentreScale(), CentreScale());
				g_centre[static_cast<std::size_t>(i)] = reflect::Hold(label);
				g_centreSlot[static_cast<std::size_t>(i)] = reflect::Hold(slot);
				g_centreShown[static_cast<std::size_t>(i)] = std::string(1, static_cast<char>(1));
			}
			ui::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 61);   // over the wheel and its name
			vp.Run();
			for (auto& h : g_label) {   // every text on the wheels: the game's UI font, white with a shadow; the sizes are the base the INI percentages scale
				White(reflect::Get(h));
				uix::ApplyFont(reflect::Get(h), 25.0);
			}
			for (auto& h : g_centre) {
				if (auto* c = reflect::Get(h)) {
					White(c);
					uix::ApplyFont(c, 25.0);
				}
			}
			ui::Visible(root, false);
			g_root = reflect::Hold(root);
			g_shown = false;
			g_x = g_y = -1e9, g_w = -1;
			logger::info("slot counts: built (build {})", g_builds);
			return true;
		}
	}

	void Show(UE::UObject* a_gameWheelImage, wheels::Wheel a_wheel)
	{
		// every 100 ms (the inventory walk is not free) - and at once whenever a wheel changed (Generation moves with
		// every LT / RT step, assign and removal), so the counter follows LT / RT on the read after the press
		const ULONGLONG now = GetTickCount64();
		const auto gen = wheels::Generation();
		const bool sameWheel = a_wheel == g_wheel;
		if (now < g_next && gen == g_gen && sameWheel && !quickkeys::Animating()) return;
		g_wheel = a_wheel;
		g_next = now + 100;
		g_gen = gen;
		if (!reflect::Get(g_root) && !Build()) return;
		auto* root = reflect::Get(g_root);
		double x = 0, y = 0, w = 0, h = 0;
		if (!root || !ui::Measure(a_gameWheelImage, x, y, w, h)) {
			Hide();
			return;
		}
		// where: only when the wheel moved or changed size
		if (std::abs(x - g_x) > 0.5 || std::abs(y - g_y) > 0.5 || std::abs(w - g_w) > 0.5) {
			const double half = w * 0.5, cx = x + half, cy = y + h * 0.5;
			for (int k = 0; k < 8; ++k) {
				const double a = k * 45.0 * std::numbers::pi / 180.0;   // slot k+1: 45 degrees apart, clockwise from the top
				const double px = cx + std::sin(a) * SlotRadius() * half;
				const double py = cy - std::cos(a) * SlotRadius() * half + Below() * half;
				if (auto* s = reflect::Get(g_slot[static_cast<std::size_t>(k)])) ui::Vec2(s, L"SetPosition", px, py);
			}
			g_x = x, g_y = y, g_w = w;
		}
		{   // the centre name's lines: centred on the middle of the wheel, one under another
			const int n = static_cast<int>(g_centreWanted.size());
			if (std::abs(w - g_cw) > 0.5 || std::abs(h - g_ch) > 0.5 || n != g_centreN) {
				const double pitch = w * 0.047;   // a line's height at the scale used
				for (int i = 0; i < kCentreLines; ++i) {
					if (auto* cs = reflect::Get(g_centreSlot[static_cast<std::size_t>(i)])) {
						ui::Vec2(cs, L"SetPosition", x + w * 0.5, y + h * 0.5 + (i - (std::max(n, 1) - 1) * 0.5) * pitch);
					}
				}
				g_cw = w, g_ch = h, g_centreN = n;
			}
		}
		// what: the shown entry's place out of the entries the slot holds, "2/3" (a reader's idea, 2026-10-20: "if I see x/3 I know
		// there's space for two more") - the owner, 2026-10-01: "whether I'm on one out of five, two out of five ..." - and "0/0"
		// for an empty slot ("slot number 3 and 1, which are actually empty, have no indicator at all of like zero out of five");
		// "-/3" when the slot's key holds none of its entries. INI [Wheel] bShowSlotCapacity=1 reads the capacity instead ("2/5",
		// "0/5", "-/5"). Every label gets text: an empty string left two labels showing the widget's placeholder "Text Block".
		const auto counts = wheels::SlotCounts(a_wheel);
		const int  cap = a_wheel == wheels::Wheel::kAmmo ? 1 : wheels::SlotCap();
		for (int k = 0; k < 8; ++k) {
			const auto& c = counts[static_cast<std::size_t>(k)];
			const int          total = settings::Get().showSlotCapacity ? cap : c.count;
			// Ammo has exactly one arrow type per cell: explicitly clear labels inherited from another wheel.
			const std::string text = a_wheel == wheels::Wheel::kAmmo ? " " : c.count == 0 ? std::format("0/{}", total) : c.position > 0 ? std::format("{}/{}", c.position, total) : std::format("-/{}", total);
			if (text != g_text[static_cast<std::size_t>(k)]) {
				if (auto* label = reflect::Get(g_label[static_cast<std::size_t>(k)])) SetText(label, text);
				g_text[static_cast<std::size_t>(k)] = text;
			}
		}
		for (int i = 0; i < kCentreLines; ++i) {
			auto* centre = reflect::Get(g_centre[static_cast<std::size_t>(i)]);
			if (!centre) continue;
			const std::string want =i < static_cast<int>(g_centreWanted.size()) ? g_centreWanted[static_cast<std::size_t>(i)] : std::string(" ");
			if (want != g_centreShown[static_cast<std::size_t>(i)]) {
				SetText(centre, want.empty() ? std::string(" ") : want);
				g_centreShown[static_cast<std::size_t>(i)] = want;
			}
		}
		if (!g_shown) {
			ui::Visible(root, true);
			g_shown = true;
		}
	}

	void SetCentre(const std::string& a_text)
	{
		// split by hand into lines the hole holds, each its own centred label (the text block's own wrap and centring did not take)
		std::vector<std::string> lines;
		std::string              line;
		std::size_t              i = 0;
		while (i < a_text.size()) {
			const auto        sp = a_text.find(' ', i);
			const std::string word = a_text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
			if (!line.empty() && line.size() + 1 + word.size() > CentreLine() && static_cast<int>(lines.size()) < kCentreLines - 1) {
				lines.push_back(line);
				line.clear();
			}
			line += (line.empty() ? "" : " ") + word;
			if (sp == std::string::npos) break;
			i = sp + 1;
		}
		if (!line.empty()) lines.push_back(line);
		g_centreWanted = std::move(lines);
	}

	void Hide()
	{
		if (!g_shown) return;
		ui::Visible(reflect::Get(g_root), false);
		g_shown = false;
	}
}
