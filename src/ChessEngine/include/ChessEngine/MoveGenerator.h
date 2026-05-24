#ifndef MOVEGENERATOR_H
#define MOVEGENERATOR_H

#include <cassert>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "Board.h"
#include "ChessApi.h"

struct MoveList {
    static constexpr int MAX_MOVES = 256;

    using Storage = std::aligned_storage_t<sizeof(Move), alignof(Move)>;
    Storage moves[MAX_MOVES];
    int count = 0;

    inline Move* data() {
        return std::launder(reinterpret_cast<Move*>(moves));
    }

    inline const Move* data() const {
        return std::launder(reinterpret_cast<const Move*>(moves));
    }

    inline void clear() {
        count = 0;
    }

    template <typename... Args>
    inline void emplace_back(Args&&... args) {
        assert(count < MAX_MOVES);
        ::new (static_cast<void*>(data() + count)) Move(std::forward<Args>(args)...);
        ++count;
    }

    inline void push_back(const Move& move) {
        assert(count < MAX_MOVES);
        ::new (static_cast<void*>(data() + count)) Move(move);
        ++count;
    }

    inline Move& back() {
        return data()[count - 1];
    }

    inline const Move& back() const {
        return data()[count - 1];
    }

    inline Move* begin() {
        return data();
    }

    inline Move* end() {
        return data() + count;
    }

    inline const Move* begin() const {
        return data();
    }

    inline const Move* end() const {
        return data() + count;
    }
};

class MoveGenerator
{
public:
    MoveGenerator();

    std::vector<Move> generatePseudoLegalMoves(board& Board);
    std::vector<Move> generateLegalMoves(board& Board);
    void generatePseudoLegalMoves(board& Board, MoveList& moves);
    void generateLegalMoves(board& Board, MoveList& moves);
    void generateLegalMoves(board& Board, MoveList& moves, int kingSq, Bitboard checkers);
    void generateQuiescenceMoves(board& Board, MoveList& moves);
    void generateQuiescenceMoves(board& Board, MoveList& moves, int kingSq, Bitboard checkers);
    void generateQuiescenceMoves(board& Board, MoveList& moves, int kingSq, Bitboard checkers,
        bool includeQuietChecks);
    int countLegalMoves(board& Board);

    Bitboard attackersToSquare(const board& Board, int sq, bool byWhite, Bitboard occ);
    bool isSquareAttacked(const board& Board, int sq, bool byWhite);
    int findKing(const board& Board, bool white);
    bool isKinginCheck(const board& Board, bool white);
};

#endif
