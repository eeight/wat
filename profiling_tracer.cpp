#include "profiling_tracer.h"
#include "text_table.h"
#include "symbols.h"

#include <boost/format.hpp>

#include <unistd.h>

namespace {

std::vector<Frame> concatStacktraces(
        std::map<pid_t, std::vector<Frame>> stacktraces) {
    std::vector<Frame> result;

    for (auto& kv: stacktraces) {
        if (result.empty()) {
            result = std::move(kv.second);
        } else {
            result.insert(result.end(), kv.second.begin(), kv.second.end());
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
