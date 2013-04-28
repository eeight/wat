#pragma once

#include "running_statistic.h"
#include "tracer.h"

#include <string>
#include <vector>

class ProfilingTracer : public Tracer{
public:
    explicit ProfilingTracer(int sampling);
    void tick(std::map<pid_t, std::vector<Frame>> stacktraces) override;
    void addInfoLine(const std::string& info) override;

private:
    RunningStatistic statistic_;
    std::vector<std::string> infoLines_;
    int sampling_;
    int iteration_;
};
