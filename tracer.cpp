#include "tracer.h"
#include "text_table.h"
#include "symbols.h"

#include <boost/format.hpp>

#include <unistd.h>

Tracer::Tracer(int sampling):
    statistic_(sampling * 10),
    sampling_(sampling),
    iteration_(0)
{}

void Tracer::tick(std::vector<Frame> frames) {
    statistic_.pushFrames(std::move(frames));
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
