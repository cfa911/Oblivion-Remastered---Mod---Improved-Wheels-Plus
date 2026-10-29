#pragma once
#include <cstdint>
#include <mutex>

namespace emptysetup
{
    // Worker completion is ticketed; a dropped queue callback can be retried after its lease.
    class Gate
    {
    public:
        void Reset() { std::scoped_lock lock(mutex); ++ticket; complete = false; retryAt = 0; }
        bool Complete() { std::scoped_lock lock(mutex); return complete; }
        std::uint64_t Queue(std::uint64_t now)
        {
            std::scoped_lock lock(mutex);
            if (running || complete || now < retryAt) return 0;
            retryAt = now + 1000;
            return ++ticket;
        }
        template <class Clear> bool Run(std::uint64_t request, Clear clear)
        {
            {
                std::scoped_lock lock(mutex);
                if (!request || request != ticket || complete || running) return false;
                running = true;
            }
            // Native clearing may reenter UI readers. Never hold our mutex across game code.
            const bool cleared = clear();
            std::scoped_lock lock(mutex);
            running = false;
            if (request == ticket) complete = cleared;
            return request == ticket && complete;
        }
    private:
        std::mutex mutex;
        std::uint64_t ticket = 0, retryAt = 0;
        bool complete = false, running = false;
    };
}
