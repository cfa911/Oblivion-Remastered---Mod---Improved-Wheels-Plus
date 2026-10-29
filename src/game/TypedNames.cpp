#include "game/TypedNames.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"

#include <unordered_map>

namespace typednames
{
	namespace
	{
		struct RawArr { std::uint8_t* data; std::int32_t num; std::int32_t max; };

		// a key string of the form UI_UserInputText... at a_el: copied into a_out (UTF-16, a_cap wchar); false if it is not one
		bool ReadKeyGuarded(const std::uint8_t* a_el, wchar_t* a_out, int a_cap)
		{
			__try {
				const auto* chars = *reinterpret_cast<const wchar_t* const*>(a_el);
				const auto  num = *reinterpret_cast<const std::int32_t*>(a_el + 8);
				if (reinterpret_cast<std::uintptr_t>(chars) < 0x10000 || num < 16 || num >= a_cap) return false;
				for (int i = 0; i < num; ++i) a_out[i] = chars[i];
				a_out[num] = 0;
				return a_out[num - 1] == 0 && std::wcsncmp(a_out, L"UI_UserInputText", 16) == 0;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		void TextToBuf(const UE::FText* a_text, char* a_out, int a_cap)
		{
			const std::string t = reflect::Text(*a_text);
			const int n = static_cast<int>(std::min<std::size_t>(t.size(), static_cast<std::size_t>(a_cap - 1)));
			std::memcpy(a_out, t.data(), static_cast<std::size_t>(n));
			a_out[n] = 0;
		}

		bool TextToBufGuarded(const UE::FText* a_text, char* a_out, int a_cap)
		{
			__try {
				TextToBuf(a_text, a_out, a_cap);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		bool ReadArrGuarded(const std::uint8_t* a_map, RawArr* a_out)
		{
			__try {
				*a_out = *reinterpret_cast<const RawArr*>(a_map);
				return a_out->data && a_out->num > 0 && a_out->num < 100000;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		std::unordered_map<std::string, std::string> g_typed;   // placeholder -> the text the player typed
		UE::UObject*  g_saveData = nullptr;
		std::int32_t  g_saveDataSlot = -1;
		ULONGLONG     g_typedAt = 0;
		std::size_t   g_typedStride = 0;

		UE::UObject* FindSaveData()
		{
			if (g_saveData && reflect::Get(g_saveData, g_saveDataSlot)) return g_saveData;
			g_saveData = nullptr;
			auto* arr = UE::FUObjectArray::GetSingleton();
			if (!arr) return nullptr;
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n && !g_saveData; ++i) {
				auto* item = arr->IndexToObject(i);
				if (!item || !item->object) continue;
				auto* o = reinterpret_cast<UE::UObject*>(item->object);
				if (pe::Utf8(o->GetClass()->GetFName().ToString()) == "UserInputTextSaveData" && !pe::Utf8(o->GetFName().ToString()).starts_with("Default__")) {
					g_saveData = o;
					g_saveDataSlot = i;
				}
			}
			arr->UnlockInternalArray();
			return g_saveData;
		}

		void LoadTyped()
		{
			auto* sd = FindSaveData();
			if (!sd) return;
			const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(sd->GetClass()), "UserInputTextsMap");
			if (off < 0) return;
			RawArr arr{};
			if (!ReadArrGuarded(reinterpret_cast<const std::uint8_t*>(sd) + off, &arr)) return;
			// the stride: whichever of the likely sizes lets the most of the first elements read as keys
			std::size_t best = 0;
			int         bestHits = 0;
			for (const std::size_t stride : { std::size_t(48), std::size_t(56), std::size_t(40), std::size_t(64), std::size_t(32) }) {
				int hits = 0;
				wchar_t key[160];
				for (int i = 0; i < std::min(arr.num, 24); ++i) {
					hits += ReadKeyGuarded(arr.data + static_cast<std::size_t>(i) * stride, key, 160) ? 1 : 0;
				}
				if (hits > bestHits) bestHits = hits, best = stride;
			}
			if (!best) return;
			std::unordered_map<std::string, std::string> fresh;
			for (int i = 0; i < arr.num; ++i) {
				const auto* el = arr.data + static_cast<std::size_t>(i) * best;
				wchar_t key[160];
				if (!ReadKeyGuarded(el, key, 160)) continue;
				char text[256];
				if (!TextToBufGuarded(reinterpret_cast<const UE::FText*>(el + 16), text, 256)) continue;
				std::string k;
				for (const wchar_t* c = key; *c; ++c) k.push_back(static_cast<char>(*c));
				fresh[k] = text;
			}
			if (best != g_typedStride || fresh.size() != g_typed.size()) {
				logger::info("rows: the player's typed texts - {} read (element size {}, {} slots)", fresh.size(), best, arr.num);
				int shown = 0;
				for (const auto& [k, v] : fresh) {
					if (shown++ < 4) logger::info("rows:   {} = \"{}\"", k, v);
				}
			}
			g_typedStride = best;
			g_typed = std::move(fresh);
		}
	}

	std::string Text(const std::string& a_placeholder)
	{
		const ULONGLONG now = GetTickCount64();
		if (now - g_typedAt >= 2000) {
			g_typedAt = now;
			g_typed.clear();   // the same placeholder can mean something else in an earlier save
			LoadTyped();
		}
		if (const auto it = g_typed.find(a_placeholder); it != g_typed.end()) return it->second;
		return {};
	}

	void Reset()
	{
		g_typed.clear();
		g_saveData = nullptr;
		g_saveDataSlot = -1;
		g_typedAt = 0;
		g_typedStride = 0;
	}
}
