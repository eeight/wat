#pragma once

#include <libunwind.h>
#include <string>

std::string demangle(const std::string& str);
std::string abbrev(const std::string& name);
std::string getProcName(unw_cursor_t *cursor);
