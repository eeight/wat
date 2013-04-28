#pragma once

#include "heartbeat.h"
#include "signal_iterator.h"
#include "tracer.h"
#include "wat.h"

#include <map>
#include <set>
#include <vector>

#include <unistd.h>

class Profiler {
    enum class Status {
        RUNNING,
        STOPPING
    };

public:
    explicit Profiler(pid_t pid);

    void eventLoop(Tracer* tracer, Heartbeat* heartbeat);

private:

    bool handleSigchld(Tracer* tracer, Heartbeat* heartbeat);
    void handleSigalrm();
    bool handleAllTraced(pid_t tid, Tracer* tracer, Heartbeat* heartbeat);

    pid_t pid_;
    SignalIterator signalIterator_;
    std::map<pid_t, Wat> wats_;
    Status status_;
    std::set<pid_t> toTrace_;
    std::set<pid_t> stalled_;
    std::map<pid_t, std::vector<Frame>> stacktraces_;
};

