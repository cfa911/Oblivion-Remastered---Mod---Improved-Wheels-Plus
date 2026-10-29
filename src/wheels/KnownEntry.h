#pragma once
#include <cstddef>

namespace knownentry
{
    // Search at most once around a saved slot; ownership is supplied by the live caller.
    template <class Entries, class Known>
    int Find(const Entries& entries, int from, int dir, bool includeFrom, Known known)
    {
        const int n = static_cast<int>(entries.size());
        if (!n) return -1;
        const bool valid = from >= 0 && from < n;
        int at = valid ? from : (dir < 0 ? 0 : n - 1);
        for (int i = 0; i < n; ++i) {
            if (i || !includeFrom || !valid) at = (at + (dir < 0 ? n - 1 : 1)) % n;
            if (known(entries[static_cast<std::size_t>(at)])) return at;
        }
        return -1;
    }
}
