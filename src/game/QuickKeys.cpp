#include "game/QuickKeys.h"
#include "game/MessageDiscovery.h"

#include "game/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "core/Settings.h"
#include "engine/Ui.h"


namespace quickkeys
{
	namespace
	{
		// The Blueprint class of the radial and its view model's native class, as the running game names them.
		constexpr const wchar_t* kWidgetClassPath = L"/Game/UI/Modern/GameMenuLayer/WBP_ModernMenu_QuickKeys.WBP_ModernMenu_QuickKeys_C";
		constexpr const wchar_t* kViewModelClassPath = L"/Script/Altar.VQuickKeysMenuViewModel";
		constexpr std::ptrdiff_t kKeyIndexOffset = 0xD0;   // VQuickKeysMenuViewModel::KeyIndex (IntProperty), read by reflection 2026-09-26


		std::atomic<UE::UClass*>  g_widgetClass{ nullptr };
		std::atomic<UE::UObject*> g_viewModel{ nullptr };
		std::atomic<std::int32_t> g_viewModelIndex{ -1 };   // its object-array slot: checked there, never through the pointer
		std::atomic<UE::UClass*>  g_viewModelClass{ nullptr };
		Listener                  g_listener = nullptr;

		std::mutex g_statusLock;
		Status     g_status;

		// The two UFunction names we act on, compared once per distinct UFunction* and remembered.
		UE::UFunction* g_fnVisibility = nullptr;
		UE::UFunction* g_fnKeyIndex = nullptr;
		UE::UFunction* g_fnUpdateIcons = nullptr;    // the game (re)draws all eight pictures
		UE::UFunction* g_fnSetKeyPicture = nullptr;  // SetQuickKeyByIndex: one picture
		std::atomic<bool> g_redrew{ false };       // the game redrew its pictures: the inventory wheel is put right on the next read, not up to 100 ms later
		std::atomic<int> g_frame{ 0 };
		std::atomic<ULONGLONG> g_boostUntil{ 0 };   // the wheel changed visibility: its animations are played faster until then

		std::string Utf8(const UE::FString& a_s)
		{
			const wchar_t* d = UE::GetData(a_s);
			const int      n = UE::GetNum(a_s);
			if (!d || n <= 0) {
				return {};
			}
			const int len = d[n - 1] == L'\0' ? n - 1 : n;
			const int bytes = WideCharToMultiByte(CP_UTF8, 0, d, len, nullptr, 0, nullptr, nullptr);
			std::string out(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
			if (bytes > 0) {
				WideCharToMultiByte(CP_UTF8, 0, d, len, out.data(), bytes, nullptr, nullptr);
			}
			return out;
		}

		std::string NameOf(const UE::FName& a_n) { return Utf8(a_n.ToString()); }

		void SetProblem(const std::string& a_why)
		{
			std::scoped_lock l(g_statusLock);
			g_status.problem = a_why;
		}

		// A kept pointer is the live view model only if the slot it was found in still holds it - asked of the SLOT, never
		// by reading the pointer (a freed object's own index is garbage: the 09:15 crash in rows) - and then of that class.
		bool IsLive(UE::UObject* a_o, UE::UClass* a_class)
		{
			auto* live = reflect::Get(a_o, g_viewModelIndex.load(std::memory_order_acquire));
			return live && live->GetClass() == a_class;
		}

		void KeepViewModel(UE::UObject* a_live)
		{
			g_viewModelIndex.store(a_live ? a_live->internalIndex : -1, std::memory_order_release);
			g_viewModel.store(a_live, std::memory_order_release);
		}

		// The one transient view model instance (not the class default object) - found by walking the object array.
		UE::UObject* FindViewModel(UE::UClass* a_class)
		{
			auto* arr = UE::FUObjectArray::GetSingleton();
			if (!arr || !a_class) {
				return nullptr;
			}
			UE::UObject* found = nullptr;
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n && !found; ++i) {
				auto* item = arr->IndexToObject(i);
				if (!item || !item->object) {
					continue;
				}
				auto* o = reinterpret_cast<UE::UObject*>(item->object);
				if (o->GetClass() == a_class && o != a_class->GetDefaultObject(false)) {
					found = o;
				}
			}
			arr->UnlockInternalArray();
			return found;
		}

