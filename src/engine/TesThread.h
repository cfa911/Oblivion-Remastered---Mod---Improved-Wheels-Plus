#pragma once

// ============================================================================================================
// The Gamebryo side's own thread. The remaster runs the TES simulation on a thread of its own, and the engine's
// equipment path checks for it: an unequip called from the UE game thread (the pad read, ProcessEvent) ends in a
// deliberate null write once GetCurrentThreadId() differs from the id the engine stored (TestBench crash record
// 2026-09-29 08:43, OblivionRemastered-Win64-Shipping.exe+0x66AA468: `cmp eax, ebx; je; inc dword [0]`). Reads are
// fine from anywhere; an equip is not (the ammo wheel, copied from Simple Loadout System).
//
// A queue drained on that thread: the 'Oblivion Main loop' (the thread's FRunnable::Run) calls one function
// unconditionally at the top of every iteration - found by byte pattern, hooked with MinHook - and the detour runs
// the queued tasks after it. (The player's vtable Process(float) was tried first: it ran once per session, not per
// frame.) OBSE64 has no task interface of its own.
// ============================================================================================================

namespace testhread
{
	using Task = std::function<void()>;

	bool Install();      // at the first frame; true when the loop call is hooked
	bool Installed();
	void Post(Task a_task);   // drops work if the character context is unavailable or changed before dispatch
	void SetPerPass(void (*a_fn)());   // called on the TES thread at the end of every pass of the main loop (keep it tiny); one function

	struct Status
	{
		bool          installed;
		std::uint32_t thread;   // the id the detour first ran on (0 until it has)
		std::uint64_t calls;
		std::size_t   queued;
	};
	Status GetStatus();
}
