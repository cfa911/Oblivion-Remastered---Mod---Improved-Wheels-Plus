#include "engine/Ui.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"

namespace ui
{
	namespace
	{
		constexpr std::ptrdiff_t kElementSize = 0x34;
	}

	std::int32_t SizeOf(UE::UStruct* a_struct, std::string_view a_name)
	{
		const auto* field = reflect::FindField(a_struct, a_name);
		return field ? *reinterpret_cast<const std::int32_t*>(field + kElementSize) : -1;
	}

	Call::Call(UE::UObject* a_obj, const wchar_t* a_fn) :
		m_obj(a_obj),
		m_fn(a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr)
	{
		if (m_fn) m_params.assign(static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(m_fn)->propertiesSize, 0)) + 16, 0);
	}

	void* Call::At(std::string_view a_name)
	{
		if (!m_fn) return nullptr;
		const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name);
		return off >= 0 ? m_params.data() + off : nullptr;
	}

	bool Call::Run()
	{
		if (!m_fn || !m_obj) return false;
		m_obj->ProcessEvent(m_fn, m_params.data());
		return true;
	}

	bool GuardedProcessEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
	{
		__try {
			a_obj->ProcessEvent(a_fn, a_params);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool Call::RunGuarded() { return m_fn && m_obj && GuardedProcessEvent(m_obj, m_fn, m_params.data()); }

	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
	{
		auto* fn = a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
		auto* st = reinterpret_cast<UE::UStruct*>(fn);
		auto* f = st ? reinterpret_cast<std::uint8_t*>(st->childProperties) : nullptr;
		const auto off = f ? *reinterpret_cast<const std::int32_t*>(f + 0x44) : -1;
		if (off < 0) return false;
		std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(st->propertiesSize, 0)) + a_size + 16, 0);
		std::memcpy(params.data() + off, a_bytes, a_size);
		a_obj->ProcessEvent(fn, params.data());
		return true;
	}

	void Vec2(UE::UObject* a_obj, const wchar_t* a_fn, double a_x, double a_y)
	{
		const double v[2] = { a_x, a_y };
		CallFirst(a_obj, a_fn, v, sizeof(v));
	}

	void Float(UE::UObject* a_obj, const wchar_t* a_fn, float a_v) { CallFirst(a_obj, a_fn, &a_v, sizeof(a_v)); }

	void Visible(UE::UObject* a_widget, bool a_visible)
	{
		if (!a_widget) return;
		Call c(a_widget, L"SetVisibility");
		c.Set("InVisibility", static_cast<std::uint8_t>(a_visible ? 3 : 1));
		c.Run();
	}

	void Colour(UE::UObject* a_image, float a_r, float a_g, float a_b, float a_a)
	{
		const float c[4] = { a_r, a_g, a_b, a_a };
		CallFirst(a_image, L"SetColorAndOpacity", c, sizeof(c));
	}

	UE::UClass* Class(const wchar_t* a_path) { return UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_path); }

	UE::UObject* Load(const wchar_t* a_path)
	{
		if (!a_path) return nullptr;
		if (auto* o = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path)) return o;
		static auto* lib = Class(L"/Script/Engine.KismetSystemLibrary");
		auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!cdo) return nullptr;
		Call mk(cdo, L"MakeSoftObjectPath");
		Call conv(cdo, L"Conv_SoftObjPathToSoftObjRef");
		Call load(cdo, L"LoadAsset_Blocking");
		void*      path = mk.At("PathString");
		const auto pathSize = mk.Size("ReturnValue");
		const auto refSize = conv.Size("ReturnValue");
		if (!mk || !conv || !load || !path || pathSize <= 0 || refSize <= 0 || conv.Size("SoftObjectPath") != pathSize || load.Size("Asset") != refSize) {
			return nullptr;
		}
		// the FString is built in place and never destroyed - a small, one-time leak per asset (as in Minimap Menu)
		new (path) UE::FString(a_path);
		mk.Run();
		std::memcpy(conv.At("SoftObjectPath"), mk.At("ReturnValue"), static_cast<std::size_t>(pathSize));
		conv.Run();
		std::memcpy(load.At("Asset"), conv.At("ReturnValue"), static_cast<std::size_t>(refSize));
		load.Run();
		return load.Get<UE::UObject*>("ReturnValue");
	}

	UE::UObject* PlayerController()
	{
		static reflect::Handle cached;
		static ULONGLONG       lastScan = 0;
		if (auto* pc = reflect::Get(cached)) return pc;
		const ULONGLONG now = GetTickCount64();
		if (now - lastScan < 2000) return nullptr;
		lastScan = now;
		// the game's controller is a SUBCLASS (VAltarPlayerController): reflect::Instances matches one class exactly and so
		// never found it - the ammo wheel was never built ("cannot be built yet", every press, 2026-09-29 23:55 on)
		auto* found = reflect::FirstOf(Class(L"/Script/Engine.PlayerController"));
		cached = found ? reflect::Hold(found) : reflect::Handle{};
		return found;
	}

	UE::UObject* CreateWidget(const wchar_t* a_classPath)
	{
		static auto* lib = Class(L"/Script/UMG.WidgetBlueprintLibrary");
		auto* cls = Class(a_classPath);
		auto* pc = PlayerController();
		auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!cdo || !cls || !pc) return nullptr;
		Call c(cdo, L"Create");
		c.Set("WorldContextObject", pc);
		c.Set("WidgetType", cls);
		c.Set("OwningPlayer", pc);
		if (!c.RunGuarded()) return nullptr;   // a world-context call: fault-guarded (a quit, a load)
		return c.Get<UE::UObject*>("ReturnValue");
	}

	UE::UObject* RootCanvas(UE::UObject* a_userWidget, const wchar_t* a_name)
	{
		auto* treeClass = Class(L"/Script/UMG.WidgetTree");
		auto* canvasClass = Class(L"/Script/UMG.CanvasPanel");
		if (!a_userWidget || !treeClass || !canvasClass || !a_userWidget->GetClass()) return nullptr;
		auto** tree = reflect::At<UE::UObject*>(a_userWidget, reflect::Offset(a_userWidget->GetClass(), "WidgetTree"));
		if (!tree) return nullptr;
		if (!*tree) *tree = UE::NewObject<UE::UObject>(a_userWidget, treeClass, UE::FName((std::wstring(a_name) + L"Tree").c_str()));
		if (!*tree) return nullptr;
		auto* canvas = UE::NewObject<UE::UObject>(*tree, canvasClass, UE::FName(a_name));
		auto** root = reflect::At<UE::UObject*>(*tree, reflect::Offset(treeClass, "RootWidget"));
		if (!canvas || !root) return nullptr;
		*root = canvas;
		return canvas;
	}

	UE::UObject* AddToCanvas(UE::UObject* a_canvas, const wchar_t* a_class, const wchar_t* a_name, UE::UObject** a_slot)
	{
		*a_slot = nullptr;
		auto* cls = Class(a_class);
		auto* outer = a_canvas ? a_canvas->GetOuter() : nullptr;   // the widget tree
		if (!cls || !outer) return nullptr;
		auto* w = UE::NewObject<UE::UObject>(outer, cls, UE::FName(a_name));
		if (!w) return nullptr;
		Call add(a_canvas, L"AddChildToCanvas");
		add.Set("Content", w);
		add.Run();
		*a_slot = add.Get<UE::UObject*>("ReturnValue");
		if (!*a_slot) return nullptr;
		const bool no = false;
		CallFirst(*a_slot, L"SetAutoSize", &no, sizeof(no));
		return w;
	}

	void Anchors(UE::UObject* a_slot, double a_minX, double a_minY, double a_maxX, double a_maxY)
	{
		const double a[4] = { a_minX, a_minY, a_maxX, a_maxY };
		CallFirst(a_slot, L"SetAnchors", a, sizeof(a));
	}

	bool RoundedBox(UE::UObject* a_image, bool a_circle, const float a_fill[4], const float a_outline[4], float a_width)
	{
		static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
		static auto* outlineStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrushOutlineSettings"));
		static auto* colorStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateColor"));
		if (!a_image || !brushStruct || !outlineStruct || !colorStruct || !a_image->GetClass()) return false;
		const auto brushOff = reflect::Offset(a_image->GetClass(), "Brush");
		const auto brushSize = SizeOf(a_image->GetClass(), "Brush");
		const auto drawAs = reflect::Offset(brushStruct, "DrawAs");
		const auto tint = reflect::Offset(brushStruct, "TintColor");
		const auto outline = reflect::Offset(brushStruct, "OutlineSettings");
		const auto radii = reflect::Offset(outlineStruct, "CornerRadii");
		const auto radiiSize = SizeOf(outlineStruct, "CornerRadii");
		const auto oColor = reflect::Offset(outlineStruct, "Color");
		const auto rounding = reflect::Offset(outlineStruct, "RoundingType");
		const auto width = reflect::Offset(outlineStruct, "Width");
		const auto spec = reflect::Offset(colorStruct, "SpecifiedColor");
		const auto rule = reflect::Offset(colorStruct, "ColorUseRule");
		if (brushOff < 0 || brushSize <= 0 || drawAs < 0 || tint < 0 || outline < 0 || radii < 0 || oColor < 0 || rounding < 0 || width < 0 ||
			spec < 0 || rule < 0) {
			return false;
		}
		auto* b = reinterpret_cast<std::uint8_t*>(a_image) + brushOff;
		b[drawAs] = 4;   // RoundedBox
		std::memcpy(b + tint + spec, a_fill, sizeof(float) * 4);
		b[tint + rule] = 0;
		std::memcpy(b + outline + oColor + spec, a_outline, sizeof(float) * 4);
		b[outline + oColor + rule] = 0;
		const double r = 6.0;
		if (radiiSize == 32) {
			const double v[4] = { r, r, r, r };
			std::memcpy(b + outline + radii, v, sizeof(v));
		} else if (radiiSize == 16) {
			const float v[4] = { 6, 6, 6, 6 };
			std::memcpy(b + outline + radii, v, sizeof(v));
		}
		b[outline + rounding] = a_circle ? 1 : 0;
		std::memcpy(b + outline + width, &a_width, sizeof(a_width));
		std::vector<std::uint8_t> copy(b, b + brushSize);
		return CallFirst(a_image, L"SetBrush", copy.data(), copy.size());
	}
}

