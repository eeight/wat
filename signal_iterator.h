#pragma once

#include <initializer_list>

class SignalIterator {
public:
    explicit SignalIterator(std::initializer_list<int> signals);

    ~SignalIterator();

    int next();

private:
    int fd_;
};
