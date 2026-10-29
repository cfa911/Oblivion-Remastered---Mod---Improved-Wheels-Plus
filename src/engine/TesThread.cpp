#include "engine/TesThread.h"

#include "engine/Scan.h"
#include "engine/SessionContext.h"

#include <MinHook.h>

namespace testhread
{
	namespace
	{
		// The 'Oblivion Main loop' (the TES thread's FRunnable::Run, disassembled 2026-09-29) calls one function
		// unconditionally at the top of every iteration, right after its message pump:
		//   ff 15 ?? ?? ?? ??          call [rip+X]
		//   49 3b c6                   cmp rax, r14
		//   74 07                      je +7
		//   48 8b 05 ?? ?? ?? ??       mov rax, [rip+Y]
		//   48 8b 0d ?? ?? ?? ??       mov rcx, [rip+Z]
		//   e8 ?? ?? ?? ??             call PerIteration        <- hooked
		//   80 3d ?? ?? ?? ?? 00       cmp byte [rip+W], 0
		//   74 0a                      je +10
		// The site is found by that byte pattern, so a game update moves nothing but the addresses.
		constexpr const char* kPattern = "49 3B C6 74 07 48 8B 05 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 74 0A";
		constexpr std::size_t kCallOffset = 19;   // the E8 within the pattern

		using PerIteration_t = std::int64_t (*)(void*);

		PerIteration_t             g_original = nullptr;
		void*                      g_target = nullptr;
		std::mutex                 g_lock;
		std::deque<Task>           g_tasks;
		std::atomic<std::uint32_t> g_thread{ 0 };
		std::atomic<std::uint64_t> g_calls{ 0 };
		std::atomic<void (*)()>    g_perPass{ nullptr };

		std::int64_t Detour(void* a_this)
		{
			const auto r = g_original(a_this);
			if (g_calls.fetch_add(1) == 0) {
				g_thread = GetCurrentThreadId();
				logger::info("tes thread: the main loop's per-iteration call runs on thread {} - the ammo wheel's equips run here", g_thread.load());
			}
			(void)sessioncontext::Current();   // observe load/unavailable intervals even with no tasks
			std::deque<Task> run;
			{
				std::scoped_lock l(g_lock);
				run.swap(g_tasks);
			}
			for (auto& t : run) {
				t();
			}
			if (const auto perPass = g_perPass.load(std::memory_order_acquire)) {
				perPass();
			}
			return r;
		}

	}

	bool Install()
	{
		if (g_target) {
			return true;
		}
		std::uint8_t* site = scan::First(kPattern);
		if (!site) {
			logger::error("tes thread: the main loop's call site was not found in this game build - the ammo wheel cannot equip");
			return false;
		}
		std::uint8_t* call = site + kCallOffset;
		const auto rel = *reinterpret_cast<std::int32_t*>(call + 1);
		void* target = call + 5 + rel;
		const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
		if (const MH_STATUS mh = MH_Initialize(); mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
			logger::error("tes thread: MinHook would not initialise ({})", static_cast<int>(mh));
			return false;
		}
		void* original = nullptr;
		if (const MH_STATUS mh = MH_CreateHook(target, reinterpret_cast<void*>(&Detour), &original); mh != MH_OK) {
			logger::error("tes thread: MinHook refused the main loop's call (create {})", static_cast<int>(mh));
			return false;
		}
		g_original = reinterpret_cast<PerIteration_t>(original);
		if (const MH_STATUS mh = MH_EnableHook(target); mh != MH_OK) {
			logger::error("tes thread: MinHook refused the main loop's call (enable {})", static_cast<int>(mh));
			MH_RemoveHook(target);
			g_original = nullptr;
			return false;
		}
		g_target = target;
		logger::info("tes thread: the main loop's per-iteration call (game+0x{:X}, site game+0x{:X}) now drains the equip queue",
			reinterpret_cast<std::uintptr_t>(target) - base, reinterpret_cast<std::uintptr_t>(site) - base);
		return true;
	}

	bool Installed() { return g_target != nullptr; }

	void SetPerPass(void (*a_fn)()) { g_perPass.store(a_fn, std::memory_order_release); }

	void Post(Task a_task)
	{
		const auto captured = sessioncontext::Current();
		if (!captured.context.ready) return;
		std::scoped_lock l(g_lock);
		g_tasks.push_back([captured, task = std::move(a_task)] {
			if (sessionepoch::Epoch::Allows(captured, sessioncontext::Current())) task();
		});
	}

	Status GetStatus()
	{
		std::scoped_lock l(g_lock);
		return { g_target != nullptr, g_thread.load(), g_calls.load(), g_tasks.size() };
	}
}
