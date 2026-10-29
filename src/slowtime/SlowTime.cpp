#include "slowtime/SlowTime.h"
#include "slowtime/SlowTimeLease.h"
#include "ammo/Ammo.h"
#include "game/QuickKeys.h"
#include "engine/Reflect.h"
#include "core/Settings.h"
#include "engine/Ui.h"

namespace slowtime
{
	namespace { CloseGate g_close; ULONGLONG g_next = 0; }
	void CloseRequested()
	{
		if (settings::Get().slowTime) logger::info("slow time: close requested; restoring before animation");
		g_close.Close(quickkeys::OpenCount());
		g_next = 0;
		Tick();
	}
	void Reopened() { g_close.Reopen(); g_next = 0; }

	void Tick()
	{
		if (!settings::Get().slowTime || !reflect::Ok()) return;
		static Lease lease;
		static reflect::Handle previousWorld;
		const auto now = GetTickCount64();
		if (now < g_next) return;
		g_next = now + 50; // real time, including while simulation is slowed
		auto* world = static_cast<UE::UWorld*>(UE::UWorld::GetSingleton());
		if (!world) { lease.Forget(); previousWorld = {}; return; }
		if (reflect::Get(previousWorld) != world) {
			lease.Forget(); // never write through a previous world's pointer
			previousWorld = reflect::Hold(world);
		}
		auto* data = RE::TESDataHandler::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* im = RE::InterfaceManager::GetInstance(false, false);
		const bool ready = data && !data->saveLoadGame && !data->clearingData && !data->loadingFiles &&
			player && !player->deadFlag && im && im->menuMode == 1;
		const bool open = quickkeys::RadialOpen() || ammo::Open();
		const bool wanted = ready && !quickkeys::PanelOpen() && g_close.Allows(open, quickkeys::OpenCount());
		if (!wanted && !lease.OwnsTime()) {
			lease.Update(false, 1.0f, 1.0f, [](float) { return true; });   // reset an external-writer block for the next opening
			return;
		}
		static reflect::Handle gameplayClass;
		auto* cls = reflect::Get(gameplayClass);
		if (!cls) { cls = ui::Class(L"/Script/Engine.GameplayStatics"); gameplayClass = reflect::Hold(cls); }
		auto* type = static_cast<UE::UClass*>(cls);
		auto* cdo = type ? type->GetDefaultObject(false) : nullptr;
		ui::Call get(cdo, L"GetGlobalTimeDilation");
		ui::Call set(cdo, L"SetGlobalTimeDilation");
		const bool valid = get && set && get.Size("WorldContextObject") == sizeof(void*) &&
			get.Size("ReturnValue") == sizeof(float) && set.Size("WorldContextObject") == sizeof(void*) &&
			set.Size("TimeDilation") == sizeof(float);
		if (!valid || !get.Set("WorldContextObject", world) || !get.RunGuarded()) {
			static bool warned = false;
			if (!warned) { logger::warn("slow time: reflected time access unavailable; feature inactive"); warned = true; }
			return;
		}
		lease.Update(wanted, get.Get<float>("ReturnValue"), settings::Get().slowTimePercent / 100.0f,
			[&](float value) {
				if (!set.Set("WorldContextObject", world) || !set.Set("TimeDilation", value) || !set.RunGuarded()) return false;
				// Read back: the engine may clamp the requested value.
				if (!get.RunGuarded() || !Lease::Near(get.Get<float>("ReturnValue"), value)) {
					logger::warn("slow time: engine did not retain requested speed {}", value);
					return false;
				}
				logger::info("slow time: world speed {}", value);
				return true;
			});
	}
}
