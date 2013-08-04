#pragma once

#include <string>
#include <stdexcept>

int throwErrnoIfMinus1(int ret);
int throwErrno();
int throwUnwindIfLessThan0(int ret);
void *throwUnwindIfVoid0(void *);

template <class T>
T* throwUnwindIf0(T* ret) {
    return reinterpret_cast<T *>(throwUnwindIfVoid0(ret));
}

