#pragma once

#include "tracer.h"

class OneshotTracer : public Tracer {
public:
    void tick(std::vector<std::vector<Frame>> stacktraces) override;
};
