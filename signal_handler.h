#pragma once

#include <initializer_list>

void handleSignals(
        std::initializer_list<int> handle,
        std::initializer_list<int> block);

int lastSignal();
void resetLastSignal();
