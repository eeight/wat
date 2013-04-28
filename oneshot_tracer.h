#pragma once

#include "tracer.h"

class OneshotTracer : public Tracer {
public:
    void tick(std::map<pid_t, std::vector<Frame>> stacktraces) override;
};
