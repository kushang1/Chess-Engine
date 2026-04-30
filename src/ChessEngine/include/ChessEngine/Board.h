#ifndef BOARD_H
#define BOARD_H

#include <cstdint>
#include <stdexcept>
#include <string>

#include "ChessApi.h"
#include "Bitboard.h"
#include "Move.h"

class CHESS_API board
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
