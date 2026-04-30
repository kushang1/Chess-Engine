#pragma once

#include <cstdint>

#include "Move.h"

namespace chess {

struct SearchResult {
    Move bestMove;
    long long nodes = 0;
    long long leafNodes = 0;
    int elapsedMs = 0;
};

struct PerftResult {
    long long nodes = 0;
    int elapsedMs = 0;
};

} // namespace chess
