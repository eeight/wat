#include "exception.h"

#include <stdexcept>

#include <libunwind.h>
#include <string.h>

SyscallError::SyscallError(int error) :
    error_(error),
    what_(std::string("syscall: ") + strerror(error))
{}

int throwErrnoIfMinus1(int ret) {
    if (ret == -1) {
        throwErrno();
    }
    return ret;
}

int throwErrno() {
    throw SyscallError(errno);
}

int throwUnwindIfLessThan0(int ret) {
    if (ret < 0) {
        throw std::runtime_error(
                std::string("libunwind: ") + unw_strerror(ret));
    }
    return ret;
}

void* throwUnwindIfVoid0(void* ret) {
    if (!ret) {
        throw std::runtime_error("Unknown unwind error");
    }
    return ret;
}

