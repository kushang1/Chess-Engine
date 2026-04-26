#pragma once

#include <array>
#include <cstdint>
#include <immintrin.h>
#include <intrin.h>
#include <vector>

#include "profiler.h"

using Bitboard = uint64_t;

namespace Bitboards {

enum Color : int {
    WHITE = 0,
    BLACK = 1
};

enum Direction : int {
    NORTH = 0,
    SOUTH = 1,
    EAST = 2,
    WEST = 3,
    NORTH_EAST = 4,
    NORTH_WEST = 5,
    SOUTH_EAST = 6,
    SOUTH_WEST = 7
};

inline std::array<Bitboard, 64> KnightAttacks{};
inline std::array<Bitboard, 64> KingAttacks{};
inline std::array<std::array<Bitboard, 64>, 2> PawnAttacks{};
inline std::array<std::array<Bitboard, 64>, 2> PawnAttackers{};
inline std::array<std::array<Bitboard, 8>, 64> Rays{};
inline std::array<std::array<Bitboard, 64>, 64> Between{};
inline std::array<std::array<Bitboard, 64>, 64> LineMasks{};
inline std::array<Bitboard, 64> RookMasks{};
inline std::array<Bitboard, 64> BishopMasks{};
inline std::array<uint32_t, 64> RookOffsets{};
inline std::array<uint32_t, 64> BishopOffsets{};
inline std::vector<Bitboard> RookAttackTable{};
inline std::vector<Bitboard> BishopAttackTable{};
inline bool UseHardwarePext = false;
inline bool Initialized = false;

inline constexpr Bitboard bit(int sq) {
    return 1ULL << sq;
}

inline int popcount(Bitboard bb) {
    return static_cast<int>(__popcnt64(bb));
}

inline int lsb(Bitboard bb) {
    unsigned long idx;
    _BitScanForward64(&idx, bb);
    return static_cast<int>(idx);
}

inline int msb(Bitboard bb) {
    unsigned long idx;
    _BitScanReverse64(&idx, bb);
    return static_cast<int>(idx);
}

inline int poplsb(Bitboard& bb) {
    int sq = lsb(bb);
    bb &= (bb - 1);
    return sq;
}

inline bool onBoard(int row, int col) {
    return row >= 0 && row < 8 && col >= 0 && col < 8;
}

inline int toSquare(int row, int col) {
    return (row << 3) | col;
}

inline bool directionUsesLsb(Direction dir) {
    return dir == SOUTH || dir == EAST || dir == SOUTH_EAST || dir == SOUTH_WEST;
}

inline Bitboard lineAttacksSlow(int sq, Bitboard occ, Direction positiveDir, Direction negativeDir) {
    Bitboard attacks = 0;

    Bitboard ray = Rays[sq][positiveDir];
    if (ray != 0) {
        Bitboard blockers = ray & occ;
        if (blockers != 0) {
            int blockerSq = directionUsesLsb(positiveDir) ? lsb(blockers) : msb(blockers);
            attacks |= ray ^ Rays[blockerSq][positiveDir];
        }
        else {
            attacks |= ray;
        }
    }

    ray = Rays[sq][negativeDir];
    if (ray != 0) {
        Bitboard blockers = ray & occ;
        if (blockers != 0) {
            int blockerSq = directionUsesLsb(negativeDir) ? lsb(blockers) : msb(blockers);
            attacks |= ray ^ Rays[blockerSq][negativeDir];
        }
        else {
            attacks |= ray;
        }
    }

    return attacks;
}

inline Bitboard rookAttacksSlow(int sq, Bitboard occ) {
    return lineAttacksSlow(sq, occ, EAST, WEST) | lineAttacksSlow(sq, occ, SOUTH, NORTH);
}

inline Bitboard bishopAttacksSlow(int sq, Bitboard occ) {
    return lineAttacksSlow(sq, occ, SOUTH_EAST, NORTH_WEST) |
        lineAttacksSlow(sq, occ, SOUTH_WEST, NORTH_EAST);
}

inline uint64_t pext64(Bitboard value, Bitboard mask) {
    if (UseHardwarePext) {
        return _pext_u64(value, mask);
    }

    uint64_t result = 0;
    uint64_t outBit = 1;
    while (mask) {
        uint64_t lsbMask = mask & (~mask + 1);
        if (value & lsbMask) {
            result |= outBit;
        }
        mask ^= lsbMask;
        outBit <<= 1;
    }
    return result;
}

inline Bitboard occupancyFromIndex(int index, Bitboard mask) {
    Bitboard occ = 0;
    int bitIndex = 0;
    while (mask) {
        Bitboard lsbMask = mask & (~mask + 1);
        if ((index & (1 << bitIndex)) != 0) {
            occ |= lsbMask;
        }
        mask ^= lsbMask;
        ++bitIndex;
    }
    return occ;
}

inline Bitboard rookAttacks(int sq, Bitboard occ) {
    Profiler::ScopedTimer timer(Profiler::SlidingAttack);
    Bitboard mask = RookMasks[sq];
    uint64_t index = pext64(occ, mask);
    return RookAttackTable[RookOffsets[sq] + index];
}

inline Bitboard bishopAttacks(int sq, Bitboard occ) {
    Profiler::ScopedTimer timer(Profiler::SlidingAttack);
    Bitboard mask = BishopMasks[sq];
    uint64_t index = pext64(occ, mask);
    return BishopAttackTable[BishopOffsets[sq] + index];
}

inline Bitboard queenAttacks(int sq, Bitboard occ) {
    return rookAttacks(sq, occ) | bishopAttacks(sq, occ);
}

inline void init() {
    if (Initialized) {
        return;
    }

    constexpr int knightSteps[8][2] = {
        { 2, 1 }, { 2, -1 }, { -2, 1 }, { -2, -1 },
        { 1, 2 }, { 1, -2 }, { -1, 2 }, { -1, -2 }
    };
    constexpr int kingSteps[8][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
    };
    constexpr int raySteps[8][2] = {
        { -1, 0 }, { 1, 0 }, { 0, 1 }, { 0, -1 },
        { -1, 1 }, { -1, -1 }, { 1, 1 }, { 1, -1 }
    };
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 0);
    if (cpuInfo[0] >= 7) {
        __cpuidex(cpuInfo, 7, 0);
        UseHardwarePext = (cpuInfo[1] & (1 << 8)) != 0;
    }

