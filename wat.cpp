#include <sys/ptrace.h>
#include <sys/wait.h>

#include <libunwind.h>
#include <libunwind-ptrace.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/preprocessor/cat.hpp>

#include <cxxabi.h>

#include <iostream>
#include <stdexcept>
#include <memory>

template <class Body>
class ScopeGuard {
public:
    explicit ScopeGuard(Body body): body_(body)
    {}

    ~ScopeGuard() { body_(); }

private:
    Body body_;
};

template <class Body>
auto scopeGuard(Body body) { return ScopeGuard<Body>(body); }

#define SCOPE_EXIT(...) auto BOOST_PP_CAT(guard, __LINE__) = scopeGuard([&] { __VA_ARGS__; });

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

std::string demangle(const std::string& str) {
    int status;
    char* demangled = __cxxabiv1::__cxa_demangle(
            str.c_str(), nullptr, 0, &status);
    if (status) {
        return str;
    }
    SCOPE_EXIT(free(demangled));
    return demangled;
}

void stacktrace(int pid) {
    unw_addr_space_t addressSpace = unw_create_addr_space(&_UPT_accessors, 0);
    SCOPE_EXIT(unw_destroy_addr_space(addressSpace));
    unw_cursor_t cursor;
    unw_word_t ip, sp, off;
    unw_proc_info_t procInfo;
    char buf[1024];

    int status;
    throwErrnoIfMinus1(ptrace(PTRACE_ATTACH, pid, nullptr, nullptr));
    throwErrnoIfMinus1(waitpid(pid, &status, WUNTRACED | WCONTINUED));
    if (WIFEXITED(status)) {
        std::cerr << "killed: " << pid << std::endl;
        std::cerr << "exit status: " << WEXITSTATUS(status) << std::endl;
        if (WTERMSIG(status)) {
            std::cerr << "deadly signal: " << WTERMSIG(status) << std::endl;
        }
        return;
    } else if (WIFSTOPPED(status)) {
        std::cerr << "stopped: " << pid << std::endl;
    }

    void *ui = _UPT_create(pid);
    if (!ui) {
        throw std::logic_error("Cannot attach");
    }
    SCOPE_EXIT(_UPT_destroy(ui));

    throwUnwindIfLessThan0(unw_init_remote(&cursor, addressSpace, ui));
    int depth = 0;
    do {
        throwUnwindIfLessThan0(unw_get_reg(&cursor, UNW_REG_IP, &ip));
        throwUnwindIfLessThan0(unw_get_reg(&cursor, UNW_REG_SP, &sp));
        if (unw_get_proc_name(&cursor, buf, sizeof (buf), &off) < 0) {
            strcpy(buf, "<unknown>");
        }
        std::cout << boost::format(
                "%016lx %-32s + 0x%016lx (sp=%016lx)\n") %
                ip % demangle(buf) % off % sp;
        if (++depth == 200) {
            break;
        }
    } while (throwUnwindIfLessThan0(unw_step(&cursor)));
    throwErrnoIfMinus1(ptrace(PTRACE_DETACH, pid, nullptr, nullptr));
    std::cerr << "detached: " << pid << std::endl;
}

int main(int argc, const char *argv[])
{
    try {
        if (argc != 2) {
            std::cerr << boost::format("Usage: %s pid\n") %
                boost::filesystem::basename(argv[0]);
            return 1;
        }
        int pid = boost::lexical_cast<int>(argv[1]);
#if 0
        throwErrnoIfMinus1(ptrace(PTRACE_ATTACH, pid, nullptr, nullptr));
        int status;
        throwErrnoIfMinus1(waitpid(pid, &status, WUNTRACED | WCONTINUED));
        if (WIFEXITED(status)) {
            std::cerr << "killed: " << pid << std::endl;
            std::cerr << "exit status: " << WEXITSTATUS(status) << std::endl;
            if (WTERMSIG(status)) {
                std::cerr << "deadly signal: " << WTERMSIG(status) << std::endl;
            }
        } else if (WIFSTOPPED(status)) {
            std::cerr << "stopped: " << pid << std::endl;
            throwErrnoIfMinus1(ptrace(PTRACE_DETACH, pid, nullptr, nullptr));
            std::cerr << "detached: " << pid << std::endl;
        }
#endif
        stacktrace(pid);
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
