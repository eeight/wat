#include "profiler.h"
#include "exception.h"
#include "signal_handler.h"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <set>

#include <unistd.h>

using boost::filesystem::directory_iterator;

namespace {

template <class F>
void forallTids(pid_t pid, F f) {
    for (directory_iterator i(str(
                    boost::format("/proc/%d/task") % pid)), i_end;
            i != i_end; ++i) {
        if (!is_directory(i->status())) {
            continue;
        }
        f(boost::lexical_cast<pid_t>(i->path().filename().string()));
    }
}

std::map<pid_t, std::unique_ptr<Wat>> attachAllThreads(
        Profiler* profiler, pid_t pid) {
    bool tracedSomething = true;
    std::map<pid_t, std::unique_ptr<Wat>> wats;

    while (tracedSomething) {
        tracedSomething = false;
        forallTids(pid, [&](pid_t tid) {
            if (!wats.count(tid)) {
                tracedSomething = true;
                wats.emplace(
                    tid, std::unique_ptr<Wat>(new Wat(pid, tid, profiler)));
            }
        });
    }

    return wats;
}

template <class Container>
std::set<typename Container::key_type> keys(const Container& container) {
    std::set<typename Container::key_type> keys;

    for (const auto& kv: container) {
        keys.insert(kv.first);
    }

    return keys;
}

} // namespace

Profiler::Profiler(pid_t pid) :
    pid_(pid),
    wats_(attachAllThreads(this, pid))
{}

void Profiler::eventLoop(Tracer* tracer, Heartbeat* heartbeat) {
    doStacktraces(tracer);
    if (!heartbeat) {
        return;
    }
    handleSignals({SIGINT}, {});
    for (;;) {
        reapDead();
        heartbeat->beat();
        if (heartbeat->skippedBeats() > 0) {
            tracer->addInfoLine(str(boost::format(
                "Too slow, skipping %d beats...") %
                    heartbeat->skippedBeats()));
        }
        if (lastSignal() == SIGINT) {
            break;
        }
        while (heartbeat->usecondsUntilNextBeat()) {
            resetLastSignal();
            if (usleep(std::max(1ul, heartbeat->usecondsUntilNextBeat())) < 0) {
                if (lastSignal() == SIGINT) {
                    return;
                }
            }
        }
        doStacktraces(tracer);
    }
}

void Profiler::newThread(pid_t tid) {
    std::unique_lock<std::mutex> lock(mutex_);
    wats_.emplace(tid, std::unique_ptr<Wat>(new Wat(pid_, tid, this)));
}

void Profiler::endThread(pid_t tid) {
    std::unique_lock<std::mutex> lock(mutex_);
    zombies_.push_back(tid);
}

void Profiler::reapDead() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (pid_t zombie: zombies_) {
        wats_.erase(zombie);
    }
    zombies_.clear();
}

void Profiler::doStacktraces(Tracer* tracer) {
    std::map<pid_t, std::future<std::vector<Frame>>> stacktraceFutures;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (auto& kv: wats_) {
            stacktraceFutures.emplace(kv.first, kv.second->stacktrace());
        }
    }
    std::map<pid_t, std::vector<Frame>> stacktraces;
    for (auto& kv: stacktraceFutures) {
        try {
            stacktraces.emplace(kv.first, kv.second.get());
        } catch (const std::exception& e) {
            tracer->addInfoLine(std::string("Exception: ") + e.what());
        }
    }

    tracer->tick(std::move(stacktraces));
}
