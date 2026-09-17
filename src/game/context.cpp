#include "context.h"

#include <algorithm>

namespace cr {

// a timer is due when what is left is (almost) nothing; 16.16 steps of 1/60 s leave a few raw units
#ifdef CR_FIXED
static const real kDue = real::fromRaw(32);
#else
static const real kDue = 1e-6f;
#endif

int Timers::after(real seconds, std::function<void()> fn)
{
    timers_.push_back({next_, seconds, std::move(fn)});
    return next_++;
}

void Timers::cancel(int id)
{
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [id](const Timer &t) { return t.id == id; }),
                  timers_.end());
}

void Timers::update(real dt)
{
    std::vector<std::function<void()>> due;
    for (Timer &t : timers_) {
        t.left -= dt;
        if (t.left <= kDue) due.push_back(std::move(t.fn));
    }
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const Timer &t) { return t.left <= kDue; }),
                  timers_.end());
    for (auto &fn : due) fn();
}

} // namespace cr
