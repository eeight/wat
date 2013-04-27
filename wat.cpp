#include "exception.h"
#include "heartbeat.h"
#include "scope.h"
#include "symbols.h"
#include "tracer.h"

#include <signal.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libunwind.h>
#include <libunwind-ptrace.h>

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

class Wat {
public:
    explicit Wat(pid_t pid) :
            pid_(pid),
            addressSpace_(throwUnwindIf0(
                    unw_create_addr_space(&_UPT_accessors, 0))),
            unwindInfo_(throwUnwindIf0(_UPT_create(pid)))
    {
    }

    ~Wat() {
        unw_destroy_addr_space(addressSpace_);
        if (unwindInfo_) {
            _UPT_destroy(unwindInfo_);
        }
    }

    Wat(const Wat&) = delete;
    void operator =(const Wat&) = delete;

    Wat(Wat&& other) :
        pid_(other.pid_),
        addressSpace_(other.addressSpace_),
        unwindInfo_(other.unwindInfo_)
    {
        other.addressSpace_ = nullptr;
        other.unwindInfo_ = nullptr;
    }

    Wat& operator =(Wat&& other) {
        if (this != &other) {
            this->~Wat();
            addressSpace_ = other.addressSpace_;
            unwindInfo_ = other.unwindInfo_;
            other.addressSpace_ = nullptr;
            other.unwindInfo_ = nullptr;
        }
        return *this;
    }

    std::vector<Frame> stacktrace() {
        std::vector<Frame> frames;

        unw_cursor_t cursor;
        throwUnwindIfLessThan0(unw_init_remote(
                    &cursor, addressSpace_, unwindInfo_));
        int depth = 0;
        do {
            unw_word_t ip;
            unw_word_t sp;

            throwUnwindIfLessThan0(unw_get_reg(&cursor, UNW_REG_IP, &ip));
            throwUnwindIfLessThan0(unw_get_reg(&cursor, UNW_REG_SP, &sp));

            std::string procName = getProcName(&cursor);
            frames.push_back({ip, sp, procName});

            if (++depth == 200) {
                break;
            }
        } while (unw_step(&cursor) > 0);

        return frames;
    }

private:
    pid_t pid_;
    unw_addr_space_t addressSpace_;
    void *unwindInfo_;
};

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
                throwErrnoIfMinus1(
                        ptrace(PTRACE_ATTACH, tid, nullptr, nullptr));
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

enum class Loop: bool { NO, YES };

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

void profile(int pid, Loop loop) {
    static const int SAMPLING = 100;

    SignalIterator signalIterator;
    Tracer tracer(SAMPLING);
    Heartbeat hb(SAMPLING);

    auto wats = attachAllThreads(pid);

    enum Status {
        STATUS_RUNNING,
        STATUS_STOPPING
    };
    throwErrnoIfMinus1(ualarm(0, 0));
    int status = STATUS_STOPPING;
    std::set<pid_t> toTrace = keys(wats);
    std::set<pid_t> stalled;
    std::vector<Frame> frames;

    for(;;) {
        const int signal = signalIterator.next();
        if (signal == SIGCHLD) {
            int tidStatus;
            // SIGCHLD is not real-time. This means that when we get a SIGCHLD,
            // multiple events from children could be pending.
            while (pid_t tid = throwErrnoIfMinus1(waitpid(
                            -1,
                            &tidStatus,
                            WNOHANG | WCONTINUED | __WALL))) {
                assert(tid != 0);
                // TODO: set this option only once
                throwErrnoIfMinus1(ptrace(
                    PTRACE_SETOPTIONS,
                    tid,
                    nullptr,
                    static_cast<long>(PTRACE_O_TRACECLONE)));
                if (WIFEXITED(tidStatus)) {
                    wats.erase(tid);
                    stalled.erase(tid);
                } else if (WIFSIGNALED(tidStatus)) {
                    // Killed by a deadly signal. Nothing to do here anymore.
                    exit(0);
                } else if (WIFSTOPPED(tidStatus)) {
                    switch (WSTOPSIG(tidStatus)) {
                        // group-stop
                        case SIGSTOP:
                        case SIGTSTP:
                        case SIGTTOU:
                        case SIGTTIN:
                            if (status != STATUS_STOPPING) {
                                // Not our work.
                                stalled.insert(tid);
                                frames.clear();
                            } else if (stalled.empty()) {
                                assert(wats.count(tid));
                                auto stacktrace =
                                        wats.find(tid)->second.stacktrace();
                                frames.insert(
                                        frames.end(),
                                        stacktrace.begin(),
                                        stacktrace.end());
                                toTrace.erase(tid);
                                if (toTrace.empty()) {
                                    hb.beat();
                                    if (hb.skippedBeats() > 0) {
                                        std::cerr <<
                                            "Too slow, skipping " <<
                                            hb.skippedBeats() <<
                                            " beats...\n";
                                    }
                                    assert(wats.count(tid));
                                    tracer.tick(wats.find(tid)->second.stacktrace());
                                    ualarm(hb.usecondsUntilNextBeat(), 0);
                                    status = STATUS_RUNNING;
                                    if (loop == Loop::NO) {
                                        return;
                                    }
                                }
                                throwErrnoIfMinus1(ptrace(PTRACE_CONT, tid, nullptr, nullptr));
                            }
                        break;
                        case SIGTRAP:
                            if (status >> 16 == PTRACE_EVENT_CLONE) {
                                long newTid;
                                throwErrnoIfMinus1(ptrace(
                                        PTRACE_GETEVENTMSG, tid, nullptr, &newTid));
                                wats.emplace(newTid, Wat(newTid));
                            }
                            throwErrnoIfMinus1(ptrace(PTRACE_CONT, tid, nullptr, nullptr));
                        break;
                        default:
                            throwErrnoIfMinus1(ptrace(
                                    PTRACE_CONT,
                                    tid,
                                    nullptr,
                                    static_cast<long>(WSTOPSIG(tidStatus))));
                    }
                } else if (WIFCONTINUED(tidStatus)) {
                    stalled.erase(tid);
                } else {
                    throw std::logic_error(str(boost::format(
                                    "Unknown status reported: tid=%d, status=%d") %
                                        tid % tidStatus));
                }
            }
        } else if (signal == SIGALRM) {
            toTrace = keys(wats);
            throwErrnoIfMinus1(kill(pid, SIGSTOP));
            status = STATUS_STOPPING;
        } else if (signal == SIGINT) {
            return;
        } else {
            throw std::logic_error(str(boost::format(
                        "Unexpected signal: %s") % strsignal(signal)));
        }
    }
}

void stacktrace(int pid) {
    Wat wat(pid);

    for (const auto& frame: wat.stacktrace()) {
        std::cout << str(boost::format("0x%x %s\n") %
                    frame.ip %
                    abbrev(demangle(frame.procName)));
    }
}

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
            profile(pid, Loop::NO);
        } else {
            profile(pid, Loop::YES);
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
