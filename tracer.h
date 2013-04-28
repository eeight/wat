#pragma once

#include "frame.h"

#include <map>
#include <vector>

#include <unistd.h>

class Tracer {
public:
    virtual void tick(std::map<pid_t, std::vector<Frame>> stacktraces) = 0;
    virtual ~Tracer() {}
};
