#include "wat.h"
#include "exception.h"
#include "profiler.h"
#include "signal_handler.h"
#include "symbols.h"

#include <boost/format.hpp>

#include <cassert>

#include <libunwind-ptrace.h>

#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>

namespace {

template <class T>
int ptraceCmdNoThrow(__ptrace_request request, pid_t tid, T arg) {
    return ptrace(
            request, tid, nullptr, reinterpret_cast<void *>(arg));
}

template <class T>
void ptraceCmd(__ptrace_request request, pid_t tid, T arg) {
    throwErrnoIfMinus1(ptraceCmdNoThrow(request, tid, arg));
}

void assertStopped(int status) {
    assert(WIFSTOPPED(status));
    assert(WSTOPSIG(status) == SIGSTOP);
}

} // namespace

Wat::Wat(pid_t pid, pid_t tid, Profiler* profiler) :
        pid_(pid),
        tid_(tid),
        profiler_(profiler),
        addressSpace_(throwUnwindIf0(
                unw_create_addr_space(&_UPT_accessors, 0))),
        unwindInfo_(throwUnwindIf0(_UPT_create(tid))),
        thread_([=] { tracer(); }),
        isAlive_(true),
        isStacktracePending_(false)
{
    ready_.get_future().wait();
}

Wat::~Wat() {
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (isAlive_) {
            throwErrnoIfMinus1(
                    pthread_kill(thread_.native_handle(), SIGTERM));
        }
    }

    thread_.join();

    unw_destroy_addr_space(addressSpace_);
    if (unwindInfo_) {
        _UPT_destroy(unwindInfo_);
    }
}

std::future<std::vector<Frame>> Wat::stacktrace() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!isAlive_) {
        throw std::runtime_error(
            "Cannot obtain stacktrace: thread has terminated already");
    }
    assert(!isStacktracePending_);
    isStacktracePending_ = true;
    stackPromise_ = std::promise<std::vector<Frame>>();
    throwErrnoIfMinus1(syscall(SYS_tgkill, pid_, tid_, SIGSTOP));
    return stackPromise_.get_future();
}

void Wat::tracer() {
    const int ptraceRet = ptraceCmdNoThrow(PTRACE_ATTACH, tid_, 0);
    if (ptraceRet == -1) {
        if (errno == ESRCH) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (isStacktracePending_) {
                std::make_exception_ptr(std::runtime_error(
                        "Cannot attach thread: it has terminated already"));
            }
            isAlive_ = false;
            return;
        } else {
            throwErrno();
        }
    }

    int status;
    throwErrnoIfMinus1(waitpid(tid_, &status, __WALL));
    assertStopped(status);
    ptraceCmd(PTRACE_SETOPTIONS, tid_, PTRACE_O_TRACECLONE);
    ptraceCmd(PTRACE_CONT, tid_, 0);
    handleSignals({SIGTERM}, {SIGINT});
    ready_.set_value();
    for (;;) {
        int status;
        if (lastSignal() == SIGTERM) {
            std::unique_lock<std::mutex> lock(mutex_);
            isAlive_ = false;
            break;
        }
        resetLastSignal();
        pid_t tid = waitpid(tid_, &status, __WALL);
        if (tid == tid_) {
            if (!onTraceeStatusChanged(status)) {
                break;
            }
            continue;
        }
        if (errno != EINTR) {
            throwErrnoIfMinus1(tid);
        }
        if (!lastSignal()) {
            continue;
        }
        if (lastSignal() == SIGTERM) {
            break;
        } else {
            throw std::logic_error(str(boost::format(
                    "Unexpected signal received: tid=%d, signal=%d (%s)") %
                            tid_ % lastSignal()% strsignal(lastSignal())));
        }
    }
}

bool Wat::onTraceeStatusChanged(int status) {
    if (WIFEXITED(status)) {
        profiler_->endThread(tid_);
        return false;
    } else if (WIFSIGNALED(status)) {
        profiler_->endThread(tid_);
        stackPromise_.set_exception(
                std::make_exception_ptr(std::runtime_error(
                        str(boost::format(
                            "Process is killed by deadly signal %d") %
                            WTERMSIG(status)))));
        return false;
    } else if (WIFSTOPPED(status)) {
        int deliveredSignal = WSTOPSIG(status);
        switch (deliveredSignal) {
            // group-stop
            case SIGSTOP:
            case SIGTSTP:
            case SIGTTOU:
            case SIGTTIN:
                stackPromise_.set_value(stacktraceImpl());
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    isStacktracePending_ = false;
                }
                deliveredSignal = 0;
            break;
            case SIGTRAP:
                if (status >> 16 == PTRACE_EVENT_CLONE) {
                    deliveredSignal = 0;
                    long newTid;
                    ptraceCmd(PTRACE_GETEVENTMSG, tid_, &newTid);
                    int status;
                    throwErrnoIfMinus1(waitpid(newTid, &status, __WALL));
                    assert(WIFSTOPPED(status));
                    ptraceCmd(PTRACE_DETACH, newTid, 0);
                    profiler_->newThread(newTid);
                }
            break;
        }
        ptraceCmd(PTRACE_CONT, tid_, deliveredSignal);
    } else {
        throw std::logic_error(str(boost::format(
                "Unknown status reported: tid=%d, status=%d") %
                        tid_ % status));
    }
    return true;
}

std::vector<Frame> Wat::stacktraceImpl() {
    std::vector<Frame> stacktrace;

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
        stacktrace.push_back({ip, sp, procName});

        if (++depth == 200) {
            break;
        }
    } while (unw_step(&cursor) > 0);

    return stacktrace;
}
