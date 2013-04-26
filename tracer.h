#pragma once

#include "frame.h"
#include "running_statistic.h"

#include <string>
#include <vector>

class Tracer {
public:
    explicit Tracer(int sampling);
    void tick(std::vector<Frame> frames);

private:
    RunningStatistic statistic_;
    int sampling_;
    int iteration_;
};
