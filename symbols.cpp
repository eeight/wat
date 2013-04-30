#include "exception.h"
#include "symbols.h"
#include "scope.h"

#include <map>

#include <cxxabi.h>
#include <string.h>

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

namespace {
std::map<unw_word_t, std::string>* symbolsCache() {
    static std::map<unw_word_t, std::string> symbols;
    return &symbols;
}
} // namespace

std::string getProcName(unw_cursor_t *cursor) {
    unw_word_t ip;
    throwUnwindIfLessThan0(unw_get_reg(cursor, UNW_REG_IP, &ip));

    auto cache = symbolsCache();
    auto iter = cache->find(ip);
    if (iter == cache->end()) {
        unw_word_t offset;
        char procName[1024] = {0};
        if (unw_get_proc_name(cursor, procName, sizeof(procName), &offset) < 0) {
            strcpy(procName, "{unknown}");
        }
        iter = cache->emplace(ip, procName).first;
    }
    return iter->second;
}
