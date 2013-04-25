#pragma once

#include <cstdint>

class Heartbeat {
public:
    explicit Heartbeat(int freq);
    int skippedBeats() const { return skipped_; }
    void beat();
    uint64_t usecondsUntilNextBeat();

private:
    uint64_t interval_;
    uint64_t nextExpectedBeat_;
    int skipped_;
};
