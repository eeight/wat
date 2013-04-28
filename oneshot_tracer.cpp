#include "oneshot_tracer.h"
#include "symbols.h"

#include <boost/format.hpp>

#include <iostream>

void OneshotTracer::tick(std::map<pid_t, std::vector<Frame>> stacktraces) {
    for (const auto& kv: stacktraces) {
        std::cout << boost::format("Thread %d:\n") % kv.first;
        for (const auto& frame: kv.second) {
            std::cout << str(boost::format("0x%x %s\n") %
                        frame.ip %
                        abbrev(demangle(frame.procName)));
        }
        std::cout << std::endl;
    }
}