namespace ui
{
	UE::UObject* ChildNamed(UE::UObject* a_userWidget, const wchar_t* a_name)
	{
		if (!a_userWidget || !a_name) return nullptr;
		Call c(a_userWidget, L"GetWidgetFromName");
		if (!c) return nullptr;
		c.Set("Name", UE::FName(a_name, UE::EFindName::Find));
		return c.Run() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
	}

	UE::UObject* BrushResource(UE::UObject* a_image)
	{
		static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
		if (!a_image || !brushStruct || !reflect::Ok()) return nullptr;
		const auto brushOff = reflect::Offset(reinterpret_cast<UE::UStruct*>(a_image->GetClass()), "Brush");
		const auto resOff = reflect::Offset(brushStruct, "ResourceObject");
		if (brushOff < 0 || resOff < 0) return nullptr;
		return *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_image) + brushOff + resOff);
	}

	float RenderOpacity(UE::UObject* a_widget)
	{
		static auto* widget = Class(L"/Script/UMG.Widget");
		const auto off = widget && a_widget && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(widget), "RenderOpacity") : -1;
		return off >= 0 ? *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(a_widget) + off) : 1.0f;
	}

	bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_width, double& a_height)
	{
		auto* pc = PlayerController();
		static auto* lib = Class(L"/Script/UMG.SlateBlueprintLibrary");
		auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!a_w || !pc || !cdo) return false;
		Call geo(a_w, L"GetCachedGeometry");
		const auto gsize = geo.Size("ReturnValue");
		if (!geo || gsize <= 0 || !geo.Run()) return false;
		Call size(cdo, L"GetLocalSize");
		void* g = size.At("Geometry");
		if (!size || !g || size.Size("Geometry") != gsize) return false;
		std::memcpy(g, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
		size.Run();
		const auto local = size.Get<std::array<double, 2>>("ReturnValue");
		const auto toViewport = [&](double a_lx, double a_ly, double& a_vx, double& a_vy) {
			Call c(cdo, L"LocalToViewport");
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
		if (!toViewport(0.0, 0.0, x0, y0) || !toViewport(local[0], local[1], x1, y1)) return false;
		a_x = x0;
		a_y = y0;
		a_width = x1 - x0;
		a_height = y1 - y0;
		return local[0] > 0.0 && a_width > 1.0 && a_height > 1.0;
	}

	float MidScalar(UE::UObject* a_mid, const wchar_t* a_name)
	{
		Call c(a_mid, L"K2_GetScalarParameterValue");
		if (!c) return 0.0f;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		return c.Run() ? c.Get<float>("ReturnValue") : 0.0f;
	}

	void SetMidScalar(UE::UObject* a_mid, const wchar_t* a_name, float a_v)
	{
		Call c(a_mid, L"SetScalarParameterValue");
		if (!c) return;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		c.Set("Value", a_v);
		c.Run();
	}

	void SetMidTexture(UE::UObject* a_mid, const wchar_t* a_name, UE::UObject* a_texture)
	{
		Call c(a_mid, L"SetTextureParameterValue");
		if (!c) return;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		c.Set("Value", a_texture);
		c.Run();
	}

	UE::UObject* MakeMaterialInstance(UE::UObject* a_parent)
	{
		static auto* mlib = Class(L"/Script/Engine.KismetMaterialLibrary");
		Call c(mlib ? mlib->GetDefaultObject(false) : nullptr, L"CreateDynamicMaterialInstance");
		c.Set("WorldContextObject", PlayerController());
		c.Set("Parent", a_parent);
		return c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
	}

	bool SetBrushMaterial(UE::UObject* a_image, UE::UObject* a_material)
	{
		Call b(a_image, L"SetBrushFromMaterial");
		b.Set("Material", a_material);
		return b.Run();
	}
}

