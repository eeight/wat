#pragma once

#include "running_statistic.h"
#include "tracer.h"

#include <map>
#include <string>
#include <vector>

class ProfilingTracer : public Tracer{
public:
    explicit ProfilingTracer(int sampling);
    void tick(std::map<pid_t, std::vector<Frame>> stacktraces) override;
    void addInfoLine(const std::string& info) override;

private:
    RunningStatistic statistic_;
    std::map<std::string, size_t> infoLines_;
    int sampling_;
    int iteration_;
};
