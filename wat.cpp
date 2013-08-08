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

#include <iostream>

#define TRACE(...) std::cerr << __VA_ARGS__ << std::endl;
//#define TRACE(...)

namespace {

template <class F>
void convertThreadErrors(F f) {
    try {
        f();
    } catch (const SyscallError& e) {
        // Funny thing. According to the man page, ptrace should
        // not return EPERM ever in our case. However it does
        // from time to time.
        // Turns out, linux uses this error code to signal that
        // you are trying to trace a zombie process.
        // Great.
        if (e.error() == ESRCH || e.error() == EPERM) {
            throw ThreadIsGone();
        } else {
            throw;
        }
    }
}

template <class T>
int ptraceCmdNoThrow(__ptrace_request request, pid_t tid, T arg) {
    return ptrace(
            request, tid, nullptr, reinterpret_cast<void *>(arg));
}

template <class T>
void ptraceCmd(__ptrace_request request, pid_t tid, T arg) {
    convertThreadErrors([=] {
        throwErrnoIfMinus1(ptraceCmdNoThrow(request, tid, arg));
    });
}

void assertStopped(int status) {
    assert(WIFSTOPPED(status));
    assert(WSTOPSIG(status) == SIGSTOP);
}

} // namespace

WatTracer::WatTracer(pid_t pid, pid_t tid, Profiler* profiler) :
        pid_(pid),
        tid_(tid),
        profiler_(profiler),
        addressSpace_(
                throwUnwindIf0(unw_create_addr_space(&_UPT_accessors, 0)),
                &unw_destroy_addr_space),
        unwindInfo_(throwUnwindIf0(_UPT_create(tid_)), &_UPT_destroy),
        thread_([=] { tracer(); }),
        isAlive_(true),
        isStacktracePending_(false),
        doDetach_(false)
{
    TRACE("WAITING UNTIL tid=" << tid_ << " is ready");
    try {
        ready_.get_future().get();
    } catch (...) {
        // Prevent thread leak
        thread_.join();
        throw;
    }
    TRACE("OK tid=" << tid_ << " is ready");
}

WatTracer::~WatTracer() {
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (isAlive_) {
            throwErrnoIfMinus1(
                    pthread_kill(thread_.native_handle(), SIGTERM));
        }
    }

    thread_.join();
}

std::future<std::vector<Frame>> WatTracer::stacktrace() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!isAlive_) {
        throw std::runtime_error(
            "Cannot obtain stacktrace: thread has terminated already");
    }
    assert(!isStacktracePending_);
    isStacktracePending_ = true;
    stackPromise_ = std::promise<std::vector<Frame>>();
    TRACE("STOPPING tid=" << tid_);
    convertThreadErrors([=] {
        throwErrnoIfMinus1(syscall(SYS_tgkill, pid_, tid_, SIGSTOP));
    });
    return stackPromise_.get_future();
}

void WatTracer::tracer() {
    auto handleSigterm = [&] {
        std::unique_lock<std::mutex> lock(mutex_);
        doDetach_ = true;
        throwErrnoIfMinus1(syscall(SYS_tgkill, pid_, tid_, SIGSTOP));
        TRACE("WAITING FOR tid=" << tid_ << " to be detached");
    };
    try {
        try {
            ptraceCmd(PTRACE_ATTACH, tid_, 0);
            int status;
            throwErrnoIfMinus1(waitpid(tid_, &status, __WALL));
            if (WIFEXITED(status)) {
                throw ThreadIsGone();
            } else {
                assertStopped(status);
            }
            ptraceCmd(PTRACE_SETOPTIONS, tid_, PTRACE_O_TRACECLONE);
            TRACE(">>> ATTACHED tid=" << tid_);
        } catch (...) {
            ready_.set_exception(std::current_exception());
            std::unique_lock<std::mutex> mutex_;
            isAlive_ = false;
            TRACE(">>> NOT ATTACHED tid=" << tid_);
            return;
        }

        ready_.set_value();

        goodToGo_.get_future().get();

        ptraceCmd(PTRACE_CONT, tid_, 0);
        handleSignals({SIGTERM}, {SIGINT});

        for (;;) {
            if (lastSignal() == SIGTERM) {
                handleSigterm();
            }
            resetLastSignal();

            int status;
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
                handleSigterm();
                resetLastSignal();
            } else {
                throw std::logic_error(str(boost::format(
                        "Unexpected signal received: tid=%d, signal=%d (%s)") %
                                tid_ % lastSignal()% strsignal(lastSignal())));
            }
        }
        std::unique_lock<std::mutex> lock(mutex_);
        isAlive_ = false;
    } catch (const std::exception& e) {
        TRACE(">>> Oh no you don't! " << e.what());
        std::unique_lock<std::mutex> lock(mutex_);
        isAlive_ = false;
        raise(SIGABRT);
    }
}

bool WatTracer::onTraceeStatusChanged(int status) {
    if (WIFEXITED(status)) {
        TRACE(">>> THREAD tid=" << tid_ << " has finished");
        profiler_->endThread(tid_);
        return false;
    } else if (WIFSIGNALED(status)) {
        TRACE(">>> THREAD tid=" << tid_ << " is terminated by a signal");
        profiler_->endThread(tid_);
        stackPromise_.set_exception(
                std::make_exception_ptr(std::runtime_error(
                        str(boost::format(
                            "Process is killed by deadly signal %d") %
                            WTERMSIG(status)))));
        return false;
    } else if (WIFSTOPPED(status)) {
        int deliveredSignal = WSTOPSIG(status);
        TRACE(">>> THREAD tid=" << tid_ << " is stopped by " << strsignal(deliveredSignal));
        switch (deliveredSignal) {
            // group-stop
            case SIGSTOP:
                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    if (doDetach_) {
                        ptraceCmd(PTRACE_DETACH, tid_, 0);
                        TRACE("DETACHED tid=" << tid_);
                        return false;
                    }

                    if (!isStacktracePending_) {
                        TRACE("THIS CAN'T BE tid=" << tid_ << "; " <<
                            "signal = " << strsignal(deliveredSignal));
                        deliveredSignal = 0;
                    } else {
                        TRACE("GOTTEN STACKTRACE FOR " << tid_);
                        isStacktracePending_ = false;
                        stackPromise_.set_value(stacktraceImpl());
                        deliveredSignal = 0;
                    }
                }
            break;
            // group-stop
            case SIGTSTP:
            case SIGTTIN:
            case SIGTTOU:
                // The thread is stopped, but it wasn't us. Not sure what to do.
                deliveredSignal = 0;
                break;
            case SIGTRAP:
                if (status >> 16 == PTRACE_EVENT_CLONE) {
                    TRACE(">>> THREAD tid=" << tid_ << " has spawned a new thread");
                    deliveredSignal = 0;
                    long newTid;
                    // The newly created thread starts in stopped state.
                    // We detach it and then reattach it from separate
                    // thread which will handle this thread tracing.
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

std::vector<Frame> WatTracer::stacktraceImpl() {
    std::vector<Frame> stacktrace;

    unw_cursor_t cursor;
    throwUnwindIfLessThan0(unw_init_remote(
                &cursor, addressSpace_.get(), unwindInfo_.get()));
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

StoppedWat::StoppedWat(pid_t pid, pid_t tid, Profiler* profiler) :
    tracer_(std::unique_ptr<WatTracer>(new WatTracer(pid, tid, profiler)))
{
}

Wat::Wat(std::unique_ptr<WatTracer> tracer) :
    tracer_(std::move(tracer))
{
    tracer_->goodToGo_.set_value();
}
