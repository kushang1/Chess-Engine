#include "EngineFacade.h"

#include <chrono>
#include <sstream>
#include <stdexcept>

#include "Board.h"
#include "Engine.h"
#include "MoveGenerator.h"
#include "Profiler.h"

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

bool movesMatch(const Move& lhs, const Move& rhs)
{
    if (lhs.from != rhs.from || lhs.to != rhs.to) {
        return false;
    }

    if (lhs.wasPromotion != rhs.wasPromotion) {
        return false;
    }

    return !lhs.wasPromotion || lhs.promotedTo == rhs.promotedTo;
}

char sanPieceLetter(Piece piece)
{
    switch (piece) {
    case WN: case BN: return 'N';
    case WB: case BB: return 'B';
    case WR: case BR: return 'R';
    case WQ: case BQ: return 'Q';
    case WK: case BK: return 'K';
    default: return '\0';
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
    : impl(new Impl())
{
}

ChessEngine::~ChessEngine()
{
    delete impl;
}

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

Piece ChessEngine::pieceAt(int square) const
{
    if (square < 0 || square >= 64) {
        return EMPTY;
    }
    return impl->position.pieceAt(square);
}

bool ChessEngine::isWhiteTurn() const
{
    return impl->position.isWhiteTurn;
}

uint64_t ChessEngine::positionHash() const
{
    return impl->position.hash;
}

std::string ChessEngine::positionKey() const
{
    std::string fen = currentFen();
    int spaces = 0;
    for (std::size_t i = 0; i < fen.size(); ++i) {
        if (fen[i] == ' ') {
            ++spaces;
            if (spaces == 4) {
                return fen.substr(0, i);
            }
        }
    }
    return fen;
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
        if (movesMatch(legal, move)) {
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
    return findBestMove(limits, std::vector<uint64_t>{ impl->position.hash });
}

SearchResult ChessEngine::findBestMove(const SearchLimits& limits, const std::vector<uint64_t>& repetitionHistory)
{
    SearchResult result;
    impl->searcher.setTimeLimitMs(limits.moveTimeMs);
    impl->searcher.setNodeLimit(limits.nodeLimit);
    impl->searcher.resetSearchStats();

    std::vector<uint64_t> repetitions = repetitionHistory;
    if (repetitions.empty()) {
        repetitions.push_back(impl->position.hash);
    }

    auto start = std::chrono::steady_clock::now();
    result.bestMove = impl->searcher.findBestMove(
        impl->position, limits.maxDepth, repetitions, limits.searchMoves);
    auto end = std::chrono::steady_clock::now();

    result.nodes = impl->searcher.nodesSearched();
    result.leafNodes = impl->searcher.leafNodesSearched();
    result.elapsedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    return result;
}

void ChessEngine::setHashSizeMb(int megabytes)
{
    impl->searcher.setHashSizeMb(megabytes);
}

void ChessEngine::clearSearchStop()
{
    impl->searcher.clearStop();
}

void ChessEngine::stopSearch()
{
    impl->searcher.requestStop();
}

int ChessEngine::evaluate() const
{
    board copy = impl->position;
    return impl->searcher.debugEvaluate(copy);
}

int ChessEngine::legacyEvaluate() const
{
    return impl->searcher.debugEvaluateLegacy(impl->position);
}

std::string ChessEngine::evaluationBreakdown() const
{
    board copy = impl->position;
    return impl->searcher.debugEvaluateBreakdown(copy);
}

PerftResult ChessEngine::perft(int depth)
{
    PROFILE_INC(::Profiler::PerftCalls);
    PROFILE_MAX(::Profiler::PerftDepth, depth);
    PROFILE_TIMER(::Profiler::PerftTime);
    PROFILE_MOVEGEN_CONTEXT(Perft);

    PerftResult result;
    board copy = impl->position;
    auto start = std::chrono::steady_clock::now();
    result.nodes = perftImpl(copy, impl->moveGenerator, depth);
    PROFILE_ADD(::Profiler::PerftNodes, result.nodes);
    auto end = std::chrono::steady_clock::now();
    result.elapsedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    return result;
}

std::vector<PerftDivideEntry> ChessEngine::divide(int depth)
{
    PROFILE_INC(::Profiler::PerftDivideCalls);
    PROFILE_MAX(::Profiler::PerftDepth, depth);
    PROFILE_TIMER(::Profiler::PerftTime);
    PROFILE_MOVEGEN_CONTEXT(Perft);

    std::vector<PerftDivideEntry> entries;
    if (depth <= 0) {
        return entries;
    }

    board copy = impl->position;
    MoveList moves;
    impl->moveGenerator.generateLegalMoves(copy, moves);
    entries.reserve(moves.count);

    long long nodes = 0;
    for (Move& move : moves) {
        Unmove undo = copy.makeMove(move);
        long long childNodes = perftImpl(copy, impl->moveGenerator, depth - 1);
        copy.unmakeMove(move, undo);
        nodes += childNodes;
        entries.push_back(PerftDivideEntry{ move, childNodes });
    }

    PROFILE_ADD(::Profiler::PerftNodes, nodes);
    return entries;
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

std::string ChessEngine::moveToUci(const Move& move) const
{
    std::string out = squareToString(move.from) + squareToString(move.to);
    if (move.wasPromotion) {
        char promotion = promotionToChar(move.promotedTo);
        if (promotion != '\0') {
            out.push_back(promotion);
        }
    }
    return out;
}

std::string ChessEngine::moveToSan(const Move& move) const
{
    MoveList moves;
    impl->moveGenerator.generateLegalMoves(impl->position, moves);

    const Move* legalMove = nullptr;
    for (const Move& legal : moves) {
        if (movesMatch(legal, move)) {
            legalMove = &legal;
            break;
        }
    }

    if (legalMove == nullptr) {
        return moveToUci(move);
    }

    board after = impl->position;
    after.makeMove(*legalMove);

    if (legalMove->wasCastling) {
        if (impl->position.isWhiteTurn) {
            return legalMove->to == 62 ? "O-O" : "O-O-O";
        }
        return legalMove->to == 6 ? "O-O" : "O-O-O";
    }

    std::string san;
    char pieceLetter = sanPieceLetter(legalMove->moved);
    if (pieceLetter != '\0') {
        san.push_back(pieceLetter);
    }

    bool isCapture = legalMove->captured != EMPTY || legalMove->wasEnPassant;
    if (pieceLetter == '\0' && isCapture) {
        san.push_back(static_cast<char>('a' + (legalMove->from & 7)));
        san.push_back('x');
    }
    else if (isCapture) {
        san.push_back('x');
    }

    san += squareToString(legalMove->to);

    if (legalMove->wasPromotion) {
        san.push_back('=');
        char promotion = sanPieceLetter(legalMove->promotedTo);
        san.push_back(promotion == '\0' ? 'Q' : promotion);
    }

    MoveList replies;
    impl->moveGenerator.generateLegalMoves(after, replies);
    int kingSq = impl->moveGenerator.findKing(after, after.isWhiteTurn);
    bool inCheck = kingSq != -1 &&
        impl->moveGenerator.isSquareAttacked(after, kingSq, !after.isWhiteTurn);

    if (replies.count == 0 && inCheck) {
        san.push_back('#');
    }
    else if (inCheck) {
        san.push_back('+');
    }

    return san;
}

GameStatus ChessEngine::gameStatus() const
{
    GameStatus status;
    status.whiteToMove = impl->position.isWhiteTurn;

    int kingSq = impl->moveGenerator.findKing(impl->position, impl->position.isWhiteTurn);
    status.inCheck = kingSq != -1 &&
        impl->moveGenerator.isSquareAttacked(impl->position, kingSq, !impl->position.isWhiteTurn);

    if (impl->position.halfmoveClock >= 100) {
        status.kind = GameStatusKind::FiftyMoveRule;
        return status;
    }

    board copy = impl->position;
    if (impl->moveGenerator.generateLegalMoves(copy).empty()) {
        status.kind = status.inCheck ? GameStatusKind::Checkmate : GameStatusKind::Stalemate;
    }

    return status;
}

bool ChessEngine::isGameOver() const
{
    return gameStatus().kind != GameStatusKind::Ongoing;
}

} // namespace chess
