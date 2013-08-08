#pragma once

#include <string>
#include <stdexcept>

class ThreadIsGone : public std::exception {
public:
    const char* what() const throw() override {
        return "Thread is gone";
    }
};

class SyscallError : public std::exception {
public:
    explicit SyscallError(int error);

    const char* what() const throw() override { return what_.c_str(); }

    int error() const { return error_; }

private:
    int error_;
    std::string what_;
};

int throwErrnoIfMinus1(int ret);
int throwErrno();
int throwUnwindIfLessThan0(int ret);
void *throwUnwindIfVoid0(void *);

template <class T>
T* throwUnwindIf0(T* ret) {
    return reinterpret_cast<T *>(throwUnwindIfVoid0(ret));
}

