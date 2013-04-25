#include "exception.h"
#include "heartbeat.h"
#include "scope.h"
#include "symbols.h"
#include "text_table.h"

#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libunwind.h>
#include <libunwind-ptrace.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

struct Frame {
    unw_word_t ip;
    unw_word_t sp;
    std::string procName;

    bool operator <(const Frame& other) const {
        return ip < other.ip;
    }
};

class Wat {
public:
    explicit Wat(pid_t pid) :
            pid_(pid),
            addressSpace_(throwUnwindIf0(
                    unw_create_addr_space(&_UPT_accessors, 0))),
            unwindInfo_(throwUnwindIf0(_UPT_create(pid)))
    {
        throwErrnoIfMinus1(ptrace(PTRACE_ATTACH, pid, nullptr, nullptr));
        requireTraceeStopped();
        throwErrnoIfMinus1(ptrace(PTRACE_CONT, pid, nullptr, nullptr));
    }

    ~Wat() {
        unw_destroy_addr_space(addressSpace_);
        _UPT_destroy(unwindInfo_);
        // Cannot do anything if error occurs in dtor.
        ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
    }

    void detach() {
        throwErrnoIfMinus1(ptrace(PTRACE_DETACH, pid_, nullptr, nullptr));
    }

    std::vector<Frame> stacktrace() {
        std::vector<Frame> frames;
        throwErrnoIfMinus1(kill(pid_, SIGSTOP));
        requireTraceeStopped();

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

        throwErrnoIfMinus1(ptrace(PTRACE_CONT, pid_, nullptr, nullptr));
        return frames;
    }

private:
    void requireTraceeStopped() {
        int status;
        throwErrnoIfMinus1(waitpid(pid_, &status, WUNTRACED | WCONTINUED));
        if (WIFEXITED(status)) {
            if (WTERMSIG(status)) {
                throw std::runtime_error(str(boost::format(
                            "Could not attach: process (pid = %d) "
                            "is killed by signal %d.") %
                        pid_ % WTERMSIG(status)));
            }
            throw std::runtime_error(str(boost::format(
                        "Could not attach: process (pid = %d) "
                        "has terminated normally") % pid_));
        } else if (!WIFSTOPPED(status)) {
            throw std::logic_error(str(boost::format(
                        "Process (pid = %d) has neither been killed "
                        "nor stopped. Status = %d") % pid_ % status));
        }
    }

    pid_t pid_;
    unw_addr_space_t addressSpace_;
    void *unwindInfo_;
};

class RunningStatistic {
public:
    explicit RunningStatistic(size_t width):
            width_(width)
    {}

    void pushFrames(std::vector<Frame> frames) {
        if (framesSeqence_.size() == width_) {
            for (const auto& frame: framesSeqence_.front()) {
                auto iter = counts_.find(frame);
                if (!--iter->second) {
                    counts_.erase(iter);
                }
            }
            framesSeqence_.pop_front();
        }
        for (const auto& frame: frames) {
            ++counts_[frame];
        }
        framesSeqence_.push_back(std::move(frames));
    }

    std::vector<std::pair<float, Frame>> topFrames(size_t count) {
        std::vector<std::pair<float, Frame>> topFrames;
        for (const auto &kv: counts_) {
            topFrames.emplace_back(
                    kv.second/static_cast<float>(width_), kv.first);
        }
        std::sort(topFrames.rbegin(), topFrames.rend());
        if (topFrames.size() > count) {
            topFrames.erase(topFrames.begin() + count, topFrames.end());
        }
        return topFrames;
    }

private:
    std::list<std::vector<Frame>> framesSeqence_;
    std::map<Frame, int> counts_;
    size_t width_;
};

bool g_interrupted = false;

void onInterrupt(int) {
    g_interrupted = true;
}

void setSigintHandler() {
    struct sigaction action = {0};
    action.sa_handler = &onInterrupt;
    throwErrnoIfMinus1(sigaction(SIGINT, &action, nullptr));
}

void liveProfile(int pid) {
    setSigintHandler();

    Wat wat(pid);
    Heartbeat hb(100);
    RunningStatistic stat(1000);
    int iter = 0;

    while (!g_interrupted) {
        hb.beat();
        if (hb.skippedBeats() > 0) {
            std::cerr << "Too slow, skipping " << hb.skippedBeats() <<
                    " beats...\n";
        }
        stat.pushFrames(wat.stacktrace());
        usleep(hb.usecondsUntilNextBeat());
        if (++iter % 10 == 0) {
            std::vector<std::string> lines;
            for (const auto &kv: stat.topFrames(30)) {
                lines.push_back(str(boost::format(
                        "%6.2f%% 0x%x %s") %
                            (kv.first*100) %
                            kv.second.ip %
                            abbrev(demangle(kv.second.procName))));
            }
            putLines(lines);
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
            stacktrace(pid);
        } else {
            liveProfile(pid);
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
