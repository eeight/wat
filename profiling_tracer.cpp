#include "profiling_tracer.h"
#include "text_table.h"
#include "symbols.h"

#include <boost/format.hpp>

#include <unistd.h>

namespace {

template <class T>
std::vector<T> concat(std::vector<std::vector<T>> parts) {
    std::vector<T> result = std::move(parts.front());

    for (size_t i = 1; i != parts.size(); ++i) {
        result.insert(result.end(), parts[i].begin(), parts[i].end());
    }

    return result;
}

} // namespace

ProfilingTracer::ProfilingTracer(int sampling):
    statistic_(sampling * 10),
    sampling_(sampling),
    iteration_(0)
{}

void ProfilingTracer::tick(std::vector<std::vector<Frame>> stacktraces) {
    statistic_.pushFrames(concat(std::move(stacktraces)));
    if (++iteration_ % (sampling_ / 10) == 0) {
        std::vector<std::string> lines;
        for (const auto &kv: statistic_.topFrames(30)) {
            lines.push_back(str(boost::format(
                    "%6.2f%% 0x%x %s") %
                        (kv.first*100) %
                        kv.second.ip %
                        abbrev(demangle(kv.second.procName))));
        }
        putLines(lines);
    }
}
