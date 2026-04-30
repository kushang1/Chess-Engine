#pragma once
#include <cstdint>
#include <string>
#include "Board.h"
#include "ChessApi.h"

struct PolyglotEntry {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
};


CHESS_API uint64_t polyglotHash(const board& b);
CHESS_API Move polyglotDecodeMove(uint16_t m16, const board& b);
