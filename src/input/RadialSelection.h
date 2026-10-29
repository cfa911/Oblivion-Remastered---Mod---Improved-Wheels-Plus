#pragma once
#include <cmath>
#include <numbers>

// The wheel's geometry, shared by the controller, the keyboard's mouse and the ammo wheel. Keys 0-7 as the wheel lays them out:
// 45 degrees a key, key 0 at the top, clockwise. Directions are x right, y up (a stick); screen positions are y down.
namespace radialselection
{
	constexpr double kStickOn = 0.45;     // the right stick points once it is this far out
	constexpr double kStickRest = 0.25;   // and is at rest inside this

	// degrees clockwise from the top, 0 to 360
	inline double ClockwiseDegrees(double x, double y)
	{
		double degrees = std::atan2(x, y) * 180.0 / std::numbers::pi;
		if (degrees < 0) degrees += 360.0;
		return degrees;
	}

	// the key a direction points at
	inline int KeyAt(double x, double y)
	{
		return static_cast<int>((ClockwiseDegrees(x, y) + 22.5) / 45.0) % 8;
	}

	// -1 means no fresh direction: callers retain native mouse/center-rest policy.
	inline int LiveStickKey(short x, short y)
	{
		if (std::hypot(x / 32767.0, y / 32767.0) < kStickOn) return -1;
		return KeyAt(x / 32767.0, y / 32767.0);
	}

	// key a_key's unit direction
	inline void KeyDirection(int a_key, double& x, double& y)
	{
		const double angle = a_key * 45.0 * std::numbers::pi / 180.0;
		x = std::sin(angle);
		y = std::cos(angle);
	}

	// where key a_key's cell sits on screen: a wheel centred at (cx, cy), a_half its half-width, the cells a_ring of it out
	inline void CellCentre(int a_key, double cx, double cy, double a_half, double a_ring, double& x, double& y)
	{
		double dx = 0, dy = 0;
		KeyDirection(a_key, dx, dy);
		x = cx + dx * a_ring * a_half;
		y = cy - dy * a_ring * a_half;
	}
}
