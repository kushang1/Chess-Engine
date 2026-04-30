#pragma once

#include <cstdint>

enum Piece {
    EMPTY,
    BQ, BR, BP, BN, BK, BB,
    WQ, WR, WP, WN, WK, WB
};

struct Unmove {
    Piece fromPiece = EMPTY;
    Piece toPiece = EMPTY;

    int prevCastleRights = 0;
    bool prevHasEnPassant = false;
    int prevEnPassantSquare = -1;
    bool prevTurn = true;
    int prevHalfmoveClock = 0;
    int prevFullmoveNumber = 0;
    uint64_t prevHash = 0;

    Piece epCapturedPiece = EMPTY;
    int epCapturedSquare = -1;
};

class Move {
public:
    Move()
        : from(-1), to(-1), moved(EMPTY), captured(EMPTY),
        wasCastling(false), castleRights(0),
        wasPromotion(false), promotedTo(EMPTY),
        wasEnPassant(false), hasEnPassant(false),
        enPassantSquare(-1) {
    }

    Move(int from, int to, Piece moved, Piece captured, int castleRights) {
        this->from = from;
        this->to = to;
        this->moved = moved;
        this->captured = captured;
        this->castleRights = castleRights;
    }

    int from;
    int to;
    Piece moved;
    Piece captured;

    bool wasCastling = false;
    int castleRights;

    bool wasPromotion = false;
    Piece promotedTo = EMPTY;

    bool wasEnPassant = false;

    bool hasEnPassant = false;
    int enPassantSquare = -1;
};
