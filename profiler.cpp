#include "profiler.h"
#include "exception.h"
#include "signal_handler.h"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <set>

#include <unistd.h>

#include <iostream>

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

std::map<pid_t, StoppedWat> attachAllThreads(pid_t pid, Profiler* profiler) {
    bool tracedSomething = true;
    std::map<pid_t, StoppedWat> wats;

    while (tracedSomething) {
        tracedSomething = false;
        forallTids(pid, [&](pid_t tid) {
            if (!wats.count(tid)) {
                tracedSomething = true;
                try {
                    wats.emplace(tid, StoppedWat(pid, tid, profiler));
                } catch (const ThreadIsGone&) {
                    //std::cerr << ">>> OH, IT'S TOO FAST\n";
                    // Tough luck, moving on.
                }
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
    isStopping_(false)
{
    auto stoppedWats = attachAllThreads(pid, this);
    //std::cerr << ">>> ATTACHED " << stoppedWats.size() << " threads\n";

    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& pair: stoppedWats) {
        wats_.emplace(pair.first, std::move(pair.second).continueWat());
    }
}

Profiler::~Profiler() {
    isStopping_.store(true);
    std::unique_lock<std::mutex> lock(mutex_);
    wats_.clear();
}

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
    try {
        if (isStopping_.load()) {
            return;
        }
        StoppedWat stoppedWat(pid_, tid, this);

        std::unique_lock<std::mutex> lock(mutex_);
        //std::cerr << "new: tid=" << tid << "\n";
        //std::cerr << "size(wats): " << wats_.size() << "\n";
        wats_.emplace(tid, std::move(stoppedWat).continueWat());
    } catch (const ThreadIsGone&) {
    }
}

void Profiler::endThread(pid_t tid) {
    std::unique_lock<std::mutex> lock(mutex_);
    //std::cerr << "dead: tid=" << tid << "\n";
    //std::cerr << "size(wats): " << wats_.size() << "\n";
    zombies_.push_back(tid);
}

void Profiler::reapDead() {
    for (pid_t zombie: zombies_) {
        wats_.erase(zombie);
    }
    zombies_.clear();
}

void Profiler::doStacktraces(Tracer* tracer) {
    std::map<pid_t, std::future<std::vector<Frame>>> stacktraceFutures;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        //std::cerr << ">>> TRYTING TO OBTAIN STACKTRACES for " << wats_.size() << " threads\n";
        for (auto& kv: wats_) {
            try {
                stacktraceFutures.emplace(kv.first, kv.second.stacktrace());
            } catch (const std::exception& e) {
                tracer->addInfoLine(std::string("Exception: ") + e.what());
            }
        }
    }
    //std::cerr << ">>> OBTAINING STACKTRACES for " << stacktraceFutures.size() << " threads\n";
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
