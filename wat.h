#pragma once

#include "frame.h"

#include <future>
#include <vector>

#include <libunwind.h>
#include <unistd.h>

class Profiler;

class Wat {
public:
    Wat(pid_t pid, pid_t tid, Profiler* profiler);

    ~Wat();

    Wat(const Wat&) = delete;
    void operator =(const Wat&) = delete;
    Wat(Wat&&) = delete;
    void operator =(Wat&&) = delete;

    std::future<std::vector<Frame>> stacktrace();

private:
    void tracer();
    bool onTraceeStatusChanged(int status);
    std::vector<Frame> stacktraceImpl();

    pid_t pid_;
    pid_t tid_;
    Profiler* profiler_;
    unw_addr_space_t addressSpace_;
    void *unwindInfo_;
    std::promise<std::vector<Frame>> stackPromise_;
    std::promise<void> ready_;
    std::thread thread_;

    std::mutex mutex_;
    bool isAlive_;
    bool isStacktracePending_;
};
