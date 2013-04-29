#include "profiling_tracer.h"
#include "text_table.h"
#include "symbols.h"

#include <boost/format.hpp>

#include <algorithm>
#include <set>

#include <unistd.h>

namespace {

std::vector<Frame> removeDuplicatedFunctions(std::vector<Frame> frames) {
    std::set<std::string> names;
    auto seen = [&](const Frame& frame) {
        if (names.count(frame.procName)) {
            return true;
        } else {
            names.insert(frame.procName);
            return false;
        }
    };
    frames.erase(
            std::remove_if(frames.begin(), frames.end(), seen),
            frames.end());
    return frames;
}

std::vector<Frame> concatStacktraces(
        std::map<pid_t, std::vector<Frame>> stacktraces) {
    std::vector<Frame> result;

    for (auto& kv: stacktraces) {
        if (result.empty()) {
            result = removeDuplicatedFunctions(std::move(kv.second));
        } else {
            auto stacktrace = removeDuplicatedFunctions(std::move(kv.second));
            result.insert(result.end(), stacktrace.begin(), stacktrace.end());
        }
    }

    return result;
}

} // namespace

ProfilingTracer::ProfilingTracer(int sampling):
    statistic_(sampling * 10),
    sampling_(sampling),
    iteration_(0)
{}

void ProfilingTracer::tick(std::map<pid_t, std::vector<Frame>> stacktraces) {
    statistic_.pushFrames(concatStacktraces(std::move(stacktraces)));
    if (++iteration_ % (sampling_ / 10) == 0) {
        std::vector<std::string> lines;
        for (const auto &kv: statistic_.topFrames(30)) {
            lines.push_back(str(boost::format(
                    "%6.2f%% %s") %
                        (kv.first*100) %
                        abbrev(demangle(kv.second))));
        }
        if (!infoLines_.empty()) {
            lines.push_back("");
            lines.push_back("INFO:");
            lines.insert(lines.end(), infoLines_.begin(), infoLines_.end());
            infoLines_.clear();
        }
        putLines(lines);
    }
}

void ProfilingTracer::addInfoLine(const std::string& info) {
    infoLines_.push_back(info);
}
