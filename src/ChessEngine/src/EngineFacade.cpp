#include "EngineFacade.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>

#include "Board.h"
#include "Engine.h"
#include "MoveGenerator.h"

namespace {

char pieceToFen(Piece piece)
{
    switch (piece) {
    case WP: return 'P';
    case WN: return 'N';
    case WB: return 'B';
    case WR: return 'R';
    case WQ: return 'Q';
    case WK: return 'K';
    case BP: return 'p';
    case BN: return 'n';
    case BB: return 'b';
    case BR: return 'r';
    case BQ: return 'q';
    case BK: return 'k';
    default: return '\0';
    }
}

char promotionToChar(Piece piece)
{
    switch (piece) {
    case WQ: case BQ: return 'q';
    case WR: case BR: return 'r';
    case WB: case BB: return 'b';
    case WN: case BN: return 'n';
    default: return '\0';
    }
}

Piece promotionFromChar(char ch, bool white)
{
    switch (ch) {
    case 'q': case 'Q': return white ? WQ : BQ;
    case 'r': case 'R': return white ? WR : BR;
    case 'b': case 'B': return white ? WB : BB;
    case 'n': case 'N': return white ? WN : BN;
    default: return EMPTY;
    }
}

std::string squareToString(int square)
{
    std::string out;
    out.push_back(static_cast<char>('a' + (square & 7)));
    out.push_back(static_cast<char>('8' - (square >> 3)));
    return out;
}

int squareFromString(const std::string& text, std::size_t offset)
{
    if (offset + 1 >= text.size()) {
        return -1;
    }

    char file = text[offset];
    char rank = text[offset + 1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return -1;
    }

    int col = file - 'a';
    int row = 8 - (rank - '0');
    return (row << 3) | col;
}

long long perftImpl(board& position, MoveGenerator& moveGenerator, int depth)
{
    if (depth == 0) {
        return 1;
    }

    if (depth == 1) {
        return moveGenerator.countLegalMoves(position);
    }

    long long nodes = 0;
    MoveList moves;
    moveGenerator.generateLegalMoves(position, moves);

    if (depth == 2) {
        for (Move& move : moves) {
            Unmove undo = position.makeMove(move);
            nodes += moveGenerator.countLegalMoves(position);
            position.unmakeMove(move, undo);
        }
        return nodes;
    }

    for (Move& move : moves) {
        Unmove undo = position.makeMove(move);
        nodes += perftImpl(position, moveGenerator, depth - 1);
        position.unmakeMove(move, undo);
    }

    return nodes;
}

} // namespace

namespace chess {

class ChessEngine::Impl {
public:
    Impl()
    {
        searcher.moveGenerator = &moveGenerator;
    }

    board position;
    MoveGenerator moveGenerator;
    Engine searcher;
};

ChessEngine::ChessEngine()
    : impl(std::make_unique<Impl>())
{
}

ChessEngine::~ChessEngine() = default;

void ChessEngine::newGame()
{
    impl->position.resetBoard();
}

bool ChessEngine::setPositionFromFen(const std::string& fen)
{
    try {
        impl->position.loadFEN(fen);
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

std::vector<Move> ChessEngine::legalMoves() const
{
    board copy = impl->position;
    return impl->moveGenerator.generateLegalMoves(copy);
}

bool ChessEngine::makeMove(const Move& move)
{
    MoveList moves;
    impl->moveGenerator.generateLegalMoves(impl->position, moves);
    for (const Move& legal : moves) {
        bool sameMove = legal.from == move.from &&
            legal.to == move.to &&
            legal.wasPromotion == move.wasPromotion;

        if (sameMove && legal.wasPromotion) {
            sameMove = legal.promotedTo == move.promotedTo;
        }

        if (sameMove) {
            impl->position.makeMove(legal);
            return true;
        }
    }
    return false;
}

bool ChessEngine::makeMoveUci(const std::string& uciMove)
{
    if (uciMove.size() < 4) {
        return false;
    }

    int from = squareFromString(uciMove, 0);
    int to = squareFromString(uciMove, 2);
    if (from < 0 || to < 0) {
        return false;
    }

    Piece promotion = uciMove.size() >= 5
        ? promotionFromChar(uciMove[4], impl->position.isWhiteTurn)
        : EMPTY;

    MoveList moves;
    impl->moveGenerator.generateLegalMoves(impl->position, moves);
    for (const Move& legal : moves) {
        if (legal.from != from || legal.to != to) {
            continue;
        }
        if (legal.wasPromotion && legal.promotedTo != promotion) {
            continue;
        }
        if (!legal.wasPromotion && promotion != EMPTY) {
            continue;
        }
        impl->position.makeMove(legal);
        return true;
    }

    return false;
}

SearchResult ChessEngine::findBestMove(const SearchLimits& limits)
{
    SearchResult result;
    impl->searcher.setTimeLimitMs(limits.moveTimeMs);
    impl->searcher.resetSearchStats();

    std::vector<uint64_t> repetitions;
    repetitions.push_back(impl->position.hash);

    auto start = std::chrono::steady_clock::now();
    result.bestMove = impl->searcher.findBestMove(impl->position, limits.maxDepth, repetitions);
    auto end = std::chrono::steady_clock::now();

    result.nodes = impl->searcher.nodesSearched();
    result.leafNodes = impl->searcher.leafNodesSearched();
    result.elapsedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    return result;
}

PerftResult ChessEngine::perft(int depth)
{
    PerftResult result;
    board copy = impl->position;
    auto start = std::chrono::steady_clock::now();
    result.nodes = perftImpl(copy, impl->moveGenerator, depth);
    auto end = std::chrono::steady_clock::now();
    result.elapsedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    return result;
}

std::string ChessEngine::currentFen() const
{
    std::ostringstream fen;
    for (int row = 0; row < 8; ++row) {
        int empty = 0;
        for (int col = 0; col < 8; ++col) {
            Piece piece = impl->position.pieceAt((row << 3) | col);
            char fenPiece = pieceToFen(piece);
            if (fenPiece == '\0') {
                ++empty;
                continue;
            }
            if (empty != 0) {
                fen << empty;
                empty = 0;
            }
            fen << fenPiece;
        }
        if (empty != 0) {
            fen << empty;
        }
        if (row != 7) {
            fen << '/';
        }
    }

    fen << (impl->position.isWhiteTurn ? " w " : " b ");

    std::string castling;
    if ((impl->position.castleRights & 0b0001) != 0) castling.push_back('K');
    if ((impl->position.castleRights & 0b0010) != 0) castling.push_back('Q');
    if ((impl->position.castleRights & 0b0100) != 0) castling.push_back('k');
    if ((impl->position.castleRights & 0b1000) != 0) castling.push_back('q');
    fen << (castling.empty() ? "-" : castling);
    fen << ' ';

    fen << (impl->position.hasEnPassant
        ? squareToString(impl->position.enPassantSquare)
        : "-");

    fen << ' ' << impl->position.halfmoveClock;
    fen << ' ' << impl->position.fullmoveNumber;
    return fen.str();
}

bool ChessEngine::isGameOver() const
{
    board copy = impl->position;
    return impl->moveGenerator.generateLegalMoves(copy).empty();
}

} // namespace chess
