// Improved Wheel Menu (Oblivion Remastered) - entry point.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "favourites/Favourites.h"
#include "input/Controls.h"
#include "game/Inventory.h"
#include "game/Menus.h"
#include "input/Pad.h"
#include "game/QuickKeys.h"
#include "game/Rows.h"
#include "core/Settings.h"
#include "slowtime/SlowTime.h"
#include "engine/TesThread.h"
#include "wheels/Wheels.h"

namespace
{
	const char* SlotWord(int a_slot)
	{
		// KeyIndex is the slot number as drawn, 1 at the top, clockwise (the game's key = number - 1; read 2026-09-29 -
		// the 2026-09-26 note had it starting at the top-left)
		static constexpr const char* kWords[9] = { "none", "top", "top-right", "right", "bottom-right", "bottom", "bottom-left", "left", "top-left" };
		return a_slot >= 0 && a_slot <= 8 ? kWords[a_slot] : "none";
	}

	void OnQuickKeys(quickkeys::Event a_event, int a_slot)
	{
		switch (a_event) {
		case quickkeys::Event::kOpened:
			logger::info("radial opened (the {} wheel)", wheels::Name(wheels::Active()));
			wheels::RadialShown();
			break;
		case quickkeys::Event::kPointed:
			logger::debug("points at slot {} ({})", a_slot, SlotWord(a_slot));
			break;
		case quickkeys::Event::kClosed:
			// only the view model's KeyIndex as it reads at the close - not proof that the game used anything (2026-10-01
			// 01:52:39 read "slot 2" and nothing was equipped); an RB / A use is read back from the item ("USE result")
			logger::info("radial closed; the view model's KeyIndex reads {} ({})", a_slot >= 1 && a_slot <= 8 ? std::format("slot {}", a_slot) : std::string("no slot"),
				SlotWord(a_slot));
			wheels::RadialHidden();
			break;
		case quickkeys::Event::kPanelOpened:
			logger::info("assign panel opened in the {}", menus::Name(menus::Active()));
			wheels::PanelShown(menus::Active());
			break;
		case quickkeys::Event::kPanelClosed:
			logger::info("assign panel closed");
			wheels::PanelHidden();
			break;
		}
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		quickkeys::Install(&OnQuickKeys);
		// The widget and menu classes appear as the game builds them (the quick keys widget at the main menu, each
		// menu the first time it opens). They are looked for on the GAME thread, from the controller read, about
		// every 200 ms - never from a thread of our own (a start-up crashed in UObjectArray, 2026-09-29).
		pad::SetFrameCallback([] {
			slowtime::Tick();
			favourites::Tick();   // the inventory's favourites column (every 100 ms while the inventory is open)
			static auto next = std::chrono::steady_clock::now();
			static bool tesThreadTried = false;
			const auto now = std::chrono::steady_clock::now();
			if (now < next) {
				return;
			}
			next = now + 200ms;
			quickkeys::Tick();
			menus::Tick();
			rows::Tick();
			controls::Tick();   // the ammo wheel's Controls-page row and its button
			if (!tesThreadTried) {
				tesThreadTried = true;   // once: the ammo wheel's equips run on the TES thread (TesThread.h)
				if (!testhread::Install()) {
					logger::error("the TES thread queue is not installed - the ammo wheel opens but cannot equip");
				} else {
					testhread::SetPerPass(&inventory::SweepNativeSpellKeys);   // native spell hotkeys stay empty (IWP-013)
				}
			}
		});
		pad::Install();
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	settings::Load();
	{
		const auto level = static_cast<spdlog::level::level_enum>(std::clamp(settings::Get().logLevel, 0, 4));
		logger::set_level(level, level);
	}
	logger::info("Improved Wheel Menu {} loaded (Oblivion Remastered)", IWM_VERSION);

	if (auto* messaging = OBSE::GetMessagingInterface()) {
		if (!messaging->RegisterListener(&OnMessage)) {
			logger::error("OBSE messaging: listener refused - the quick keys hook will not be installed");
		}
	} else {
		logger::error("OBSE messaging interface missing - the quick keys hook will not be installed");
	}
	return true;
}
