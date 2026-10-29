#include "engine/Reflect.h"

#include "engine/PEHook.h"

namespace reflect
{
	namespace
	{
		constexpr std::ptrdiff_t kFieldNext = 0x18;
		constexpr std::ptrdiff_t kFieldName = 0x20;
		constexpr std::ptrdiff_t kPropertyOffsetInternal = 0x44;

		std::atomic<int> g_state{ 0 };   // 0 unchecked, 1 proven, -1 failed
	}

	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name)
	{
		for (UE::UStruct* s = a_struct; s; s = s->superStruct) {
			for (auto* f = reinterpret_cast<std::uint8_t*>(s->childProperties); f; f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
				const auto& name = *reinterpret_cast<const UE::FName*>(f + kFieldName);
				if (pe::Utf8(name.ToString()) == a_name) {
					return *reinterpret_cast<const std::int32_t*>(f + kPropertyOffsetInternal);
				}
			}
		}
		return -1;
	}

	std::vector<std::pair<std::string, std::int32_t>> Fields(UE::UStruct* a_struct)
	{
		std::vector<std::pair<std::string, std::int32_t>> out;
		for (auto* f = a_struct ? reinterpret_cast<std::uint8_t*>(a_struct->childProperties) : nullptr; f;
			 f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
			out.emplace_back(pe::Utf8(reinterpret_cast<const UE::FName*>(f + kFieldName)->ToString()),
				*reinterpret_cast<const std::int32_t*>(f + kPropertyOffsetInternal));
		}
		return out;
	}

	bool SelfCheck()
	{
		if (g_state.load() != 0) {
			return g_state.load() > 0;
		}
		auto* vm = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VQuickKeysMenuViewModel");
		if (!vm) {
			return false;   // not loaded yet: ask again later
		}
		const auto keyIndex = Offset(vm, "KeyIndex");
		const auto icons = Offset(vm, "Icons");
		if (keyIndex < 0) {
			return false;   // the class exists before its property chain is linked (the first frames of a launch): ask again, never latch
		}
		g_state.store(keyIndex == 0xD0 ? 1 : -1);
		if (g_state.load() > 0) {
			logger::info("reflect: property offsets proven (VQuickKeysMenuViewModel KeyIndex at 0x{:X}, Icons at 0x{:X})", keyIndex, icons);
		} else {
			logger::error("reflect: KeyIndex read at 0x{:X}, expected 0xD0 - the property layout is not UE5's; Favourite and the Magic wheel's icons are off",
				keyIndex);
		}
		return g_state.load() > 0;
	}

	bool Ok() { return g_state.load() > 0; }

	namespace
	{
		// A row the engine is tearing down (its list was rebuilt, e.g. as a spell is given a wheel slot in the Magic menu) can still
		// pass the liveness check while the text it held is already freed: the engine then faults reading it (crash 2026-10-18,
		// StringTableIdAndKeyFromText through the Magic wheel's icon harvest). The reads are fault-guarded: such a text reads as empty.
		// No C++ objects here: __try cannot unwind them.
		bool Guarded(void (*a_fn)(void*), void* a_ctx)
		{
			__try {
				a_fn(a_ctx);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		struct ToStringCtx
		{
			const UE::FText* text;
			std::string      out;
		};

		void ToStringRun(void* a_ctx)
		{
			auto* c = static_cast<ToStringCtx*>(a_ctx);
			c->out = pe::Utf8(c->text->ToString());
		}

		struct EventCtx
		{
			UE::UObject*   obj;
			UE::UFunction* fn;
			void*          params;
		};

		void EventRun(void* a_ctx)
		{
			auto* c = static_cast<EventCtx*>(a_ctx);
			c->obj->ProcessEvent(c->fn, c->params);
		}
	}

	std::string Text(const UE::FText& a_text)
	{
		if (!TextSet(a_text)) {
			return {};
		}
		ToStringCtx ctx{ &a_text, {} };
		if (!Guarded(&ToStringRun, &ctx)) {
			logger::warn("reflect: a row's text could not be read (the row is being torn down) - read as empty");
			return {};
		}
		return ctx.out;
	}

	bool TextSet(const UE::FText& a_text)
	{
		// TSharedRef<ITextData>: the object and its reference controller. A row whose Properties were never filled (a
		// list entry built but not yet given an item) holds zeroes here, and the engine reads through them - the crash
		// of 2026-09-29 02:44 (StringTableIdAndKeyFromText on such a row, as the inventory's panel opened).
		const auto* raw = reinterpret_cast<void* const*>(&a_text);
		return raw[0] != nullptr && raw[1] != nullptr;
	}

	std::string TextKey(const UE::FText& a_text)
	{
		if (!TextSet(a_text)) {
			return {};
		}
		static UE::UObject*   cdo = nullptr;
		static UE::UFunction* fn = nullptr;
		static std::int32_t   offText = -1, offKey = -1, offRet = -1, size = 0;
		if (!fn) {
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Engine.KismetTextLibrary");
			cdo = cls ? cls->GetDefaultObject(false) : nullptr;
			fn = cdo ? cdo->FindFunction(UE::FName(L"StringTableIdAndKeyFromText", UE::EFindName::Find)) : nullptr;
			if (!fn) {
				return {};
			}
			auto* st = reinterpret_cast<UE::UStruct*>(fn);
			offText = Offset(st, "Text");
			offKey = Offset(st, "OutKey");
			offRet = Offset(st, "ReturnValue");
			size = st->propertiesSize;
			logger::info("reflect: StringTableIdAndKeyFromText params Text 0x{:X}, OutKey 0x{:X}, ReturnValue 0x{:X}, size 0x{:X}", offText, offKey,
				offRet, size);
		}
		if (offText < 0 || offKey < 0 || offRet < 0 || size <= 0) {
			return {};
		}
		std::vector<std::uint8_t> params(static_cast<std::size_t>(size), 0);
		std::memcpy(params.data() + offText, &a_text, sizeof(UE::FText));   // borrowed: never destroyed here, so its reference count stays right
		EventCtx ev{ cdo, fn, params.data() };
		if (!Guarded(&EventRun, &ev)) {
			logger::warn("reflect: a row's text key could not be read (the row is being torn down) - read as empty");
			return {};
		}
		auto* key = reinterpret_cast<UE::FString*>(params.data() + offKey);
		std::string out = params[offRet] ? pe::Utf8(*key) : std::string();
		if (void* data = *reinterpret_cast<void**>(key)) {
			UE::FMemory::Free(data);   // the engine allocated the key's characters
		}
		return out;
	}

	bool IsLive(UE::UObject* a_o)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!a_o || !arr) {
			return false;
		}
		const std::int32_t idx = a_o->internalIndex;
		if (idx < 0 || idx >= arr->GetObjectArrayNum()) {
			return false;
		}
		auto* item = arr->IndexToObject(idx);
		return item && reinterpret_cast<UE::UObject*>(item->object) == a_o;
	}

	Handle Hold(UE::UObject* a_live)
	{
		return a_live ? Handle{ a_live, a_live->internalIndex } : Handle{};
	}

	UE::UObject* Get(UE::UObject* a_ptr, std::int32_t a_index)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!a_ptr || !arr || a_index < 0 || a_index >= arr->GetObjectArrayNum()) {
			return nullptr;
		}
		auto* item = arr->IndexToObject(a_index);
		return item && reinterpret_cast<UE::UObject*>(item->object) == a_ptr ? a_ptr : nullptr;
	}

	UE::UObject* Get(const Handle& a_handle)
	{
		return Get(a_handle.ptr, a_handle.index);
	}

	UE::UObject* FirstOf(UE::UClass* a_class)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		UE::UObject* found = nullptr;
		if (!a_class || !arr) {
			return found;
		}
		arr->LockInternalArray();
		const std::int32_t n = arr->GetObjectArrayNum();
		for (std::int32_t i = 0; i < n && !found; ++i) {
			auto* item = arr->IndexToObject(i);
			auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
			auto* oc = o ? o->GetClass() : nullptr;
			if (oc && oc->IsChildOf(a_class) && (static_cast<std::int32_t>(o->objectFlags) & kTemplateFlags) == 0) {
				found = o;
			}
		}
		arr->UnlockInternalArray();
		return found;
	}

	std::vector<UE::UObject*> Instances(UE::UClass* a_class)
	{
		std::vector<UE::UObject*> out;
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!arr || !a_class) {
			return out;
		}
		auto* cdo = a_class->GetDefaultObject(false);
		arr->LockInternalArray();
		const std::int32_t n = arr->GetObjectArrayNum();
		for (std::int32_t i = 0; i < n; ++i) {
			auto* item = arr->IndexToObject(i);
			auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
			// templates are never the live thing: the class default object, and archetypes - a widget blueprint's
			// WidgetTree template (WBP_ModernMenu_MagicMenu's Magic_QuickKeys under /Game/...) read as a wheel panel on
			// screen, so the magic menu's panel "showed" the moment the menu opened (probe A, 2026-10-01)
			if (o && o->GetClass() == a_class && o != cdo && (static_cast<std::int32_t>(o->objectFlags) & kTemplateFlags) == 0) {
				out.push_back(o);
			}
		}
		arr->UnlockInternalArray();
		return out;
	}

	bool Call(UE::UObject* a_obj, const wchar_t* a_function, void* a_params)
	{
		if (!a_obj) {
			return false;
		}
		auto* fn = a_obj->FindFunction(UE::FName(a_function, UE::EFindName::Find));
		if (!fn) {
			return false;
		}
		a_obj->ProcessEvent(fn, a_params);
		return true;
	}
}
