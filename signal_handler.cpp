#include "signal_handler.h"
#include "exception.h"

#include <signal.h>
#include <string.h>

namespace {

thread_local int g_lastSignal = 0;

void sighandler(int signal) {
    g_lastSignal = signal;
}

} // namespace

void handleSignals(
        std::initializer_list<int> handle,
        std::initializer_list<int> block) {
    sigset_t set;
    throwErrnoIfMinus1(sigemptyset(&set));
    for (int signal: block) {
        throwErrnoIfMinus1(sigaddset(&set, signal));
    }
    throwErrnoIfMinus1(pthread_sigmask(SIG_BLOCK, &set, nullptr));
    throwErrnoIfMinus1(sigemptyset(&set));
    for (int signal: handle) {
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_handler = &sighandler;
        throwErrnoIfMinus1(sigaction(signal, &act, nullptr));
    }
}

int lastSignal() {
    return g_lastSignal;
}

void resetLastSignal() {
    g_lastSignal = 0;
}
