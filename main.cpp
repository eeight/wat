#include "exception.h"
#include "heartbeat.h"
#include "oneshot_tracer.h"
#include "profiling_tracer.h"
#include "tracer.h"
#include "wat.h"

#include <signal.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

using boost::filesystem::directory_iterator;

template <class T>
void ptraceCmd(__ptrace_request request, pid_t pid, T arg) {
    throwErrnoIfMinus1(ptrace(
            request, pid, nullptr, reinterpret_cast<void *>(arg)));
}

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

std::map<pid_t, Wat> attachAllThreads(pid_t pid) {
    bool tracedSomething = true;
    std::map<pid_t, Wat> wats;

    while (tracedSomething) {
        tracedSomething = false;
        forallTids(pid, [&](pid_t tid) {
            if (!wats.count(tid)) {
                tracedSomething = true;
                ptraceCmd(PTRACE_ATTACH, tid, 0);
                wats.emplace(tid, Wat(tid));
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

class SignalIterator {
public:
    SignalIterator() {
        sigset_t signals;
        throwErrnoIfMinus1(sigemptyset(&signals));
        throwErrnoIfMinus1(sigaddset(&signals, SIGCHLD));
        throwErrnoIfMinus1(sigaddset(&signals, SIGALRM));
        throwErrnoIfMinus1(sigaddset(&signals, SIGINT));
        throwErrnoIfMinus1(sigprocmask(SIG_BLOCK, &signals, nullptr));
        fd_ = throwErrnoIfMinus1(signalfd(-1, &signals, 0));
    }

    ~SignalIterator() {
        while (close(fd_) == -1 && errno == EINTR);
    }

    int next() {
        struct signalfd_siginfo sigInfo;
        throwErrnoIfMinus1(read(fd_, &sigInfo, sizeof(sigInfo)));
        return sigInfo.ssi_signo;
    }

private:
    int fd_;
};

class Profiler {
    enum class Status {
        RUNNING,
        STOPPING
    };

public:
    explicit Profiler(pid_t pid) :
        pid_(pid),
        wats_(attachAllThreads(pid)),
        status_(Status::STOPPING),
        toTrace_(keys(wats_))
    {}

    void eventLoop(Tracer* tracer, Heartbeat* heartbeat) {
        for (int signal; (signal = signalIterator_.next()) != SIGINT;) {
            if (signal == SIGCHLD) {
                if (!handleSigchld(tracer, heartbeat)) {
                    return;
                }
            } else if (signal == SIGALRM) {
                handleSigalrm();
            } else {
                throw std::logic_error(str(boost::format(
                        "Unexpected signal: %s") % strsignal(signal)));
            }
        }
    }

private:

    bool handleSigchld(Tracer* tracer, Heartbeat* heartbeat) {
        int tidStatus;
        // SIGCHLD is not real-time. This means that when we get a SIGCHLD,
        // multiple events from children could be pending.
        while (pid_t tid = throwErrnoIfMinus1(waitpid(
                        -1, &tidStatus, WNOHANG | WCONTINUED | __WALL))) {
            // TODO: set this option only once
            ptraceCmd(PTRACE_SETOPTIONS, tid, PTRACE_O_TRACECLONE);
            if (WIFEXITED(tidStatus)) {
                wats_.erase(tid);
                stalled_.erase(tid);
            } else if (WIFSIGNALED(tidStatus)) {
                // Killed by a deadly signal. Nothing to do here anymore.
                return false;
            } else if (WIFSTOPPED(tidStatus)) {
                switch (WSTOPSIG(tidStatus)) {
                    // group-stop
                    case SIGSTOP:
                    case SIGTSTP:
                    case SIGTTOU:
                    case SIGTTIN:
                        if (status_ == Status::RUNNING) {
                            // Not our work.
                            stalled_.insert(tid);
                            stacktraces_.clear();
                        } else {
                            assert(status_ == Status::STOPPING);
                            if (stalled_.empty()) {
                                if (!handleAllTraced(tid, tracer, heartbeat)) {
                                    return false;
                                }
                            }
                        }
                    break;
                    case SIGTRAP:
                        if (tidStatus >> 16 == PTRACE_EVENT_CLONE) {
                            long newTid;
                            ptraceCmd(PTRACE_GETEVENTMSG, tid, &newTid);
                            wats_.emplace(newTid, Wat(newTid));
                        }
                        ptraceCmd(PTRACE_CONT, tid, 0);
                    break;
                    default:
                        ptraceCmd(PTRACE_CONT, tid, WSTOPSIG(tidStatus));
                }
            } else if (WIFCONTINUED(tidStatus)) {
                stalled_.erase(tid);
            } else {
                throw std::logic_error(str(boost::format(
                        "Unknown status reported: tid=%d, status=%d") %
                                tid % tidStatus));
            }
        }
    }

    void handleSigalrm() {
        toTrace_ = keys(wats_);
        for (pid_t tid: toTrace_) {
            throwErrnoIfMinus1(syscall(SYS_tgkill, pid_, tid, SIGSTOP));
        }
        status_ = Status::STOPPING;
    }

    bool handleAllTraced(pid_t tid, Tracer* tracer, Heartbeat* heartbeat) {
        assert(wats_.count(tid));
        stacktraces_.emplace(tid, wats_.find(tid)->second.stacktrace());
        toTrace_.erase(tid);
        if (toTrace_.empty()) {
            tracer->tick(std::move(stacktraces_));
            if (!heartbeat) {
                return false;
            }
            stacktraces_.clear();
            heartbeat->beat();
            if (heartbeat->skippedBeats() > 0) {
                tracer->addInfoLine(str(boost::format(
                    "Too slow, skipping %d beats...") %
                        heartbeat->skippedBeats()));
            }
            ualarm(std::max(1ul, heartbeat->usecondsUntilNextBeat()), 0);
            status_ = Status::RUNNING;
        }
        ptraceCmd(PTRACE_CONT, tid, 0);
        return true;
    }

    pid_t pid_;
    SignalIterator signalIterator_;
    std::map<pid_t, Wat> wats_;
    Status status_;
    std::set<pid_t> toTrace_;
    std::set<pid_t> stalled_;
    std::map<pid_t, std::vector<Frame>> stacktraces_;
};

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
