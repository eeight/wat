#include "oneshot_tracer.h"
#include "symbols.h"

#include <boost/format.hpp>

#include <iostream>

void OneshotTracer::tick(std::vector<std::vector<Frame>> stacktraces) {
    for (const auto& stacktrace: stacktraces) {
        std::cout << "Thread:\n";
        for (const auto& frame: stacktrace) {
            std::cout << str(boost::format("0x%x %s\n") %
                        frame.ip %
                        abbrev(demangle(frame.procName)));
        }
    }
}
