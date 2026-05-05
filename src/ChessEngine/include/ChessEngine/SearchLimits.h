#pragma once

#include <vector>

#include "Move.h"

namespace chess {

struct SearchLimits {
    int maxDepth = 128;
    int moveTimeMs = 1000;
    long long nodeLimit = 0;
    int mateMoves = 0;
    std::vector<Move> searchMoves;
};

} // namespace chess
