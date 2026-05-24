#pragma once

#include <cstdint>

enum Piece : std::uint8_t {
    EMPTY,
    BQ, BR, BP, BN, BK, BB,
    WQ, WR, WP, WN, WK, WB
};

struct Unmove {
    uint64_t prevHash = 0;
    int prevCastleRights = 0;
    int prevEnPassantSquare = -1;
    int prevHalfmoveClock = 0;
    int prevFullmoveNumber = 0;
    int epCapturedSquare = -1;

    Piece fromPiece = EMPTY;
    Piece toPiece = EMPTY;
    Piece epCapturedPiece = EMPTY;

    bool prevHasEnPassant = false;
    bool prevTurn = true;
};

class Move {
public:
    Move()
        : from(-1), to(-1), castleRights(0), enPassantSquare(-1),
        moved(EMPTY), captured(EMPTY), promotedTo(EMPTY),
        wasCastling(false), wasPromotion(false),
        wasEnPassant(false), hasEnPassant(false) {
    }

    Move(int from, int to, Piece moved, Piece captured, int castleRights)
        : from(from), to(to), castleRights(castleRights), enPassantSquare(-1),
        moved(moved), captured(captured), promotedTo(EMPTY),
        wasCastling(false), wasPromotion(false),
        wasEnPassant(false), hasEnPassant(false) {
    }

    int from;
    int to;
    int castleRights;
    int enPassantSquare;

    Piece moved;
    Piece captured;
    Piece promotedTo = EMPTY;

    bool wasCastling = false;
    bool wasPromotion = false;
    bool wasEnPassant = false;
    bool hasEnPassant = false;
};

static_assert(sizeof(Move) <= 24, "Move should stay compact for move-list/search hot paths");
static_assert(sizeof(Unmove) <= 40, "Unmove should stay compact for make/unmake hot paths");
