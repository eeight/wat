#pragma once

#include "heartbeat.h"
#include "tracer.h"
#include "wat.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <unistd.h>

class Profiler {
public:
    explicit Profiler(pid_t pid);

    void eventLoop(Tracer* tracer, Heartbeat* heartbeat);

private:
    friend class Wat;
    void newThread(pid_t tid);
    void endThread(pid_t tid);

    void doStacktraces(Tracer* tracer);
    void reapDead();

    pid_t pid_;
    std::map<pid_t, std::unique_ptr<Wat>> wats_;
    std::vector<pid_t> zombies_;
    std::mutex mutex_;
};

