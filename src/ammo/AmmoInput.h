#pragma once
// The Ammo shortcut's input rules while it is open: pure, no game calls (tests/ammo_input_regression.py compiles them).
#include <cmath>
#include <cstdint>

namespace ammoinput
{
	// XInput button bits (kept here so the rules need no Windows header)
	inline constexpr std::uint16_t kDpad = 0x000F, kRB = 0x0200, kA = 0x1000, kB = 0x2000;

	enum class Action { kNone, kCancel, kEquip };

	// a press while the shortcut is open: B cancels; A, RB (the main wheel's "use", IWP-044), the shortcut's own button or
	// its keyboard key equip
	inline Action ActionFor(std::uint16_t a_pressed, std::uint16_t a_button, bool a_request)
	{
		if (a_pressed & kB) return Action::kCancel;
		if ((a_pressed & (a_button | kA | kRB)) || a_request) return Action::kEquip;
		return Action::kNone;
	}

	// the buttons the game never sees while the shortcut is open (RB would cast the readied spell, IWP-044)
	inline std::uint16_t TakenWhileOpen() { return static_cast<std::uint16_t>(kDpad | kA | kB | kRB); }

	// the entry pointed when the shortcut opens: the worn arrows, else the first arrow (owner, 2026-11-20: J / Enter always
	// equip something); -1 only for an empty wheel
	inline int StartPointed(const std::uint32_t* a_ids, int a_count, std::uint32_t a_worn)
	{
		int first = -1;
		for (int i = 0; i < a_count; ++i) {
			if (a_ids[i] && a_ids[i] == a_worn) return i;
			if (a_ids[i] && first < 0) first = i;
		}
		return first;
	}

	// mouse movement read as a stick (the game shows no cursor in gameplay, IWP-043): the movement is gathered, bounded to
	// kRadius so a turn of direction answers quickly, and aims once it is kAim long. Units: the game's mouse delta (y up).
	struct MouseAim
	{
		static constexpr double kAim = 12.0, kRadius = 40.0;
		double                  x = 0, y = 0;
		void                    Reset() { x = y = 0; }
		bool                    Feed(double a_dx, double a_dy, double& a_ox, double& a_oy)
		{
			x += a_dx;
			y += a_dy;
			const double m = std::hypot(x, y);
			if (m > kRadius) {
				x *= kRadius / m;
				y *= kRadius / m;
			}
			const double n = std::hypot(x, y);
			if (n < kAim) return false;
			a_ox = x / n;
			a_oy = y / n;
			return true;
		}
	};
}
