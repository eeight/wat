#pragma once

#include "frame.h"

#include <list>
#include <map>
#include <vector>

class RunningStatistic {
public:
    explicit RunningStatistic(size_t width);
    void pushFrames(std::vector<Frame> frames);
    std::vector<std::pair<float, Frame>> topFrames(size_t count);

private:
    std::list<std::vector<Frame>> framesSeqence_;
    std::map<Frame, int> counts_;
    size_t width_;
};
