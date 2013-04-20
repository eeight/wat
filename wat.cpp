#include "scope.h"
#include "symbols.h"

#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <libunwind.h>
#include <libunwind-ptrace.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <iostream>
#include <stdexcept>
#include <memory>

int throwErrnoIfMinus1(int ret) {
    if (ret == -1) {
        throw std::runtime_error(strerror(errno));
    }
    return ret;
}

int throwUnwindIfLessThan0(int ret) {
    if (ret < 0) {
        throw std::runtime_error(unw_strerror(ret));
    }
    return ret;
}

template <class T>
T* throwUnwindIf0(T* ret) {
    if (!ret) {
        throw std::runtime_error("Unknown unwind error");
    }
    return ret;
}

struct Frame {
    unw_word_t ip;
    unw_word_t sp;
    unw_word_t offset;
    std::string procName;
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

            unw_word_t offset;
            char procName[1024] = {0};
            if (unw_get_proc_name(&cursor, procName, sizeof (procName), &offset) < 0) {
                strcpy(procName, "{unknown}");
            }
            frames.push_back({ip, sp, offset, procName});

            if (++depth == 200) {
                break;
            }
        } while (throwUnwindIfLessThan0(unw_step(&cursor)));

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

int main(int argc, const char *argv[])
{
    try {
        if (argc != 2) {
            std::cerr << boost::format("Usage: %s pid\n") %
                boost::filesystem::basename(argv[0]);
            return 1;
        }
        int pid = boost::lexical_cast<int>(argv[1]);
        Wat wat(pid);
        for (const auto& frame: wat.stacktrace()) {
            std::cout << boost::format("%016lx %s\n") %
                    frame.ip %
                    abbrev(demangle(frame.procName));
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