namespace ui
{
	// on screen for real: the widget and every widget above it visible (not Collapsed / Hidden) and not faded out - the
	// magic menu hides its wheel panel through a parent, so the wheel's own IsVisible stayed true after it was hidden
	// (the owner, 2026-09-30: "after you hide the wheel menu, the title of it still stays")
	bool ShownOnScreen(UE::UObject* a_widget)
	{
		static auto* widgetClass = Class(L"/Script/UMG.Widget");
		static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
		static auto* slotClass = Class(L"/Script/UMG.PanelSlot");
		static auto* switcherClass = Class(L"/Script/UMG.WidgetSwitcher");
		const auto slotOff = widgetClass && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(widgetClass), "Slot") : -1;
		const auto parentOff = slotClass && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(slotClass), "Parent") : -1;
		UE::UObject* w = a_widget;
		for (int depth = 0; w && depth < 40; ++depth) {
			Call vis(w, L"IsVisible");
			if (!vis) {
				return depth > 0;   // above the widgets: the game instance or a viewport owner - shown so far
			}
			if (!vis.Run() || !vis.Get<bool>("ReturnValue")) return false;
			if (RenderOpacity(w) < 0.05f) return false;
			UE::UObject* next = nullptr;
			auto* slot = slotOff >= 0 ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(w) + slotOff) : nullptr;
			if (slot && parentOff >= 0) {
				next = *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(slot) + parentOff);
			}
			// a WidgetSwitcher draws only its active child, though every child reads visible: the magic menu's wheel panel
			// (Magic_QuickKeys) shares RightPanel_Switcher with the description, and read as shown under it (probe A,
			// 2026-10-01: every parent identical with the panel hidden and shown)
			if (next && switcherClass && next->GetClass()->IsChildOf(switcherClass)) {
				Call active(next, L"GetActiveWidget");
				if (active && active.Run() && active.Get<UE::UObject*>("ReturnValue") != w) return false;
			}
			if (!next) {
				// a tree's root widget: the user widget that owns the tree (outer of its WidgetTree)
				auto* outer = w->GetOuter();
				next = outer && treeClass && outer->GetClass() == treeClass ? outer->GetOuter() : nullptr;
			}
			w = next;
		}
		return true;
	}

	bool IsWithin(UE::UObject* a_obj, UE::UObject* a_ancestor)
	{
		for (auto* o = a_obj; o; o = o->GetOuter()) {
			if (o == a_ancestor) return true;
		}
		return false;
	}
}

