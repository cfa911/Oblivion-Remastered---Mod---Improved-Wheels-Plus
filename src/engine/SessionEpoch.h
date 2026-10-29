#pragma once
#include <cstdint>

namespace sessionepoch
{
	struct Context
	{
		std::uintptr_t player = 0;
		std::uint32_t seed = 0;
		bool ready = false;
		bool operator==(const Context&) const = default;
	};
	struct Token
	{
		Context context{};
		std::uint64_t generation = 0;
		bool operator==(const Token&) const = default;
	};
	// Called under the adapter's mutex. Even a return to the same identity gets a new epoch.
	class Epoch
	{
	public:
		Token Observe(Context context)
		{
			if (context != current.context) { current.context = context; ++current.generation; }
			return current;
		}
		static bool Allows(Token captured, Token current)
		{
			return captured.context.ready && current.context.ready && captured == current;
		}
	private:
		Token current{};
	};
}
