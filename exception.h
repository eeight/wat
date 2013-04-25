#pragma once

#include <stdexcept>

#include <libunwind.h>
#include <string.h>

inline int throwErrnoIfMinus1(int ret) {
    if (ret == -1) {
        throw std::runtime_error(std::string("syscall: ") + strerror(errno));
    }
    return ret;
}

inline int throwUnwindIfLessThan0(int ret) {
    if (ret < 0) {
        throw std::runtime_error(
                std::string("libunwind: ") + unw_strerror(ret));
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