namespace ui
{
	namespace
	{
		UE::UObject* PropObj(UE::UObject* a_o, UE::UClass* a_declaring, std::string_view a_name)
		{
			const auto off = a_o && a_declaring && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(a_declaring), a_name) : -1;
			return off >= 0 ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_o) + off) : nullptr;
		}

		bool IsA(UE::UObject* a_o, UE::UClass* a_class)
		{
			for (auto* c = a_o ? static_cast<UE::UStruct*>(a_o->GetClass()) : nullptr; c; c = c->superStruct) {
				if (c == reinterpret_cast<UE::UStruct*>(a_class)) return true;
			}
			return false;
		}

		UE::UObject* Search(UE::UObject* a_w, std::string_view a_name, int a_depth)
		{
			static auto* userWidget = Class(L"/Script/UMG.UserWidget");
			static auto* panelWidget = Class(L"/Script/UMG.PanelWidget");
			static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
			static auto* panelSlot = Class(L"/Script/UMG.PanelSlot");
			if (!a_w || a_depth > 24) return nullptr;
			if (a_depth > 0 && pe::Utf8(a_w->GetFName().ToString()) == a_name) return a_w;
			if (IsA(a_w, userWidget)) {   // a user widget: into its own tree
				auto* tree = PropObj(a_w, userWidget, "WidgetTree");
				auto* root = tree ? PropObj(tree, treeClass, "RootWidget") : nullptr;
				if (auto* found = root ? (pe::Utf8(root->GetFName().ToString()) == a_name ? root : Search(root, a_name, a_depth + 1)) : nullptr) return found;
			}
			if (IsA(a_w, panelWidget)) {   // a panel: into each slot's content
				const auto slotsOff = reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(panelWidget), "Slots") : -1;
				if (slotsOff < 0) return nullptr;
				struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
				const auto* arr = reinterpret_cast<const RawArray*>(reinterpret_cast<const std::uint8_t*>(a_w) + slotsOff);
				for (std::int32_t i = 0; arr->data && i < arr->num && i < 256; ++i) {
					auto* content = PropObj(arr->data[i], panelSlot, "Content");
					if (!content) continue;
					if (pe::Utf8(content->GetFName().ToString()) == a_name) return content;
					if (auto* found = Search(content, a_name, a_depth + 1)) return found;
				}
			}
			// content widgets with one child (SizeBox, Border...) are panels too; nothing else holds children
			return nullptr;
		}

	}

	// A row or widget being built or destroyed while the walk reads it can hold a garbage slot pointer: an access violation
	// here (seen as a crash on opening the inventory) ends the walk with "not found" instead of ending the game.
	static UE::UObject* SearchGuarded(UE::UObject* a_w, std::string_view a_name, bool* a_faulted)
	{
		__try {
			return Search(a_w, a_name, 0);
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			*a_faulted = true;
			return nullptr;
		}
	}

	UE::UObject* FindInTree(UE::UObject* a_userWidget, std::string_view a_name)
	{
		bool faulted = false;
		auto* found = SearchGuarded(a_userWidget, a_name, &faulted);
		if (faulted) {
			static bool logged = false;
			if (!logged) {
				logged = true;
				logger::warn("ui: a widget tree could not be read (access violation) while looking for {} - skipped, not crashed", std::string(a_name));
			}
		}
		return found;
	}

}

namespace ui
{
	UE::UObject* MidTexture(UE::UObject* a_mid, const wchar_t* a_name)
	{
		Call c(a_mid, L"K2_GetTextureParameterValue");
		if (!c) return nullptr;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		return c.Run() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
	}
}
