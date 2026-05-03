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


uint64_t polyglotHash(const board& b);
Move polyglotDecodeMove(uint16_t m16, const board& b);
