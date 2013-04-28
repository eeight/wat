#include "wat.h"
#include "exception.h"
#include "symbols.h"

#include <libunwind-ptrace.h>

Wat::Wat(pid_t pid) :
        pid_(pid),
        addressSpace_(throwUnwindIf0(
                unw_create_addr_space(&_UPT_accessors, 0))),
        unwindInfo_(throwUnwindIf0(_UPT_create(pid)))
{
}

Wat::~Wat() {
    unw_destroy_addr_space(addressSpace_);
    if (unwindInfo_) {
        _UPT_destroy(unwindInfo_);
    }
}

Wat::Wat(Wat&& other) :
    pid_(other.pid_),
    addressSpace_(other.addressSpace_),
    unwindInfo_(other.unwindInfo_)
{
    other.addressSpace_ = nullptr;
    other.unwindInfo_ = nullptr;
}

Wat& Wat::operator =(Wat&& other) {
    if (this != &other) {
        this->~Wat();
        addressSpace_ = other.addressSpace_;
        unwindInfo_ = other.unwindInfo_;
        other.addressSpace_ = nullptr;
        other.unwindInfo_ = nullptr;
    }
    return *this;
}

std::vector<Frame> Wat::stacktrace() {
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
