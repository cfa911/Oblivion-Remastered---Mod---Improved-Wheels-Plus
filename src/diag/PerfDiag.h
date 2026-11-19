#pragma once
// TEMP-DIAG: per-second runtime cost of the plugin's hot paths (owner FPS report, 2026-11-19).
// Sums only; one warn line per second from Flush(). Remove before packaging.
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace perfdiag
{
	enum Slot { kChained, kFrameCb, kKeyboard, kRewrite, kDetour, kInstances, kMessageUp, kWheelScan, kCount };

	inline std::atomic<std::uint64_t> g_ticks[kCount]{};
	inline std::atomic<std::uint32_t> g_calls[kCount]{};

	inline std::int64_t Now()
	{
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		return t.QuadPart;
	}

	inline void Add(Slot a_slot, std::int64_t a_ticks)
	{
		g_ticks[a_slot].fetch_add(static_cast<std::uint64_t>(a_ticks), std::memory_order_relaxed);
		g_calls[a_slot].fetch_add(1, std::memory_order_relaxed);
	}

	struct Scope
	{
		Slot         slot;
		std::int64_t start;
		explicit Scope(Slot a_slot) : slot(a_slot), start(Now()) {}
		~Scope() { Add(slot, Now() - start); }
	};
}
