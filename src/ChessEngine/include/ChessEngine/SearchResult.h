#pragma once

#include <cstdint>
#include <vector>

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

struct PerftDivideEntry {
    Move move;
    long long nodes = 0;
};

enum class GameStatusKind {
    Ongoing,
    Checkmate,
    Stalemate,
    FiftyMoveRule,
    ThreefoldRepetition
};

struct GameStatus {
    GameStatusKind kind = GameStatusKind::Ongoing;
    bool whiteToMove = true;
    bool inCheck = false;
};

} // namespace chess
