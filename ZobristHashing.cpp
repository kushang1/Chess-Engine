#include "ZobristHashing.h"

#include <algorithm>

namespace {

uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace

namespace ZobristData {

static PieceSquareTable g_pieceSquare{};
static std::array<uint64_t, 2> g_sideToMove{};
static std::array<uint64_t, 16> g_castlingRights{};
static std::array<uint64_t, 8> g_enPassantFile{};
static bool g_initialized = false;

void init() {
    if (g_initialized) {
        return;
    }

    uint64_t seed = 0xC0D35B17B04D1234ULL;
    for (auto& pieceTable : g_pieceSquare) {
        for (uint64_t& key : pieceTable) {
            key = splitmix64(seed);
        }
    }

    for (uint64_t& key : g_sideToMove) {
        key = splitmix64(seed);
    }

    for (uint64_t& key : g_castlingRights) {
        key = splitmix64(seed);
    }

    for (uint64_t& key : g_enPassantFile) {
        key = splitmix64(seed);
    }

    g_initialized = true;
}

int pieceIndex(Piece p) {
    switch (p) {
    case WP: return 0;
    case WN: return 1;
    case WB: return 2;
    case WR: return 3;
    case WQ: return 4;
    case WK: return 5;
    case BP: return 6;
    case BN: return 7;
    case BB: return 8;
    case BR: return 9;
    case BQ: return 10;
    case BK: return 11;
    default: return -1;
    }
}

uint64_t pieceSquare(Piece p, int sq) {
    int idx = pieceIndex(p);
    return (idx >= 0 && sq >= 0 && sq < 64) ? g_pieceSquare[idx][sq] : 0ULL;
}

uint64_t sideToMove(bool whiteToMove) {
    return g_sideToMove[whiteToMove ? 0 : 1];
}

uint64_t castlingRights(int rights) {
    return g_castlingRights[rights & 0xF];
}

uint64_t enPassantFile(int file) {
    return (file >= 0 && file < 8) ? g_enPassantFile[file] : 0ULL;
}

} // namespace ZobristData

ZobristHashing::ZobristHashing() {
    ZobristData::init();
}

uint64_t ZobristHashing::computeHash(const board& b) const {
    uint64_t hashValue = 0;

    for (int p = BQ; p <= WB; ++p) {
        Piece piece = static_cast<Piece>(p);
        for (int i = 0; i < b.pieceCount[piece]; ++i) {
            hashValue ^= ZobristData::pieceSquare(piece, b.pieceList[piece][i]);
        }
    }

    hashValue ^= ZobristData::sideToMove(b.isWhiteTurn);
    hashValue ^= ZobristData::castlingRights(b.castleRights);

    int epFile = b.zobristEnPassantFile();
    if (epFile != -1) {
        hashValue ^= ZobristData::enPassantFile(epFile);
    }

    return hashValue;
}

void ZobristHashing::updateHash(uint64_t& hashValue, const Move& move, const board& b) const {
    (void)move;
    hashValue = computeHash(b);
}