    for (int sq = 0; sq < 64; ++sq) {
        int row = sq / 8;
        int col = sq % 8;

        for (auto& step : knightSteps) {
            int nr = row + step[0];
            int nc = col + step[1];
            if (onBoard(nr, nc)) {
                KnightAttacks[sq] |= bit(toSquare(nr, nc));
            }
        }

        for (auto& step : kingSteps) {
            int nr = row + step[0];
            int nc = col + step[1];
            if (onBoard(nr, nc)) {
                KingAttacks[sq] |= bit(toSquare(nr, nc));
            }
        }

        // White pawn attacks move toward smaller rows.
        if (onBoard(row - 1, col - 1)) {
            PawnAttacks[WHITE][sq] |= bit(toSquare(row - 1, col - 1));
        }
        if (onBoard(row - 1, col + 1)) {
            PawnAttacks[WHITE][sq] |= bit(toSquare(row - 1, col + 1));
        }

        // Black pawn attacks move toward larger rows.
        if (onBoard(row + 1, col - 1)) {
            PawnAttacks[BLACK][sq] |= bit(toSquare(row + 1, col - 1));
        }
        if (onBoard(row + 1, col + 1)) {
            PawnAttacks[BLACK][sq] |= bit(toSquare(row + 1, col + 1));
        }

        if (onBoard(row + 1, col - 1)) {
            PawnAttackers[WHITE][sq] |= bit(toSquare(row + 1, col - 1));
        }
        if (onBoard(row + 1, col + 1)) {
            PawnAttackers[WHITE][sq] |= bit(toSquare(row + 1, col + 1));
        }
        if (onBoard(row - 1, col - 1)) {
            PawnAttackers[BLACK][sq] |= bit(toSquare(row - 1, col - 1));
        }
        if (onBoard(row - 1, col + 1)) {
            PawnAttackers[BLACK][sq] |= bit(toSquare(row - 1, col + 1));
        }

        for (int dir = 0; dir < 8; ++dir) {
            int nr = row + raySteps[dir][0];
            int nc = col + raySteps[dir][1];
            while (onBoard(nr, nc)) {
                Rays[sq][dir] |= bit(toSquare(nr, nc));
                nr += raySteps[dir][0];
                nc += raySteps[dir][1];
            }
        }

        for (int nr = row - 1; nr >= 1; --nr) RookMasks[sq] |= bit(toSquare(nr, col));
        for (int nr = row + 1; nr <= 6; ++nr) RookMasks[sq] |= bit(toSquare(nr, col));
        for (int nc = col - 1; nc >= 1; --nc) RookMasks[sq] |= bit(toSquare(row, nc));
        for (int nc = col + 1; nc <= 6; ++nc) RookMasks[sq] |= bit(toSquare(row, nc));

        for (int nr = row - 1, nc = col - 1; nr >= 1 && nc >= 1; --nr, --nc) BishopMasks[sq] |= bit(toSquare(nr, nc));
        for (int nr = row - 1, nc = col + 1; nr >= 1 && nc <= 6; --nr, ++nc) BishopMasks[sq] |= bit(toSquare(nr, nc));
        for (int nr = row + 1, nc = col - 1; nr <= 6 && nc >= 1; ++nr, --nc) BishopMasks[sq] |= bit(toSquare(nr, nc));
        for (int nr = row + 1, nc = col + 1; nr <= 6 && nc <= 6; ++nr, ++nc) BishopMasks[sq] |= bit(toSquare(nr, nc));
    }

