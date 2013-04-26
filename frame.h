#pragma once

#include <libunwind.h>

#include <string>

struct Frame {
    unw_word_t ip;
    unw_word_t sp;
    std::string procName;

    bool operator <(const Frame& other) const {
        return ip < other.ip;
    }
};
