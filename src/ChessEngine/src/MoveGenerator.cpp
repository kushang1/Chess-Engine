#include "MoveGenerator.h"

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

void generateMoves(board& b, MoveList& moves, bool legalOnly);

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
        Bitboard pinMask = (legalOnly && ((ctx.pinned & fromMask) != 0)) ? ctx.pinMasks[from] : FULL_MASK;
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
        Bitboard fromMask = Bitboards::bit(from);
        Bitboard targets = AttackFn(from, ctx.occ) & ~ctx.usOcc;
        if (legalOnly) {
            targets &= ctx.checkMask;
            if ((ctx.pinned & fromMask) != 0) {
                targets &= ctx.pinMasks[from];
            }
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

bool quietMoveGivesCheck(const board& b, const GenerationContext& ctx, Piece moved, int from, int to) {
    int enemyKingSq = b.kingSquare(!ctx.whiteToMove);
    if (enemyKingSq == -1) {
        return false;
    }

    Bitboard enemyKing = Bitboards::bit(enemyKingSq);
    Bitboard fromMask = Bitboards::bit(from);
    Bitboard toMask = Bitboards::bit(to);
    Bitboard occAfter = (ctx.occ ^ fromMask) | toMask;

    switch (moved) {
    case WP:
    case BP:
        if ((Bitboards::PawnAttacks[ctx.whiteToMove ? Bitboards::WHITE : Bitboards::BLACK][to] &
            enemyKing) != 0) {
            return true;
        }
        break;
    case WN:
    case BN:
        if ((Bitboards::KnightAttacks[to] & enemyKing) != 0) {
            return true;
        }
        break;
    case WB:
    case BB:
        if ((Bitboards::bishopAttacks(to, occAfter) & enemyKing) != 0) {
            return true;
        }
        break;
    case WR:
    case BR:
        if ((Bitboards::rookAttacks(to, occAfter) & enemyKing) != 0) {
            return true;
        }
        break;
    case WQ:
    case BQ:
        if ((Bitboards::queenAttacks(to, occAfter) & enemyKing) != 0) {
            return true;
        }
        break;
    case WK:
    case BK:
        if ((Bitboards::KingAttacks[to] & enemyKing) != 0) {
            return true;
        }
        break;
    default:
        break;
    }

    Bitboard discoveredBishops = bishopQueens(b, ctx.whiteToMove) & ~fromMask;
    if ((Bitboards::bishopAttacks(enemyKingSq, occAfter) & discoveredBishops) != 0) {
        return true;
    }

    Bitboard discoveredRooks = rookQueens(b, ctx.whiteToMove) & ~fromMask;
    return (Bitboards::rookAttacks(enemyKingSq, occAfter) & discoveredRooks) != 0;
}

void generateQuiescencePawnMoves(board& b, MoveList& moves, const GenerationContext& ctx) {
    Piece pawn = pawnPiece(ctx.whiteToMove);
    Bitboard pawns = b.pieces(pawn);

    while (pawns) {
        int from = Bitboards::poplsb(pawns);
        Bitboard fromMask = Bitboards::bit(from);
        Bitboard pinMask = ((ctx.pinned & fromMask) != 0) ? ctx.pinMasks[from] : FULL_MASK;
        int row = from >> 3;

        int oneStep = ctx.whiteToMove ? (from - 8) : (from + 8);
        if (oneStep >= 0 && oneStep < 64) {
            Bitboard oneMask = Bitboards::bit(oneStep);
            if ((ctx.occ & oneMask) == 0 && (pinMask & oneMask) != 0) {
                bool promotion = ctx.whiteToMove ? (row == 1) : (row == 6);
                if (promotion) {
                    addPromotionMoves(moves, b, from, oneStep, pawn, EMPTY);
                }
                else if (quietMoveGivesCheck(b, ctx, pawn, from, oneStep)) {
                    addQuietOrCapture(moves, b, from, oneStep, pawn, EMPTY);
                }

                bool startRank = ctx.whiteToMove ? (row == 6) : (row == 1);
                if (!promotion && startRank) {
                    int twoStep = ctx.whiteToMove ? (from - 16) : (from + 16);
                    Bitboard twoMask = Bitboards::bit(twoStep);
                    if ((ctx.occ & twoMask) == 0 &&
                        (pinMask & twoMask) != 0 &&
                        quietMoveGivesCheck(b, ctx, pawn, from, twoStep)) {
                        addQuietOrCapture(moves, b, from, twoStep, pawn, EMPTY);
                        moves.back().hasEnPassant = true;
                        moves.back().enPassantSquare = oneStep;
                    }
                }
            }
        }

        Bitboard captures = Bitboards::PawnAttacks[ctx.whiteToMove ? Bitboards::WHITE : Bitboards::BLACK][from]
            & ctx.themOcc
            & pinMask;

        while (captures) {
            int to = Bitboards::poplsb(captures);
            Piece captured = b.pieceAt(to);
            bool promotion = ctx.whiteToMove ? (row == 1) : (row == 6);
            if (promotion) {
                addPromotionMoves(moves, b, from, to, pawn, captured);
            }
            else {
                addQuietOrCapture(moves, b, from, to, pawn, captured);
            }
        }

        maybeAddEnPassant(b, moves, ctx, from, true);
    }
}

void generateQuiescenceKnightMoves(const board& b, MoveList& moves, const GenerationContext& ctx) {
    Piece knight = knightPiece(ctx.whiteToMove);
    Bitboard knights = b.pieces(knight) & ~ctx.pinned;

    while (knights) {
        int from = Bitboards::poplsb(knights);
        Bitboard targets = Bitboards::KnightAttacks[from] & ~ctx.usOcc;
        Bitboard captures = targets & ctx.themOcc;
        while (captures) {
            int to = Bitboards::poplsb(captures);
            addQuietOrCapture(moves, b, from, to, knight, b.pieceAt(to));
        }

        Bitboard quiets = targets & ~ctx.occ;
        while (quiets) {
            int to = Bitboards::poplsb(quiets);
            if (quietMoveGivesCheck(b, ctx, knight, from, to)) {
                addQuietOrCapture(moves, b, from, to, knight, EMPTY);
            }
        }
    }
}

template <Bitboard(*AttackFn)(int, Bitboard)>
void generateQuiescenceSlidingMoves(const board& b, MoveList& moves, const GenerationContext& ctx, Piece piece) {
    Bitboard pieces = b.pieces(piece);

    while (pieces) {
        int from = Bitboards::poplsb(pieces);
        Bitboard fromMask = Bitboards::bit(from);
        Bitboard targets = AttackFn(from, ctx.occ) & ~ctx.usOcc;
        if ((ctx.pinned & fromMask) != 0) {
            targets &= ctx.pinMasks[from];
        }

        Bitboard captures = targets & ctx.themOcc;
        while (captures) {
            int to = Bitboards::poplsb(captures);
            addQuietOrCapture(moves, b, from, to, piece, b.pieceAt(to));
        }

        Bitboard quiets = targets & ~ctx.occ;
        while (quiets) {
            int to = Bitboards::poplsb(quiets);
            if (quietMoveGivesCheck(b, ctx, piece, from, to)) {
                addQuietOrCapture(moves, b, from, to, piece, EMPTY);
            }
        }
    }
}

void generateQuiescenceKingMoves(const board& b, MoveList& moves, const GenerationContext& ctx) {
    Bitboard targets = Bitboards::KingAttacks[ctx.kingSq] & ~ctx.usOcc;

    while (targets) {
        int to = Bitboards::poplsb(targets);
        if (!kingMoveLeavesSafe(b, ctx, to)) {
            continue;
        }

        Piece captured = ((ctx.themOcc & Bitboards::bit(to)) != 0) ? b.pieceAt(to) : EMPTY;
        if (captured != EMPTY || quietMoveGivesCheck(b, ctx, kingPiece(ctx.whiteToMove), ctx.kingSq, to)) {
            addQuietOrCapture(moves, b, ctx.kingSq, to, kingPiece(ctx.whiteToMove), captured);
        }
    }
}

void generateQuiescenceMoveList(board& b, MoveList& moves) {
    moves.clear();

    GenerationContext ctx = buildContext(b, true);
    if (ctx.kingSq == -1) {
        return;
    }

    if (ctx.checkerCount != 0) {
        generateMoves(b, moves, true);
        return;
    }

    generateQuiescenceKingMoves(b, moves, ctx);
    generateQuiescencePawnMoves(b, moves, ctx);
    generateQuiescenceKnightMoves(b, moves, ctx);
    generateQuiescenceSlidingMoves<Bitboards::bishopAttacks>(b, moves, ctx, bishopPiece(ctx.whiteToMove));
    generateQuiescenceSlidingMoves<Bitboards::rookAttacks>(b, moves, ctx, rookPiece(ctx.whiteToMove));
    generateQuiescenceSlidingMoves<Bitboards::queenAttacks>(b, moves, ctx, queenPiece(ctx.whiteToMove));
}

int countEnPassant(board& b, const GenerationContext& ctx, int from, bool legalOnly) {
    if (!b.hasEnPassant || b.enPassantSquare < 0) {
        return 0;
    }

    int epSq = b.enPassantSquare;
    if ((Bitboards::PawnAttacks[ctx.whiteToMove ? Bitboards::WHITE : Bitboards::BLACK][from]
        & Bitboards::bit(epSq)) == 0) {
        return 0;
    }

    if (legalOnly && ctx.checkerCount > 1) {
        return 0;
    }

    if (!legalOnly) {
        return 1;
    }

    Move epMove(from, epSq, pawnPiece(ctx.whiteToMove), pawnPiece(!ctx.whiteToMove), b.castleRights);
    epMove.wasEnPassant = true;

    Unmove undo = b.makeMove(epMove);
    int kingSq = b.kingSquare(ctx.whiteToMove);
    int legal = (kingSq != -1 && attackersTo(b, kingSq, !ctx.whiteToMove, b.occupied) == 0) ? 1 : 0;
    b.unmakeMove(epMove, undo);
    return legal;
}

int countPawnMoves(board& b, const GenerationContext& ctx, bool legalOnly) {
    int count = 0;
    Bitboard pawns = b.pieces(pawnPiece(ctx.whiteToMove));
    Bitboard enemyKing = b.pieces(kingPiece(!ctx.whiteToMove));

    while (pawns) {
        int from = Bitboards::poplsb(pawns);
        Bitboard fromMask = Bitboards::bit(from);
        Bitboard pinMask = (legalOnly && ((ctx.pinned & fromMask) != 0)) ? ctx.pinMasks[from] : FULL_MASK;
        int row = from >> 3;

        int oneStep = ctx.whiteToMove ? (from - 8) : (from + 8);
        if (oneStep >= 0 && oneStep < 64) {
            Bitboard oneMask = Bitboards::bit(oneStep);
            if ((ctx.occ & oneMask) == 0) {
                bool promotion = ctx.whiteToMove ? (row == 1) : (row == 6);
                if ((!legalOnly || ((pinMask & oneMask) != 0 && (ctx.checkMask & oneMask) != 0))) {
                    count += promotion ? 4 : 1;
                }

                bool startRank = ctx.whiteToMove ? (row == 6) : (row == 1);
                if (startRank) {
                    int twoStep = ctx.whiteToMove ? (from - 16) : (from + 16);
                    Bitboard twoMask = Bitboards::bit(twoStep);
                    if ((ctx.occ & twoMask) == 0 &&
                        (!legalOnly || ((pinMask & twoMask) != 0 && (ctx.checkMask & twoMask) != 0))) {
                        ++count;
                    }
                }
            }
        }

        Bitboard captures = Bitboards::PawnAttacks[ctx.whiteToMove ? Bitboards::WHITE : Bitboards::BLACK][from]
            & ctx.themOcc & ~enemyKing;
        if (legalOnly) {
            captures &= ctx.checkMask;
            captures &= pinMask;
        }

        bool capturePromotion = ctx.whiteToMove ? (row == 1) : (row == 6);
        count += Bitboards::popcount(captures) * (capturePromotion ? 4 : 1);
        count += countEnPassant(b, ctx, from, legalOnly);
    }

    return count;
}

int countKnightMoves(const board& b, const GenerationContext& ctx, bool legalOnly) {
    Bitboard knights = b.pieces(knightPiece(ctx.whiteToMove));
    Bitboard enemyKing = b.pieces(kingPiece(!ctx.whiteToMove));
    if (legalOnly) {
        knights &= ~ctx.pinned;
    }

    int count = 0;
    while (knights) {
        int from = Bitboards::poplsb(knights);
        Bitboard targets = Bitboards::KnightAttacks[from] & ~ctx.usOcc & ~enemyKing;
        if (legalOnly) {
            targets &= ctx.checkMask;
        }
        count += Bitboards::popcount(targets);
    }
    return count;
}

template <Bitboard(*AttackFn)(int, Bitboard)>
int countSlidingMoves(const board& b, const GenerationContext& ctx, bool legalOnly, Piece piece) {
    Bitboard pieces = b.pieces(piece);
    Bitboard enemyKing = b.pieces(kingPiece(!ctx.whiteToMove));
    int count = 0;

    while (pieces) {
        int from = Bitboards::poplsb(pieces);
        Bitboard fromMask = Bitboards::bit(from);
        Bitboard targets = AttackFn(from, ctx.occ) & ~ctx.usOcc & ~enemyKing;
        if (legalOnly) {
            targets &= ctx.checkMask;
            if ((ctx.pinned & fromMask) != 0) {
                targets &= ctx.pinMasks[from];
            }
        }
        count += Bitboards::popcount(targets);
    }

    return count;
}

int countKingMoves(const board& b, const GenerationContext& ctx, bool legalOnly) {
    Bitboard targets = Bitboards::KingAttacks[ctx.kingSq]
        & ~ctx.usOcc
        & ~b.pieces(kingPiece(!ctx.whiteToMove));
    int count = 0;

    while (targets) {
        int to = Bitboards::poplsb(targets);
        if (!legalOnly || kingMoveLeavesSafe(b, ctx, to)) {
            ++count;
        }
    }

    return count;
}

int countCastles(const board& b, const GenerationContext& ctx, bool legalOnly) {
    if (ctx.kingSq == -1 || (legalOnly && ctx.checkerCount != 0)) {
        return 0;
    }

    auto safeSquare = [&](int sq) {
        return !legalOnly || attackersTo(b, sq, !ctx.whiteToMove, ctx.occ) == 0;
    };

    int count = 0;
    if (ctx.whiteToMove && ctx.kingSq == 60) {
        if ((b.castleRights & 0b0001) != 0 &&
            b.pieceAt(63) == WR &&
            (b.occupied & (Bitboards::bit(61) | Bitboards::bit(62))) == 0 &&
            safeSquare(60) && safeSquare(61) && safeSquare(62)) {
            ++count;
        }

        if ((b.castleRights & 0b0010) != 0 &&
            b.pieceAt(56) == WR &&
            (b.occupied & (Bitboards::bit(57) | Bitboards::bit(58) | Bitboards::bit(59))) == 0 &&
            safeSquare(60) && safeSquare(59) && safeSquare(58)) {
            ++count;
        }
    }
    else if (!ctx.whiteToMove && ctx.kingSq == 4) {
        if ((b.castleRights & 0b0100) != 0 &&
            b.pieceAt(7) == BR &&
            (b.occupied & (Bitboards::bit(5) | Bitboards::bit(6))) == 0 &&
            safeSquare(4) && safeSquare(5) && safeSquare(6)) {
            ++count;
        }

        if ((b.castleRights & 0b1000) != 0 &&
            b.pieceAt(0) == BR &&
            (b.occupied & (Bitboards::bit(1) | Bitboards::bit(2) | Bitboards::bit(3))) == 0 &&
            safeSquare(4) && safeSquare(3) && safeSquare(2)) {
            ++count;
        }
    }

    return count;
}

int countMoves(board& b, bool legalOnly) {
    GenerationContext ctx = buildContext(b, legalOnly);
    if (ctx.kingSq == -1) {
        return 0;
    }

    int count = countKingMoves(b, ctx, legalOnly);
    if (legalOnly && ctx.checkerCount > 1) {
        return count;
    }

    if (legalOnly && ctx.checkerCount == 0 && ctx.pinned == 0 && !b.hasEnPassant) {
        count += countCastles(b, ctx, true);
        count += countPawnMoves(b, ctx, false);
        count += countKnightMoves(b, ctx, false);
        count += countSlidingMoves<Bitboards::bishopAttacks>(b, ctx, false, bishopPiece(ctx.whiteToMove));
        count += countSlidingMoves<Bitboards::rookAttacks>(b, ctx, false, rookPiece(ctx.whiteToMove));
        count += countSlidingMoves<Bitboards::queenAttacks>(b, ctx, false, queenPiece(ctx.whiteToMove));
        return count;
    }

    count += countCastles(b, ctx, legalOnly);
    count += countPawnMoves(b, ctx, legalOnly);
    count += countKnightMoves(b, ctx, legalOnly);
    count += countSlidingMoves<Bitboards::bishopAttacks>(b, ctx, legalOnly, bishopPiece(ctx.whiteToMove));
    count += countSlidingMoves<Bitboards::rookAttacks>(b, ctx, legalOnly, rookPiece(ctx.whiteToMove));
    count += countSlidingMoves<Bitboards::queenAttacks>(b, ctx, legalOnly, queenPiece(ctx.whiteToMove));
    return count;
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

void MoveGenerator::generateQuiescenceMoves(board& Board, MoveList& moves) {
    generateQuiescenceMoveList(Board, moves);
}

int MoveGenerator::countLegalMoves(board& Board) {
    return countMoves(Board, true);
}

bool MoveGenerator::isSquareAttacked(const board& Board, int sq, bool byWhite) {
    return attackersTo(Board, sq, byWhite, Board.occupied) != 0;
}

int MoveGenerator::findKing(const board& Board, bool white) {
    return Board.kingSquare(white);
}

bool MoveGenerator::isKinginCheck(const board& Board, bool white) {
    int KingSquare = findKing(Board, white);
    // A king is in check when the opponent attacks its square.
    return KingSquare != -1 && isSquareAttacked(Board, KingSquare, !white);
}
