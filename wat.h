#pragma once

#include "frame.h"

#include <libunwind.h>
#include <unistd.h>

#include <vector>

class Wat {
public:
    explicit Wat(pid_t pid);

    ~Wat();

    Wat(const Wat&) = delete;
    void operator =(const Wat&) = delete;

    Wat(Wat&& other);
    Wat& operator =(Wat&& other);

    std::vector<Frame> stacktrace();

private:
    pid_t pid_;
    unw_addr_space_t addressSpace_;
    void *unwindInfo_;
};
