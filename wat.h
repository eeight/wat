#pragma once

#include "frame.h"

#include <future>
#include <memory>
#include <vector>

#include <libunwind.h>
#include <unistd.h>

class Profiler;
class Wat;
class StoppedWat;

class WatTracer {
public:
    ~WatTracer();

private:
    WatTracer(pid_t pid, pid_t tid, Profiler* profiler);
    std::future<std::vector<Frame>> stacktrace();

    friend class Wat;
    friend class StoppedWat;

    void tracer();
    bool onTraceeStatusChanged(int status);
    std::vector<Frame> stacktraceImpl();

    pid_t pid_;
    pid_t tid_;
    Profiler* profiler_;
    std::unique_ptr<
        struct unw_addr_space,
        void (*)(unw_addr_space_t)> addressSpace_;
    std::unique_ptr<
        void,
        void (*)(void *)> unwindInfo_;
    std::promise<std::vector<Frame>> stackPromise_;
    std::promise<void> ready_;
    std::promise<void> goodToGo_;

    std::mutex mutex_;
    bool isAlive_;
    bool isStacktracePending_;
    bool doDetach_;

    std::thread thread_;
};

class Wat {
public:
    explicit Wat(std::unique_ptr<WatTracer> tracer);

    std::future<std::vector<Frame>> stacktrace() {
        return tracer_->stacktrace();
    }

    Wat(Wat&&) = default;
    Wat& operator=(Wat&&) = default;

private:
    std::unique_ptr<WatTracer> tracer_;
};

class StoppedWat {
public:
    StoppedWat(pid_t pid, pid_t tid, Profiler* profiler);

    Wat continueWat() && { return Wat(std::move(tracer_)); }

    StoppedWat(StoppedWat&&) = default;
    StoppedWat& operator=(StoppedWat&&) = default;

private:
    std::unique_ptr<WatTracer> tracer_;
};
