#include "running_statistic.h"

#include <algorithm>

RunningStatistic::RunningStatistic(size_t width):
    width_(width)
{}

void RunningStatistic::pushFrames(std::vector<Frame> frames) {
    if (framesSeqence_.size() == width_) {
        for (const auto& frame: framesSeqence_.front()) {
            auto iter = counts_.find(frame);
            if (!--iter->second) {
                counts_.erase(iter);
            }
        }
        framesSeqence_.pop_front();
    }
    for (const auto& frame: frames) {
        ++counts_[frame];
    }
    framesSeqence_.push_back(std::move(frames));
}

std::vector<std::pair<float, Frame>> RunningStatistic::topFrames(size_t count) {
    std::vector<std::pair<float, Frame>> topFrames;
    for (const auto &kv: counts_) {
        topFrames.emplace_back(
                kv.second/static_cast<float>(width_), kv.first);
    }
    std::sort(topFrames.rbegin(), topFrames.rend());
    if (topFrames.size() > count) {
        topFrames.erase(topFrames.begin() + count, topFrames.end());
    }
    return topFrames;
}
