#include "movegenerator.h"

#include <array>

namespace {

constexpr Bitboard FULL_MASK = ~0ULL;

struct GenerationContext {
    bool whiteToMove = true;
    int kingSq = -1;
    Bitboard usOcc = 0;
    Bitboard themOcc = 0;
    Bitboard occ = 0;
    Bitboard checkers = 0;
    Bitboard checkMask = FULL_MASK;
    Bitboard pinned = 0;
    int checkerCount = 0;
    std::array<Bitboard, 64> pinMasks{};
};

inline bool isKingPiece(Piece p) {
    return p == WK || p == BK;
}

inline Piece pawnPiece(bool white) {
    return white ? WP : BP;
}

inline Piece knightPiece(bool white) {
    return white ? WN : BN;
}

inline Piece bishopPiece(bool white) {
    return white ? WB : BB;
}

inline Piece rookPiece(bool white) {
    return white ? WR : BR;
}

inline Piece queenPiece(bool white) {
    return white ? WQ : BQ;
}

inline Piece kingPiece(bool white) {
    return white ? WK : BK;
}

inline Bitboard bishopQueens(const board& b, bool white) {
    return b.pieces(bishopPiece(white)) | b.pieces(queenPiece(white));
}

inline Bitboard rookQueens(const board& b, bool white) {
    return b.pieces(rookPiece(white)) | b.pieces(queenPiece(white));
}

inline int updatedCastleRights(int currentRights, Piece moved, int from, Piece captured, int to) {
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

inline Bitboard attackersTo(const board& b, int sq, bool byWhite, Bitboard occ) {
    if (sq < 0 || sq >= 64) {
        return 0;
    }

    Bitboard attackers = 0;
    attackers |= Bitboards::PawnAttackers[byWhite ? Bitboards::WHITE : Bitboards::BLACK][sq]
        & b.pieces(pawnPiece(byWhite));
    attackers |= Bitboards::KnightAttacks[sq] & b.pieces(knightPiece(byWhite));
    attackers |= Bitboards::KingAttacks[sq] & b.pieces(kingPiece(byWhite));
    attackers |= Bitboards::bishopAttacks(sq, occ) & bishopQueens(b, byWhite);
    attackers |= Bitboards::rookAttacks(sq, occ) & rookQueens(b, byWhite);
    return attackers;
}

void computePins(const board& b, GenerationContext& ctx) {
    ctx.pinMasks.fill(FULL_MASK);

    Bitboard enemyOrthogonal = rookQueens(b, !ctx.whiteToMove);
    Bitboard enemyDiagonal = bishopQueens(b, !ctx.whiteToMove);

    constexpr Bitboards::Direction orthDirs[4] = {
        Bitboards::NORTH, Bitboards::SOUTH, Bitboards::EAST, Bitboards::WEST
    };
    constexpr Bitboards::Direction diagDirs[4] = {
        Bitboards::NORTH_EAST, Bitboards::NORTH_WEST, Bitboards::SOUTH_EAST, Bitboards::SOUTH_WEST
    };

    auto scanDir = [&](Bitboards::Direction dir, Bitboard enemySliders) {
        Bitboard blockers = Bitboards::Rays[ctx.kingSq][dir] & ctx.occ;
        if (blockers == 0) {
            return;
        }

        int firstSq = Bitboards::directionUsesLsb(dir)
            ? Bitboards::lsb(blockers)
            : Bitboards::msb(blockers);
        Bitboard firstMask = Bitboards::bit(firstSq);
        if ((firstMask & ctx.usOcc) == 0) {
            return;
        }

        blockers &= ~firstMask;
        if (blockers == 0) {
            return;
        }

        int secondSq = Bitboards::directionUsesLsb(dir)
            ? Bitboards::lsb(blockers)
            : Bitboards::msb(blockers);
        Bitboard secondMask = Bitboards::bit(secondSq);
        if ((secondMask & enemySliders) == 0) {
            return;
        }

        ctx.pinned |= firstMask;
        ctx.pinMasks[firstSq] = Bitboards::LineMasks[ctx.kingSq][secondSq];
    };

    for (Bitboards::Direction dir : orthDirs) {
        scanDir(dir, enemyOrthogonal);
    }
    for (Bitboards::Direction dir : diagDirs) {
        scanDir(dir, enemyDiagonal);
    }
}

GenerationContext buildContext(const board& b, bool legalOnly) {
    GenerationContext ctx;
    ctx.whiteToMove = b.isWhiteTurn;
    ctx.kingSq = b.kingSquare(ctx.whiteToMove);
    ctx.usOcc = b.occupancy(ctx.whiteToMove);
    ctx.themOcc = b.occupancy(!ctx.whiteToMove);
    ctx.occ = b.occupied;
    ctx.pinMasks.fill(FULL_MASK);

    if (!legalOnly || ctx.kingSq == -1) {
        return ctx;
    }

    ctx.checkers = attackersTo(b, ctx.kingSq, !ctx.whiteToMove, ctx.occ);
    ctx.checkerCount = Bitboards::popcount(ctx.checkers);

    if (ctx.checkerCount == 1) {
        int checkerSq = Bitboards::lsb(ctx.checkers);
        Piece checker = b.pieceAt(checkerSq);
        if (checker == WP || checker == BP || checker == WN || checker == BN) {
            ctx.checkMask = Bitboards::bit(checkerSq);
        }
        else {
            ctx.checkMask = Bitboards::Between[ctx.kingSq][checkerSq] | Bitboards::bit(checkerSq);
        }
    }
    else if (ctx.checkerCount > 1) {
        ctx.checkMask = 0;
    }

    computePins(b, ctx);
    return ctx;
}

inline void addQuietOrCapture(MoveList& moves, const board& b, int from, int to, Piece moved, Piece captured) {
    if (isKingPiece(captured)) {
        return;
    }

    int castleRights = updatedCastleRights(b.castleRights, moved, from, captured, to);
    moves.emplace_back(from, to, moved, captured, castleRights);
}

inline void addPromotionMoves(MoveList& moves, const board& b, int from, int to, Piece pawn, Piece captured) {
    static constexpr Piece whitePromos[4] = { WQ, WR, WB, WN };
    static constexpr Piece blackPromos[4] = { BQ, BR, BB, BN };

    const Piece* promos = (pawn == WP) ? whitePromos : blackPromos;
    int castleRights = updatedCastleRights(b.castleRights, pawn, from, captured, to);

    for (Piece promo : std::array<Piece, 4>{promos[0], promos[1], promos[2], promos[3]}) {
        moves.emplace_back(from, to, pawn, captured, castleRights);
        moves.back().wasPromotion = true;
        moves.back().promotedTo = promo;
    }
}

inline bool kingMoveLeavesSafe(const board& b, const GenerationContext& ctx, int to) {
    Bitboard occ = ctx.occ ^ Bitboards::bit(ctx.kingSq);
    if ((ctx.themOcc & Bitboards::bit(to)) != 0) {
        occ ^= Bitboards::bit(to);
    }
    return attackersTo(b, to, !ctx.whiteToMove, occ) == 0;
}

inline void maybeAddEnPassant(board& b, MoveList& moves, const GenerationContext& ctx, int from, bool legalOnly) {
    if (!b.hasEnPassant || b.enPassantSquare < 0) {
        return;
    }

    int epSq = b.enPassantSquare;
    if ((Bitboards::PawnAttacks[ctx.whiteToMove ? Bitboards::WHITE : Bitboards::BLACK][from]
        & Bitboards::bit(epSq)) == 0) {
        return;
    }

    if (legalOnly && ctx.checkerCount > 1) {
        return;
    }

    Move epMove(from, epSq, pawnPiece(ctx.whiteToMove), pawnPiece(!ctx.whiteToMove), b.castleRights);
    epMove.wasEnPassant = true;

    if (!legalOnly) {
        moves.push_back(epMove);
        return;
    }

    Unmove undo = b.makeMove(epMove);
    int kingSq = b.kingSquare(ctx.whiteToMove);
    if (kingSq != -1 && attackersTo(b, kingSq, !ctx.whiteToMove, b.occupied) == 0) {
        moves.push_back(epMove);
    }
    b.unmakeMove(epMove, undo);
}

void generatePawnMoves(board& b, MoveList& moves, const GenerationContext& ctx, bool legalOnly) {
    Bitboard pawns = b.pieces(pawnPiece(ctx.whiteToMove));

    while (pawns) {
        int from = Bitboards::poplsb(pawns);
        Bitboard fromMask = Bitboards::bit(from);
        Bitboard pinMask = legalOnly ? ctx.pinMasks[from] : FULL_MASK;
        int row = from >> 3;

        int oneStep = ctx.whiteToMove ? (from - 8) : (from + 8);
        if (oneStep >= 0 && oneStep < 64) {
            Bitboard oneMask = Bitboards::bit(oneStep);
            if ((ctx.occ & oneMask) == 0) {
                bool promotion = ctx.whiteToMove ? (row == 1) : (row == 6);
                if (promotion &&
                    (!legalOnly || ((pinMask & oneMask) != 0 && (ctx.checkMask & oneMask) != 0))) {
                    addPromotionMoves(moves, b, from, oneStep, pawnPiece(ctx.whiteToMove), EMPTY);
                }
                else if (!promotion &&
                    (!legalOnly || ((pinMask & oneMask) != 0 && (ctx.checkMask & oneMask) != 0))) {
                    addQuietOrCapture(moves, b, from, oneStep, pawnPiece(ctx.whiteToMove), EMPTY);
                }

                bool startRank = ctx.whiteToMove ? (row == 6) : (row == 1);
                if (startRank) {
                    int twoStep = ctx.whiteToMove ? (from - 16) : (from + 16);
                    Bitboard twoMask = Bitboards::bit(twoStep);
                    if ((ctx.occ & twoMask) == 0 &&
                        (!legalOnly || ((pinMask & twoMask) != 0 && (ctx.checkMask & twoMask) != 0))) {
                        addQuietOrCapture(moves, b, from, twoStep, pawnPiece(ctx.whiteToMove), EMPTY);
                        moves.back().hasEnPassant = true;
                        moves.back().enPassantSquare = oneStep;
                    }
                }
            }
        }

        Bitboard captures = Bitboards::PawnAttacks[ctx.whiteToMove ? Bitboards::WHITE : Bitboards::BLACK][from]
            & ctx.themOcc;
        if (legalOnly) {
            captures &= ctx.checkMask;
            captures &= pinMask;
        }

        while (captures) {
            int to = Bitboards::poplsb(captures);
            Piece captured = b.pieceAt(to);
            bool promotion = ctx.whiteToMove ? (row == 1) : (row == 6);
            if (promotion) {
                addPromotionMoves(moves, b, from, to, pawnPiece(ctx.whiteToMove), captured);
            }
            else {
                addQuietOrCapture(moves, b, from, to, pawnPiece(ctx.whiteToMove), captured);
            }
        }

        maybeAddEnPassant(b, moves, ctx, from, legalOnly);
    }
}

void generateKnightMoves(const board& b, MoveList& moves, const GenerationContext& ctx, bool legalOnly) {
    Bitboard knights = b.pieces(knightPiece(ctx.whiteToMove));
    if (legalOnly) {
        knights &= ~ctx.pinned;
    }

    while (knights) {
        int from = Bitboards::poplsb(knights);
        Bitboard targets = Bitboards::KnightAttacks[from] & ~ctx.usOcc;
        if (legalOnly) {
            targets &= ctx.checkMask;
        }

        while (targets) {
            int to = Bitboards::poplsb(targets);
            Piece captured = ((ctx.themOcc & Bitboards::bit(to)) != 0) ? b.pieceAt(to) : EMPTY;
            addQuietOrCapture(moves, b, from, to, knightPiece(ctx.whiteToMove), captured);
        }
    }
}

template <Bitboard(*AttackFn)(int, Bitboard)>
void generateSlidingMoves(const board& b, MoveList& moves, const GenerationContext& ctx, bool legalOnly, Piece piece) {
    Bitboard pieces = b.pieces(piece);

    while (pieces) {
        int from = Bitboards::poplsb(pieces);
        Bitboard targets = AttackFn(from, ctx.occ) & ~ctx.usOcc;
        if (legalOnly) {
            targets &= ctx.checkMask;
            targets &= ctx.pinMasks[from];
        }

        while (targets) {
            int to = Bitboards::poplsb(targets);
            Piece captured = ((ctx.themOcc & Bitboards::bit(to)) != 0) ? b.pieceAt(to) : EMPTY;
            addQuietOrCapture(moves, b, from, to, piece, captured);
        }
    }
}

void generateKingMoves(const board& b, MoveList& moves, const GenerationContext& ctx, bool legalOnly) {
    Bitboard targets = Bitboards::KingAttacks[ctx.kingSq] & ~ctx.usOcc;

    while (targets) {
        int to = Bitboards::poplsb(targets);
        if (legalOnly && !kingMoveLeavesSafe(b, ctx, to)) {
            continue;
        }

        Piece captured = ((ctx.themOcc & Bitboards::bit(to)) != 0) ? b.pieceAt(to) : EMPTY;
        addQuietOrCapture(moves, b, ctx.kingSq, to, kingPiece(ctx.whiteToMove), captured);
    }
}

void generateCastles(const board& b, MoveList& moves, const GenerationContext& ctx, bool legalOnly) {
    if (ctx.kingSq == -1 || (legalOnly && ctx.checkerCount != 0)) {
        return;
    }

    auto safeSquare = [&](int sq) {
        return !legalOnly || attackersTo(b, sq, !ctx.whiteToMove, ctx.occ) == 0;
    };

    if (ctx.whiteToMove && ctx.kingSq == 60) {
        if ((b.castleRights & 0b0001) != 0 &&
            b.pieceAt(63) == WR &&
            (b.occupied & (Bitboards::bit(61) | Bitboards::bit(62))) == 0 &&
            safeSquare(60) && safeSquare(61) && safeSquare(62)) {
            addQuietOrCapture(moves, b, 60, 62, WK, EMPTY);
            moves.back().wasCastling = true;
        }

        if ((b.castleRights & 0b0010) != 0 &&
            b.pieceAt(56) == WR &&
            (b.occupied & (Bitboards::bit(57) | Bitboards::bit(58) | Bitboards::bit(59))) == 0 &&
            safeSquare(60) && safeSquare(59) && safeSquare(58)) {
            addQuietOrCapture(moves, b, 60, 58, WK, EMPTY);
            moves.back().wasCastling = true;
        }
    }
    else if (!ctx.whiteToMove && ctx.kingSq == 4) {
        if ((b.castleRights & 0b0100) != 0 &&
            b.pieceAt(7) == BR &&
            (b.occupied & (Bitboards::bit(5) | Bitboards::bit(6))) == 0 &&
            safeSquare(4) && safeSquare(5) && safeSquare(6)) {
            addQuietOrCapture(moves, b, 4, 6, BK, EMPTY);
            moves.back().wasCastling = true;
        }

        if ((b.castleRights & 0b1000) != 0 &&
            b.pieceAt(0) == BR &&
            (b.occupied & (Bitboards::bit(1) | Bitboards::bit(2) | Bitboards::bit(3))) == 0 &&
            safeSquare(4) && safeSquare(3) && safeSquare(2)) {
            addQuietOrCapture(moves, b, 4, 2, BK, EMPTY);
            moves.back().wasCastling = true;
        }
    }
}

void generateMoves(board& b, MoveList& moves, bool legalOnly) {
    moves.clear();

    GenerationContext ctx = buildContext(b, legalOnly);
    if (ctx.kingSq == -1) {
        return;
    }

    generateKingMoves(b, moves, ctx, legalOnly);

    if (legalOnly && ctx.checkerCount > 1) {
        return;
    }

    if (legalOnly && ctx.checkerCount == 0 && ctx.pinned == 0 && !b.hasEnPassant) {
        generateCastles(b, moves, ctx, true);
        generatePawnMoves(b, moves, ctx, false);
        generateKnightMoves(b, moves, ctx, false);
        generateSlidingMoves<Bitboards::bishopAttacks>(b, moves, ctx, false, bishopPiece(ctx.whiteToMove));
        generateSlidingMoves<Bitboards::rookAttacks>(b, moves, ctx, false, rookPiece(ctx.whiteToMove));
        generateSlidingMoves<Bitboards::queenAttacks>(b, moves, ctx, false, queenPiece(ctx.whiteToMove));
        return;
    }

    generateCastles(b, moves, ctx, legalOnly);
    generatePawnMoves(b, moves, ctx, legalOnly);
    generateKnightMoves(b, moves, ctx, legalOnly);
    generateSlidingMoves<Bitboards::bishopAttacks>(b, moves, ctx, legalOnly, bishopPiece(ctx.whiteToMove));
    generateSlidingMoves<Bitboards::rookAttacks>(b, moves, ctx, legalOnly, rookPiece(ctx.whiteToMove));
    generateSlidingMoves<Bitboards::queenAttacks>(b, moves, ctx, legalOnly, queenPiece(ctx.whiteToMove));
}

} // namespace

MoveGenerator::MoveGenerator() {
    Bitboards::init();
}

std::vector<Move> MoveGenerator::generatePseudoLegalMoves(board& Board) {
    MoveList moves;
    generatePseudoLegalMoves(Board, moves);
    return std::vector<Move>(moves.begin(), moves.end());
}

std::vector<Move> MoveGenerator::generateLegalMoves(board& Board) {
    MoveList moves;
    generateLegalMoves(Board, moves);
    return std::vector<Move>(moves.begin(), moves.end());
}

void MoveGenerator::generatePseudoLegalMoves(board& Board, MoveList& moves) {
    generateMoves(Board, moves, false);
}

void MoveGenerator::generateLegalMoves(board& Board, MoveList& moves) {
    generateMoves(Board, moves, true);
}

bool MoveGenerator::isSquareAttacked(const board& Board, int sq, bool byWhite) {
    return attackersTo(Board, sq, byWhite, Board.occupied) != 0;
}

int MoveGenerator::findKing(const board& Board, bool white) {
    return Board.kingSquare(white);
}
