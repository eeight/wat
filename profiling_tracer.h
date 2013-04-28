#pragma once

#include "running_statistic.h"
#include "tracer.h"

#include <vector>

class ProfilingTracer : public Tracer{
public:
    explicit ProfilingTracer(int sampling);
    void tick(std::map<pid_t, std::vector<Frame>> stacktraces) override;

private:
    RunningStatistic statistic_;
    int sampling_;
    int iteration_;
};