		std::int32_t* KeyIndexField()
		{
			auto* vm = g_viewModel.load(std::memory_order_acquire);
			auto* cls = g_viewModelClass.load(std::memory_order_acquire);
			if (!vm || !IsLive(vm, cls)) {
				vm = FindViewModel(cls);   // recreated after a load? find it again
				KeepViewModel(vm);
				if (!vm) {
					return nullptr;
				}
			}
			return reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uint8_t*>(vm) + kKeyIndexOffset);
		}

		int ReadKeyIndex()
		{
			const auto* f = KeyIndexField();
			return f ? *f : -1;
		}

		// The instance that opened as a menu's panel (nullptr when none) - its close is the panel's, not the radial's.
		std::atomic<UE::UObject*> g_panelObject{ nullptr };

		void Emit(Event a_event, int a_slot)
		{
			{
				std::scoped_lock l(g_statusLock);
				switch (a_event) {
				case Event::kOpened:
					g_status.open = true;
					g_status.pointedSlot = -1;
					++g_status.opens;
					break;
				case Event::kPanelOpened:
					g_status.panelOpen = true;
					g_status.pointedSlot = -1;
					break;
				case Event::kPointed:
					g_status.pointedSlot = a_slot;
					break;
				case Event::kClosed:
					// the widget collapses itself once as the menu layer is built (no open before it): not the
					// player closing the radial, and it chose nothing
					if (!g_status.open) {
						logger::debug("quick keys: collapsed while not open (widget set-up) - ignored");
						return;
					}
					g_status.open = false;
					g_status.lastChosenSlot = a_slot;
					break;
				case Event::kPanelClosed:
					if (!g_status.panelOpen) {
						return;
					}
					g_status.panelOpen = false;
					break;
				}
			}
			if (g_listener) {
				g_listener(a_event, a_slot);
			}
		}

		void OnVisibility(UE::UObject* a_obj, std::uint8_t a_vis)
		{
			g_boostUntil.store(GetTickCount64() + 900);
			if (a_vis == 4) {   // SelfHitTestInvisible: shown
				if (menus::Active() != menus::Menu::kNone) {
					g_panelObject.store(a_obj);
					Emit(Event::kPanelOpened, -1);
				} else {
					Emit(Event::kOpened, -1);
				}
			} else if (a_vis == 1) {   // Collapsed: hidden
				if (g_panelObject.load() == a_obj) {
					g_panelObject.store(nullptr);
					Emit(Event::kPanelClosed, -1);
				} else {
					Emit(Event::kClosed, ReadKeyIndex());
				}
			}
		}

