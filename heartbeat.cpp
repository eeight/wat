#include "exception.h"
#include "heartbeat.h"

#include <stdexcept>

#include <sys/time.h>

namespace {

uint64_t now() {
    struct timeval tv;
    throwErrnoIfMinus1(gettimeofday(&tv, nullptr));
    return tv.tv_usec +
        static_cast<uint64_t>(tv.tv_sec)*1000000;
}

}

Heartbeat::Heartbeat(int freq) :
    interval_(1000000 / freq),
    nextExpectedBeat_(now()),
    skipped_(0)
{
}

void Heartbeat::beat() {
    uint64_t t = now();
    if (t < nextExpectedBeat_) {
        throw std::logic_error("Too soon");
    }
    uint64_t elapsed = t - nextExpectedBeat_;
    skipped_ = elapsed / interval_;
    if (skipped_ > 0) {
        --skipped_;
    }
    nextExpectedBeat_ += (skipped_ + 1)*interval_;
    if (nextExpectedBeat_ < t) {
        nextExpectedBeat_ += interval_;
    }
}

uint64_t Heartbeat::usecondsUntilNextBeat() {
    uint64_t t = now();
    if (t > nextExpectedBeat_) {
        return 0;
    } else {
        return nextExpectedBeat_ - t;
    }
}
