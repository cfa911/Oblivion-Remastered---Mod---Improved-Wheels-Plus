#pragma once
#include <cstdint>

namespace messagediscovery
{
	// Only widget discovery is throttled. Known widgets are always checked live;
	// input actions force discovery so a newly created dialog cannot consume a wheel action.
	class Gate
	{
	public:
		bool Due(std::uint64_t now, bool immediate)
		{
			if (!immediate && now < next_) return false;
			next_ = now + 100;
			return true;
		}
	private:
		std::uint64_t next_ = 0;
	};

	// A dialog only matters to input that is actually being pressed: with nothing pressed the guarded block does nothing, so
	// the message is not asked about (that ask is an object-array scan whenever no known dialog is up).
	template <class MessageUp>
	bool InputAllowed(bool a_anyGuardedInputDown, MessageUp&& a_messageUp)
	{
		return a_anyGuardedInputDown && !a_messageUp();
	}
}
