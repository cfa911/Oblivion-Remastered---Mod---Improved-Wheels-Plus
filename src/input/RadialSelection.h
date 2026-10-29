#pragma once
#include <cmath>
#include <numbers>

namespace radialselection
{
    // -1 means no fresh direction: callers retain native mouse/center-rest policy.
    inline int LiveStickKey(short x, short y)
    {
        if (std::hypot(x / 32767.0, y / 32767.0) < 0.45) return -1;
        double degrees = std::atan2(x / 32767.0, y / 32767.0) * 180.0 / std::numbers::pi;
        if (degrees < 0) degrees += 360.0;
        return static_cast<int>((degrees + 22.5) / 45.0) % 8;
    }
}
