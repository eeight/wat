#include "signal_iterator.h"
#include "exception.h"

#include <signal.h>
#include <unistd.h>
#include <sys/signalfd.h>

SignalIterator::SignalIterator(std::initializer_list<int> signals) {
    sigset_t set;
    throwErrnoIfMinus1(sigemptyset(&set));
    for (int signal: signals) {
        throwErrnoIfMinus1(sigaddset(&set, signal));
    }
    throwErrnoIfMinus1(sigprocmask(SIG_BLOCK, &set, nullptr));
    fd_ = throwErrnoIfMinus1(signalfd(-1, &set, 0));
}

SignalIterator::~SignalIterator() {
    while (close(fd_) == -1 && errno == EINTR);
}

int SignalIterator::next() {
    struct signalfd_siginfo sigInfo;
    throwErrnoIfMinus1(read(fd_, &sigInfo, sizeof(sigInfo)));
    return sigInfo.ssi_signo;
}
