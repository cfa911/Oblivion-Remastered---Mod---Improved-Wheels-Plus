// The HUD's spell picture after a Magic wheel choice.
#include "wheels/WheelsInternal.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "game/Rows.h"

namespace wheels::detail
{
	// the HUD's spell picture (VHUDMainViewModel.SpellIcon), read to learn whether it follows selectedSpell
	UE::UObject* HudSpellIcon()
	{
		static auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VHUDMainViewModel");
		if (!cls || !reflect::Ok()) return nullptr;
		const auto off = reflect::Offset(cls, "SpellIcon");
		const auto vms = reflect::Instances(cls);
		return off >= 0 && !vms.empty() ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(vms.front()) + off) : nullptr;
	}

	// The HUD's spell picture after a Magic wheel choice. Setting selectedSpell does not reach the HUD: the magic menu's
	// own pick makes the game write VHUDMainViewModel.SpellIcon natively and broadcast the field's change, and the HUD's
	// binding then calls GetSpellIcon once (probe B, 2026-10-01: OnItemClicked, then GetSpellIcon x1 and nothing else
	// spell-related on the HUD; every Magic wheel pick logged "did NOT change"). The same, from here: the spell's icon
	// written into SpellIcon, then MVVMViewModelBase's K2_BroadcastFieldValueChanged for "SpellIcon" (the HUD's view
	// models are VViewModelBase -> MVVMViewModelBase).
	// The HUD's quick-magic picture itself. The view model write and its broadcast did not reach it (the owner, 2026-10-01:
	// "The widget did not update"; a trace of VHUDMainViewModel showed our five K2_BroadcastFieldValueChanged calls and no
	// GetSpellIcon after any of them - the HUD's binding listens under another field id). The widget has its own
	// SetMagicTexture(InTexture) (WBP_ModernHud_MagicIcon_C, from VLegacyHudMagicIcon); called on the live one from
	// TestBench with the Shield picture, the owner: "I did just notice the shield icon". Every live instance (never a
	// template) gets it.
	int SetHudWidgetTexture(UE::UObject* a_icon)
	{
		static auto* widgetCls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Game/UI/Modern/HUD/Main/WBP_ModernHud_MagicIcon.WBP_ModernHud_MagicIcon_C");
		if (!widgetCls || !a_icon) return 0;
		int set = 0;
		for (auto* w : reflect::Instances(widgetCls)) {
			ui::Call call(w, L"SetMagicTexture");
			if (!call || !call.Set("InTexture", a_icon)) continue;
			if (call.Run()) ++set;
		}
		return set;
	}

	void PushHudSpellIcon(std::uint32_t a_formID)
	{
		static auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VHUDMainViewModel");
		static auto* texCls = ui::Class(L"/Script/Engine.Texture2D");
		if (!cls || !texCls || !reflect::Ok()) {
			logger::warn("wheels: the HUD's spell picture cannot be set (view model class {}, Texture2D {})", cls ? "found" : "missing", texCls ? "found" : "missing");
			return;
		}
		const auto off = reflect::Offset(cls, "SpellIcon");
		const auto vms = reflect::Instances(cls);
		auto*      icon = rows::SpellIcon(a_formID);
		if (off < 0 || vms.empty() || !icon || !icon->GetClass()->IsChildOf(texCls)) {
			logger::info("wheels: the HUD's spell picture left as it is (SpellIcon at {}, {} view model(s), icon {})", off, vms.size(),
				!icon ? std::string("none") : pe::Utf8(icon->GetClass()->GetFName().ToString()));
			return;
		}
		auto* vm = vms.front();
		*reinterpret_cast<UE::UObject**>(reinterpret_cast<std::uint8_t*>(vm) + off) = icon;
		const int widgets = SetHudWidgetTexture(icon);
		logger::info("wheels: the HUD's quick-magic picture set to {} on {} widget(s) (SetMagicTexture)", pe::Utf8(icon->GetFName().ToString()), widgets);
		ui::Call broadcast(vm, L"K2_BroadcastFieldValueChanged");
		void* id = broadcast ? broadcast.At("FieldId") : nullptr;
		if (!id) {
			logger::warn("wheels: SpellIcon written, but the view model has no K2_BroadcastFieldValueChanged(FieldId) - the HUD keeps its picture until the game asks again");
			return;
		}
		new (id) UE::FName(L"SpellIcon", UE::EFindName::Add);   // FFieldNotificationId { FName FieldName }
		broadcast.Run();
		logger::info("wheels: the HUD's spell picture set to {} and its change broadcast", pe::Utf8(icon->GetFName().ToString()));
	}

	void CheckHud()
	{
		if (g_hudCheckAt == std::chrono::steady_clock::time_point{} || std::chrono::steady_clock::now() < g_hudCheckAt) return;
		g_hudCheckAt = {};
		auto* now = HudSpellIcon();
		logger::info("wheels: the HUD's spell picture {} after the Magic wheel's choice ({})", now != g_hudIconBefore ? "CHANGED" : "did NOT change",
			now ? pe::Utf8(now->GetFName().ToString()) : std::string("none"));
	}
}
