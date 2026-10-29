#pragma once
#include <cmath>

namespace slowtime
{
	class CloseGate
	{
	public:
		void Close(unsigned count) { closing = true; openedAtClose = count; }
		void Reopen() { closing = false; }
		bool Allows(bool open, unsigned count)
		{
			if (!open || count != openedAtClose) closing = false;
			return open && !closing;
		}
	private:
		bool closing = false;
		unsigned openedAtClose = 0;
	};

	// Own one write for one continuous wheel session. An external writer wins.
	class Lease
	{
	public:
		static bool Near(float a, float b) { return std::isfinite(a) && std::abs(a - b) < 0.0001f; }
		template <class Write>
		void Update(bool wanted, float current, float multiplier, Write write)
		{
			if (owned && !Near(current, applied)) { owned = false; blocked = true; }
			if (!wanted) {
				if (owned && !write(original)) return; // retry a failed restore on the next live frame
				owned = blocked = false;
				return;
			}
			if (owned || blocked) return;
			blocked = true;
			if (!Near(current, 1.0f) || !std::isfinite(multiplier) || multiplier < 0.01f || multiplier >= 1.0f) return;
			original = current;
			applied = multiplier;
			owned = write(applied);
		}
		void Forget() { owned = false; blocked = true; }
	private:
		bool owned = false, blocked = false;
		float original = 1, applied = 1;
	};
}