    for (int from = 0; from < 64; ++from) {
        for (int to = 0; to < 64; ++to) {
            if (from == to) {
                LineMasks[from][to] = bit(from);
                continue;
            }

            for (int dir = 0; dir < 8; ++dir) {
                if ((Rays[from][dir] & bit(to)) == 0) {
                    continue;
                }

                Between[from][to] = Rays[from][dir] ^ Rays[to][dir] ^ bit(to);
                LineMasks[from][to] = Between[from][to] | bit(from) | bit(to);
                break;
            }
        }
    }

    uint32_t rookTotal = 0;
    uint32_t bishopTotal = 0;
    for (int sq = 0; sq < 64; ++sq) {
        RookOffsets[sq] = rookTotal;
        BishopOffsets[sq] = bishopTotal;
        rookTotal += (1u << popcount(RookMasks[sq]));
        bishopTotal += (1u << popcount(BishopMasks[sq]));
    }

    RookAttackTable.resize(rookTotal);
    BishopAttackTable.resize(bishopTotal);

    for (int sq = 0; sq < 64; ++sq) {
        int rookEntries = 1 << popcount(RookMasks[sq]);
        for (int index = 0; index < rookEntries; ++index) {
            Bitboard occ = occupancyFromIndex(index, RookMasks[sq]);
            RookAttackTable[RookOffsets[sq] + index] = rookAttacksSlow(sq, occ);
        }

        int bishopEntries = 1 << popcount(BishopMasks[sq]);
        for (int index = 0; index < bishopEntries; ++index) {
            Bitboard occ = occupancyFromIndex(index, BishopMasks[sq]);
            BishopAttackTable[BishopOffsets[sq] + index] = bishopAttacksSlow(sq, occ);
        }
    }

    Initialized = true;
}

} // namespace Bitboards
