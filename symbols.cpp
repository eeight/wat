#include "symbols.h"
#include "scope.h"

#include <cxxabi.h>

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

std::string abbrev(const std::string& name) {
    int nesting = 0;
    std::string result;

    for (char c: name) {
        if (c == '<') {
            ++nesting;
        }

        if (nesting < 1) {
            result.push_back(c);
        }

        if (c == '>') {
            --nesting;
        }
    }

    return result;
}