		// pe::Watch handler: runs on the game thread before the widget's function
		void OnWidgetEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			if (a_fn == g_fnVisibility) {
				if (a_params) { OnVisibility(a_obj, *static_cast<const std::uint8_t*>(a_params)); }
			} else if (a_fn == g_fnKeyIndex) {
				if (a_params) { Emit(Event::kPointed, *static_cast<const std::int32_t*>(a_params)); }
			} else if (a_fn == g_fnUpdateIcons || a_fn == g_fnSetKeyPicture) {
				g_redrew.store(true);
			} else if (!g_fnVisibility || !g_fnKeyIndex || !g_fnUpdateIcons || !g_fnSetKeyPicture) {
				// still learning the UFunction pointers: one name compare per unknown function
				const std::string n = pe::FunctionName(a_fn);
				if (!g_fnUpdateIcons && n == "UpdateIcons") {
					g_fnUpdateIcons = a_fn;
					g_redrew.store(true);
				} else if (!g_fnSetKeyPicture && n == "SetQuickKeyByIndex") {
					g_fnSetKeyPicture = a_fn;
					g_redrew.store(true);
				}
				if (!g_fnVisibility && n == "OnVisibilityChangedEvent") {
					g_fnVisibility = a_fn;
					if (a_params) { OnVisibility(a_obj, *static_cast<const std::uint8_t*>(a_params)); }
				} else if (!g_fnKeyIndex && n == "Update Key Index") {
					g_fnKeyIndex = a_fn;
					if (a_params) { Emit(Event::kPointed, *static_cast<const std::int32_t*>(a_params)); }
				}
			}
		}
	}

	void Install(Listener a_listener)
	{
		g_listener = a_listener;
	}

	// From the plugin's lazy thread until the watch is in (the class exists at the main menu).
	void Tick()
	{
		if (GetStatus().hookInstalled) {
			return;
		}
		g_frame.fetch_add(1, std::memory_order_relaxed);
		auto* widgetClass = g_widgetClass.load(std::memory_order_acquire);
		if (!widgetClass) {
			widgetClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kWidgetClassPath);
			if (!widgetClass) {
				return;
			}
			g_widgetClass.store(widgetClass, std::memory_order_release);
			logger::info("quick keys: widget class found ({})", Utf8(widgetClass->GetFullName()));
		}
		auto* vmClass = g_viewModelClass.load(std::memory_order_acquire);
		if (!vmClass) {
			vmClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kViewModelClassPath);
			if (vmClass) {
				g_viewModelClass.store(vmClass, std::memory_order_release);
				KeepViewModel(FindViewModel(vmClass));
			}
		}
		const bool hooked = pe::Watch(widgetClass, &OnWidgetEvent);
		std::scoped_lock l(g_statusLock);
		g_status.widgetClassFound = true;
		g_status.viewModelFound = g_viewModel.load(std::memory_order_relaxed) != nullptr;
		g_status.hookInstalled = hooked;
		if (hooked) {
			g_status.problem.clear();
		} else {
			g_status.problem = "the widget's vtable could not be swapped";
		}
	}

	namespace
	{
		// The object a field holds, if it is a WidgetAnimation: the field's 8 bytes are read as a pointer and its class compared with
		// the engine's WidgetAnimation class - inside a guard, because most fields of the widget are not pointers at all (a first
		// version asked the object table about each of them and crashed on an integer: 0x20000000b, 2026-10-16).
		UE::UObject* AnimationAt(const std::uint8_t* a_widget, std::int32_t a_offset, UE::UClass* a_animClass)
		{
			__try {
				auto* o = *reinterpret_cast<UE::UObject* const*>(a_widget + a_offset);
				const auto raw = reinterpret_cast<std::uintptr_t>(o);
				if (raw < 0x10000 || raw > 0x00007FFFFFFFFFFF || (raw & 7) != 0) return nullptr;
				return o->GetClass() == a_animClass ? o : nullptr;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return nullptr;
			}
		}
	}

	bool Animating()
	{
		return GetTickCount64() <= g_boostUntil.load();
	}

	void BoostAnimations()
	{
		const int pct = settings::Get().animSpeedPercent;
		if (pct <= 100 || GetTickCount64() > g_boostUntil.load()) return;
		auto* w = VisibleWheel();
		if (!w || !reflect::Ok()) return;
		static std::vector<std::int32_t> s_offsets;   // the wheel class's object fields that are widget animations, found once
		static UE::UClass*               s_class = nullptr;
		if (s_class != w->GetClass()) {
			s_class = w->GetClass();
			s_offsets.clear();
			static auto* animClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/UMG.WidgetAnimation");
			if (animClass) {
				for (const auto& [name, off] : reflect::Fields(reinterpret_cast<UE::UStruct*>(s_class))) {
					if (AnimationAt(reinterpret_cast<const std::uint8_t*>(w), off, animClass)) {
						s_offsets.push_back(off);
						logger::info("quick keys: widget animation {} (played at {}%)", name, pct);
					}
				}
			}
			if (s_offsets.empty()) logger::info("quick keys: the wheel widget has no animation fields to speed up");
		}
		const float speed = static_cast<float>(pct) / 100.0f;
		for (const auto off : s_offsets) {
			static auto* animClass2 = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/UMG.WidgetAnimation");
			auto* anim = animClass2 ? AnimationAt(reinterpret_cast<const std::uint8_t*>(w), off, animClass2) : nullptr;
			if (!anim) continue;
			ui::Call c(w, L"SetPlaybackSpeed");
			if (!c) return;
			c.Set("InAnimation", anim);
			c.Set("PlaybackSpeed", speed);
			c.RunGuarded();
		}
	}

	void CancelChoice()
	{
		if (auto* f = KeyIndexField()) {
			// KeyIndex is already the slot number as drawn (1 = top, the game's key = number - 1; 0 / -1 = none). The old
			// message added one more ("pointed at slot 3" while the pad said slot 2, 2026-10-01 01:50:55).
			static ULONGLONG s_said = 0;
			const ULONGLONG  nowTick = GetTickCount64();
			if (nowTick - s_said >= 1000) {   // at most once a second: the radial keeps the choice clear every read while it closes
				s_said = nowTick;
				logger::info("quick keys: choice cancelled (the view model pointed at {})",
				*f >= 1 && *f <= 8 ? std::format("slot {}", *f) : std::string("no slot"));
			}
			*f = -1;
		}
		std::scoped_lock l(g_statusLock);
		g_status.pointedSlot = -1;
	}

	namespace
	{
		std::int32_t* BpIntField(UE::UObject* a_widget, const char* a_name)
		{
			const auto off = a_widget && reflect::Ok() ? reflect::Offset(a_widget->GetClass(), a_name) : -1;
			return off >= 0 ? reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uint8_t*>(a_widget) + off) : nullptr;
		}

		std::int32_t BpInt(UE::UObject* a_widget, const char* a_name)
		{
			const auto* f = BpIntField(a_widget, a_name);
			return f ? *f : -999;
		}

		// The selector the wheel draws is two scalars on its picture's material instance (quickKeys_material > Image, a
		// MaterialInstanceDynamic of MIC_UI_QuickKeys), read live 2026-10-01 on the HUD wheel: SelectorRotator (0.375 =
		// slot 4 of 8, the bow last pointed) turns it and SelectorArrowAlpha (1.0) shows it. The slots' own IDn_Opacity /
		// IDnTexture are the pictures.
		constexpr const wchar_t* kArrowAlpha = L"SelectorArrowAlpha";
		reflect::Handle g_snapMid;              // the material whose arrow the rest snap hid
		float           g_snapAlpha = 1.0f;     // its arrow's alpha before the snap, put back when the stick points again
		bool            g_snapHidden = false;

		UE::UObject* WheelMid(UE::UObject* a_wheel)
		{
			auto* material = ui::ChildNamed(a_wheel, L"quickKeys_material");
			if (!material) material = ui::FindInTree(a_wheel, "quickKeys_material");
			auto* img = material ? ui::ChildNamed(material, L"Image") : nullptr;
			if (!img && material) img = ui::FindInTree(material, "Image");
			static auto* midClass = ui::Class(L"/Script/Engine.MaterialInstanceDynamic");
			auto* mid = img ? ui::BrushResource(img) : nullptr;
			return mid && midClass && mid->GetClass() == midClass ? mid : nullptr;
		}
	}

	void ClearPointer()
	{
		CancelChoice();
		// The view model's KeyIndex is only what the game reads, and the game puts it back within a frame (2026-10-01
		// 02:35:41.541 snap, 02:35:41.555 A read slot 2 again). "Update Key Index"(-1) on the widget changed nothing either
		// ("QuickKeyID 1 -> 1 ... CurrentScaledKeyID 2 -> 2", every snap of 02:34-02:35). So, what the snap now does:
		//   * the arrow goes: SelectorArrowAlpha = 0 on the wheel's material, held at 0 while the snap lasts (HoldCleared)
		//     and put back as the stick points again (RestorePointer);
		//   * the Blueprint's own pointed key is written to -1 (QuickKeyID, CurrentScaledKeyID, HoveredKeyID - -1 is the
		//     value the Blueprint itself leaves them at between opens, read on the closed HUD wheel), so a stick that goes
		//     back to the SAME slot is a change for it and the slot lights and grows again;
		//   * our own pointed slot is held at none by the pad until the stick points again (Pad.cpp, PointedKey).
		auto* w = VisibleWheel();
		if (!w) {
			logger::info("quick keys: rest snap - no wheel on screen, nothing more to clear");
			return;
		}
		const auto q0 = BpInt(w, "QuickKeyID"), h0 = BpInt(w, "HoveredKeyID"), c0 = BpInt(w, "CurrentScaledKeyID");
		for (const char* name : { "QuickKeyID", "CurrentScaledKeyID", "HoveredKeyID" }) {
			if (auto* f = BpIntField(w, name)) {
				*f = -1;
			}
		}
		auto* mid = WheelMid(w);
		float before = -1.0f;
		if (mid) {
			before = ui::MidScalar(mid, kArrowAlpha);
			if (!g_snapHidden || reflect::Get(g_snapMid) != mid) {
				g_snapAlpha = before > 0.0f ? before : 1.0f;
			}
			ui::SetMidScalar(mid, kArrowAlpha, 0.0f);
			g_snapMid = reflect::Hold(mid);
			g_snapHidden = true;
		}
		logger::info("quick keys: rest snap cleared the wheel - QuickKeyID {} -> {}, HoveredKeyID {} -> {}, CurrentScaledKeyID {} -> {}; selector arrow {}",
			q0, BpInt(w, "QuickKeyID"), h0, BpInt(w, "HoveredKeyID"), c0, BpInt(w, "CurrentScaledKeyID"),
			mid ? std::format("alpha {:.2f} -> {:.2f} (SelectorRotator {:.3f})", before, ui::MidScalar(mid, kArrowAlpha), ui::MidScalar(mid, L"SelectorRotator"))
				: std::string("NOT hidden - the wheel's material was not found"));
	}

	void HoldCleared()
	{
		if (!g_snapHidden) {
			return;
		}
		auto* mid = reflect::Get(g_snapMid);
		if (mid && ui::MidScalar(mid, kArrowAlpha) != 0.0f) {
			ui::SetMidScalar(mid, kArrowAlpha, 0.0f);   // the Blueprint lit it again while the stick still rests
		}
	}

	void RestorePointer(const char* a_why)
	{
		if (!g_snapHidden) {
			return;
		}
		g_snapHidden = false;
		auto* mid = reflect::Get(g_snapMid);
		if (mid) {
			ui::SetMidScalar(mid, kArrowAlpha, g_snapAlpha);
		}
		g_snapMid = {};
		logger::info("quick keys: the selector arrow is back (alpha {:.2f}) - {}", g_snapAlpha, a_why);
	}

	void Repoint(int a_key)
	{
		auto* w = VisibleWheel();
		auto* fn = w ? w->FindFunction(UE::FName(L"Update Key Index", UE::EFindName::Find)) : nullptr;
		if (!fn || a_key < 0 || a_key > 7) {
			return;
		}
		if (auto* f = KeyIndexField()) {
			*f = a_key + 1;   // as the game's own pointing leaves it: the slot number as drawn
		}
		std::array<std::uint8_t, 32> params{};
		*reinterpret_cast<std::int32_t*>(params.data()) = a_key + 1;
		w->ProcessEvent(fn, params.data());   // through our watch: PointedSlot follows
	}

	bool PressQuickKey(int a_key)
	{
		if (a_key < 0 || a_key > 7) {
			return false;
		}
		// The game's own direct quick-key use: VEnhancedAltarPlayerController::Quick<N>Input_Pressed / _Released (no
		// parameters, read 2026-10-01 with ue.struct) - what Enhanced Input calls for IA_Game_QuickKeys_Keyboard_<N>, the
		// number keys 1-8, which use key N-1 with no radial at all.
		auto* pc = ui::PlayerController();
		if (!pc) {
			logger::warn("quick keys: no player controller - key {} not pressed", a_key + 1);
			return false;
		}
		const std::wstring n = std::to_wstring(a_key + 1);
		ui::Call press(pc, (L"Quick" + n + L"Input_Pressed").c_str());
		ui::Call release(pc, (L"Quick" + n + L"Input_Released").c_str());
		if (!press) {
			logger::warn("quick keys: the player controller has no Quick{}Input_Pressed - key {} not pressed", a_key + 1, a_key + 1);
			return false;
		}
		const bool ok = press.RunGuarded();
		if (release) {
			release.RunGuarded();
		}
		return ok;
	}

	Icons ReadIcons()
	{
		Icons out{};
		auto* cls = g_viewModelClass.load(std::memory_order_acquire);
		const std::int32_t off = cls && reflect::Ok() ? reflect::Offset(cls, "Icons") : -1;
		auto* f = KeyIndexField();   // also refreshes g_viewModel
		auto* vm = g_viewModel.load(std::memory_order_acquire);
		if (!f || !vm || off < 0) {
			return out;
		}
		struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
		const auto* arr = reflect::At<RawArray>(vm, off);
		for (std::int32_t i = 0; arr && arr->data && i < arr->num && i < static_cast<std::int32_t>(out.size()); ++i) {
			out[i] = arr->data[i];
		}
		return out;
	}

	void RequestRedraw() { g_redrew.store(true); }

	bool TakeRedrew() { return g_redrew.exchange(false); }

	bool RadialOpen()
	{
		std::scoped_lock l(g_statusLock);
		return g_status.open;
	}

	std::uint32_t OpenCount()
	{
		std::scoped_lock l(g_statusLock);
		return g_status.opens;
	}

	bool GameMessageUp(bool a_immediate)
	{
		static UE::UClass*     s_cls = nullptr;
		static reflect::Handle s_last;   // the instance seen up last time: asked first
		static std::vector<reflect::Handle> s_known;
		static messagediscovery::Gate s_discovery;
		if (!s_cls) {
			s_cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Game/UI/Modern/ModalLayer/Messages/WBP_ModernMenu_Message.WBP_ModernMenu_Message_C");
			if (!s_cls) return false;   // no message shown yet this session
			logger::info("quick keys: drawing message discovery is limited to100ms; input discovery stays immediate");
		}
		const auto up = [](UE::UObject* a_w) {
			if (!a_w) return false;
			ui::Call act(a_w, L"IsActivated");   // a CommonUI activatable widget: deactivated when answered
			if (act && act.Run() && !act.Get<bool>("ReturnValue")) return false;
			ui::Call vis(a_w, L"IsVisible");
			return vis && vis.Run() && vis.Get<bool>("ReturnValue");
		};
		if (up(reflect::Get(s_last))) return true;
		for (const auto& handle : s_known) {
			if (auto* widget = reflect::Get(handle); widget && up(widget)) {
				s_last = handle;
				return true;
			}
		}
		if (!s_discovery.Due(GetTickCount64(), a_immediate)) return false;
		s_known.clear();
		for (auto* w : reflect::Instances(s_cls)) {
			s_known.push_back(reflect::Hold(w));
			if (up(w)) {
				s_last = reflect::Hold(w);
				return true;
			}
		}
		return false;
	}

	bool PanelOpen()
	{
		std::scoped_lock l(g_statusLock);
		return g_status.panelOpen;
	}

	bool AnyWheelVisible() { return VisibleWheel() != nullptr; }

	UE::UObject* VisibleWheel()
	{
		// the wheel widgets, found by a whole-array scan at most every 2 s and kept by their slots
		static std::vector<reflect::Handle> s_known;
		static ULONGLONG                    s_nextScan = 0;
		auto* cls = g_widgetClass.load(std::memory_order_acquire);
		if (!cls || !reflect::Ok()) {
			return nullptr;
		}
		const ULONGLONG now = GetTickCount64();
		if (now >= s_nextScan) {
			s_nextScan = now + 2000;
			s_known.clear();
			for (auto* w : reflect::Instances(cls)) {
				s_known.push_back(reflect::Hold(w));
			}
		}
		for (const auto& h : s_known) {
			auto* w = reflect::Get(h);
			if (!w) {
				continue;
			}
			if (ui::ShownOnScreen(w)) {   // itself AND every parent (the magic menu hides the panel through a parent)
				return w;
			}
		}
		return nullptr;
	}

	int PointedSlot()
	{
		std::scoped_lock l(g_statusLock);
		return g_status.pointedSlot;
	}

	Status GetStatus()
	{
		std::scoped_lock l(g_statusLock);
		return g_status;
	}
}
