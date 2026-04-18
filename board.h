#ifndef BOARD_H
#define BOARD_H

#include <cstdint>
#include <stdexcept>
#include <string>

#include "bitboard.h"

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

class board
{
public:
    board();

    void resetBoard();
    void loadFEN(const std::string& fen);

    Unmove makeMove(const Move& m);
    void unmakeMove(const Move& m, const Unmove& u);

    inline int toIndex(int row, int col) const { return (row << 3) | col; }

    inline bool isWhitePiece(Piece p) const {
        return p >= WQ && p <= WB;
    }

    inline bool isBlackPiece(Piece p) const {
        return p >= BQ && p <= BB;
    }

    inline Bitboard pieces(Piece p) const {
        return pieceBB[p];
    }

    inline Bitboard occupancy(bool white) const {
        return white ? whiteOcc : blackOcc;
    }

    inline int kingSquare(bool white) const {
        return white ? whiteKingSquare : blackKingSquare;
    }

    inline int countPieces(Piece p) const {
        return pieceCount[p];
    }

    Piece pieceAt(int sq) const;
    void rebuildPieceLists();
    int zobristEnPassantFile() const;

public:
    static constexpr int MAX_PIECES_PER_TYPE = 16;

    Bitboard pieceBB[13]{};
    Bitboard whiteOcc = 0;
    Bitboard blackOcc = 0;
    Bitboard occupied = 0;

    int pieceList[13][MAX_PIECES_PER_TYPE]{};
    int pieceCount[13]{};
    int pieceIndex[64]{};

    bool isWhiteTurn = true;
    int castleRights = 0;
    bool hasEnPassant = false;
    int enPassantSquare = -1;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    int whiteKingSquare = -1;
    int blackKingSquare = -1;
    uint64_t hash = 0;

private:
    void clearBoard();
    void addPiece(Piece p, int sq);
    void removePiece(Piece p, int sq);
    void movePiece(Piece p, int from, int to);

    Piece squareBoard[64]{};
};

#endif // BOARD_H
