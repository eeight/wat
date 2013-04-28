#pragma once

#include "frame.h"

#include <vector>

class Tracer {
public:
    virtual void tick(std::vector<std::vector<Frame>> stacktraces) = 0;
    virtual ~Tracer() {}
};
