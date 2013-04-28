#include "heartbeat.h"
#include "oneshot_tracer.h"
#include "profiling_tracer.h"
#include "profiler.h"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <iostream>
#include <stdexcept>

int main(int argc, const char *argv[])
{
    try {
        if (argc != 2 && argc != 3) {
            std::cerr << boost::format("Usage: %s pid {-1}\n") %
                boost::filesystem::basename(argv[0]);
            return 1;
        }
        int pid = boost::lexical_cast<int>(argv[1]);
        if (argc == 3 && argv[2] == std::string("-1")) {
            OneshotTracer tracer;
            Profiler(pid).eventLoop(&tracer, nullptr);
        } else {
            const int SAMPLING = 200;
            ProfilingTracer tracer(SAMPLING);
            Heartbeat heartbeat(SAMPLING);
            Profiler(pid).eventLoop(&tracer, &heartbeat);
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
