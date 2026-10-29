#include "engine/PEHook.h"
#include "core/TextUtil.h"

namespace pe
{
	namespace
	{
		constexpr std::size_t kProcessEventSlot = 0x4D;
		using ProcessEvent_t = void (*)(UE::UObject*, UE::UFunction*, void*);

		struct Swapped { void** vtable; ProcessEvent_t original; };
		struct Watcher { UE::UClass* cls; Handler handler; bool post; };

		// Small fixed tables, written only under the lock and only ever appended to; the detour reads them lock-free
		// (a count published after the entry is complete).
		constexpr std::size_t kMax = 16;
		std::array<Swapped, kMax> g_swapped{};
		std::atomic<std::size_t>  g_swappedCount{ 0 };
		std::array<Watcher, kMax> g_watchers{};
		std::atomic<std::size_t>  g_watcherCount{ 0 };
		std::mutex                g_lock;

		void Detour(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			ProcessEvent_t original = nullptr;
			void** vt = a_obj ? *reinterpret_cast<void***>(a_obj) : nullptr;
			const std::size_t ns = g_swappedCount.load(std::memory_order_acquire);
			for (std::size_t i = 0; i < ns; ++i) {
				if (g_swapped[i].vtable == vt) {
					original = g_swapped[i].original;
					break;
				}
			}
			UE::UClass* cls = nullptr;
			if (a_obj && a_fn) {
				cls = a_obj->GetClass();
				const std::size_t nw = g_watcherCount.load(std::memory_order_acquire);
				for (std::size_t i = 0; i < nw; ++i) {
					if (g_watchers[i].cls == cls && !g_watchers[i].post) {
						g_watchers[i].handler(a_obj, a_fn, a_params);
					}
				}
			}
			if (original) {
				original(a_obj, a_fn, a_params);
			}
			if (cls) {   // after the body: the post watchers
				const std::size_t nw = g_watcherCount.load(std::memory_order_acquire);
				for (std::size_t i = 0; i < nw; ++i) {
					if (g_watchers[i].cls == cls && g_watchers[i].post) {
						g_watchers[i].handler(a_obj, a_fn, a_params);
					}
				}
			}
		}
	}

	std::string NameOf(const UE::FName& a_name) { return Utf8(a_name.ToString()); }

	std::string NameOf(UE::UObject* a_object) { return a_object ? NameOf(a_object->GetFName()) : std::string("none"); }

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

	std::wstring Wide(const std::string& a_s)
	{
		return textutil::Wide(a_s);
	}

	std::string FunctionName(UE::UFunction* a_fn)
	{
		return a_fn ? Utf8(a_fn->GetFName().ToString()) : std::string();
	}

	static bool WatchImpl(UE::UClass* a_class, Handler a_handler, bool a_post)
	{
		auto* cdo = a_class ? a_class->GetDefaultObject(false) : nullptr;
		if (!cdo || !a_handler) {
			return false;
		}
		void** vt = *reinterpret_cast<void***>(cdo);
		if (!vt || !vt[kProcessEventSlot]) {
			logger::warn("pe: {} has no readable vtable", Utf8(a_class->GetFullName()));
			return false;
		}
		std::scoped_lock l(g_lock);
		const std::size_t nw = g_watcherCount.load(std::memory_order_relaxed);
		for (std::size_t i = 0; i < nw; ++i) {
			if (g_watchers[i].cls == a_class && g_watchers[i].handler == a_handler && g_watchers[i].post == a_post) {
				return true;   // already watched
			}
		}
		if (nw >= kMax) {
			logger::error("pe: watcher table full");
			return false;
		}
		bool swapped = false;
		const std::size_t ns = g_swappedCount.load(std::memory_order_relaxed);
		for (std::size_t i = 0; i < ns; ++i) {
			swapped = swapped || g_swapped[i].vtable == vt;
		}
		if (!swapped) {
			if (ns >= kMax) {
				logger::error("pe: vtable table full");
				return false;
			}
			void** slot = &vt[kProcessEventSlot];
			DWORD oldProtect = 0;
			if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
				logger::warn("pe: VirtualProtect on {} failed ({})", Utf8(a_class->GetFullName()), GetLastError());
				return false;
			}
			g_swapped[ns] = { vt, reinterpret_cast<ProcessEvent_t>(*slot) };
			g_swappedCount.store(ns + 1, std::memory_order_release);   // the original is known before the slot points here
			*slot = reinterpret_cast<void*>(&Detour);
			DWORD ignored = 0;
			VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
			logger::info("pe: vtable {:p} slot 0x{:X} swapped for {} (previous target {:p}, UE4SS {})", static_cast<void*>(vt),
				kProcessEventSlot, Utf8(a_class->GetFullName()), reinterpret_cast<void*>(g_swapped[ns].original),
				GetModuleHandleW(L"UE4SS.dll") ? "loaded" : "not loaded");
		}
		g_watchers[nw] = { a_class, a_handler, a_post };
		g_watcherCount.store(nw + 1, std::memory_order_release);
		return true;
	}

	bool Watch(UE::UClass* a_class, Handler a_handler) { return WatchImpl(a_class, a_handler, false); }
	bool WatchPost(UE::UClass* a_class, Handler a_handler) { return WatchImpl(a_class, a_handler, true); }
}
