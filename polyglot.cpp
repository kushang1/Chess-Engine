#include "polyglot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "./Book/polyglot_random.cpp"

namespace {

int polyID(Piece p) {
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

int updatedCastleRights(int currentRights, Piece moved, int from, Piece captured, int to) {
    int rights = currentRights;

    if (moved == WK) {
        rights &= 0b1100;
    }
    else if (moved == BK) {
        rights &= 0b0011;
    }
    else if (moved == WR) {
        if (from == 63) rights &= 0b1110;
        else if (from == 56) rights &= 0b1101;
    }
    else if (moved == BR) {
        if (from == 7) rights &= 0b1011;
        else if (from == 0) rights &= 0b0111;
    }

    if (captured == WR) {
        if (to == 63) rights &= 0b1110;
        else if (to == 56) rights &= 0b1101;
    }
    else if (captured == BR) {
        if (to == 7) rights &= 0b1011;
        else if (to == 0) rights &= 0b0111;
    }

    return rights;
}

inline int toPolySq(int engineSq) {
    int row = engineSq / 8;
    int file = engineSq & 7;
    int polyRank = 7 - row;
    return polyRank * 8 + file;
}

} // namespace

uint64_t polyglotHash(const board& b)
{
    uint64_t h = 0;

    for (int p = BQ; p <= WB; ++p) {
        Piece piece = static_cast<Piece>(p);
        int id = polyID(piece);
        if (id == -1) {
            continue;
        }

        for (int i = 0; i < b.pieceCount[piece]; ++i) {
            int sq = b.pieceList[piece][i];
            h ^= polyglotRandom[64 * id + toPolySq(sq)];
        }
    }

    if ((b.castleRights & 0b0001) != 0) h ^= polyglotRandom[768];
    if ((b.castleRights & 0b0010) != 0) h ^= polyglotRandom[769];
    if ((b.castleRights & 0b0100) != 0) h ^= polyglotRandom[770];
    if ((b.castleRights & 0b1000) != 0) h ^= polyglotRandom[771];

    int epFile = b.zobristEnPassantFile();
    if (epFile != -1) {
        h ^= polyglotRandom[772 + epFile];
    }

    if (b.isWhiteTurn) {
        h ^= polyglotRandom[780];
    }

    return h;
}

Move polyglotDecodeMove(uint16_t m16, const board& b)
{
    int fromFile = (m16 >> 6) & 7;
    int fromRank = (m16 >> 9) & 7;
    int toFile = (m16 >> 0) & 7;
    int toRank = (m16 >> 3) & 7;
    int prom = (m16 >> 12) & 7;

    int fromRow = 7 - fromRank;
    int toRow = 7 - toRank;

    int fromSq = fromRow * 8 + fromFile;
    int toSq = toRow * 8 + toFile;

    Piece moved = b.pieceAt(fromSq);

    const bool isKing = (moved == WK || moved == BK);
    if (isKing) {
        const int WHITE_KING_FROM = 60;
        const int WHITE_OO = 62;
        const int WHITE_OOO = 58;
        const int WHITE_ROOK_A1 = 56;
        const int WHITE_ROOK_H1 = 63;
        const int BLACK_KING_FROM = 4;
        const int BLACK_OO = 6;
        const int BLACK_OOO = 2;
        const int BLACK_ROOK_A8 = 0;
        const int BLACK_ROOK_H8 = 7;

        int df = std::abs((fromSq & 7) - (toSq & 7));
        if (df != 2) {
            if (fromSq == WHITE_KING_FROM) {
                if (toSq == WHITE_ROOK_H1) toSq = WHITE_OO;
                if (toSq == WHITE_ROOK_A1) toSq = WHITE_OOO;
            }
            else if (fromSq == BLACK_KING_FROM) {
                if (toSq == BLACK_ROOK_H8) toSq = BLACK_OO;
                if (toSq == BLACK_ROOK_A8) toSq = BLACK_OOO;
            }
        }
    }

    Piece captured = b.pieceAt(toSq);
    if ((moved == WP || moved == BP) && toSq == b.enPassantSquare && captured == EMPTY) {
        captured = (moved == WP) ? BP : WP;
    }

    Move m(fromSq, toSq, moved, captured,
        updatedCastleRights(b.castleRights, moved, fromSq, captured, toSq));

    if (isKing && std::abs((fromSq & 7) - (toSq & 7)) == 2) {
        m.wasCastling = true;
        m.captured = EMPTY;
    }

    if (prom != 0) {
        static Piece promoWhite[8] = { EMPTY, WN, WB, WR, WQ };
        static Piece promoBlack[8] = { EMPTY, BN, BB, BR, BQ };
        m.wasPromotion = true;
        m.promotedTo = b.isWhiteTurn ? promoWhite[prom] : promoBlack[prom];
    }

    if ((moved == WP || moved == BP) && toSq == b.enPassantSquare && b.hasEnPassant) {
        m.wasEnPassant = true;
        m.captured = (moved == WP) ? BP : WP;
    }

    if (moved == WP && fromSq - toSq == 16) {
        m.hasEnPassant = true;
        m.enPassantSquare = fromSq - 8;
    }
    else if (moved == BP && toSq - fromSq == 16) {
        m.hasEnPassant = true;
        m.enPassantSquare = fromSq + 8;
    }

    return m;
}
