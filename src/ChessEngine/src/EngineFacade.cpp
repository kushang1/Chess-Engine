#include "EngineFacade.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

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

namespace {

constexpr int MinSearchThreads = 1;
constexpr int MaxSearchThreads = 256;

bool isUsableMove(const Move& move)
{
    return move.from >= 0 && move.to >= 0;
}

} // namespace

class SearchThreadPool {
public:
    ~SearchThreadPool()
    {
        shutdown();
    }

    void resize(int count)
    {
        count = std::clamp(count, MinSearchThreads, MaxSearchThreads);
        if (count == static_cast<int>(threads.size())) {
            return;
        }

        if (!threads.empty()) {
            shutdown();
        }

        threads.reserve(count);
        for (int i = 0; i < count; ++i) {
            threads.emplace_back([this]() { threadLoop(); });
        }
    }

    void run(int count, const std::function<void(int)>& task)
    {
        count = std::clamp(count, 0, static_cast<int>(threads.size()));
        if (count <= 0) {
            return;
        }

        std::unique_lock<std::mutex> lock(mutex);
        currentTask = task;
        pending.clear();
        pending.reserve(count);
        for (int id = 0; id < count; ++id) {
            pending.push_back(id);
        }
        active = count;

        workAvailable.notify_all();
        workDone.wait(lock, [this]() { return active == 0; });
        currentTask = nullptr;
    }

private:
    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shuttingDown = true;
            pending.clear();
        }

        workAvailable.notify_all();
        for (std::thread& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();

        std::lock_guard<std::mutex> lock(mutex);
        active = 0;
        currentTask = nullptr;
        shuttingDown = false;
    }

    void threadLoop()
    {
        for (;;) {
            int workerId = -1;
            std::function<void(int)> task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                workAvailable.wait(lock, [this]() {
                    return shuttingDown || !pending.empty();
                });

                if (shuttingDown) {
                    return;
                }

                workerId = pending.back();
                pending.pop_back();
                task = currentTask;
            }

            if (task) {
                task(workerId);
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                --active;
                if (active == 0) {
                    workDone.notify_one();
                }
            }
        }
    }

    std::mutex mutex;
    std::condition_variable workAvailable;
    std::condition_variable workDone;
    std::vector<std::thread> threads;
    std::vector<int> pending;
    std::function<void(int)> currentTask;
    int active = 0;
    bool shuttingDown = false;
};

struct SearchWorker {
    explicit SearchWorker(int id,
        const std::shared_ptr<SharedTranspositionTable>& table,
        const std::shared_ptr<SharedSearchControl>& control)
        : workerId(id)
    {
        engine.moveGenerator = &moveGenerator;
        engine.setWorkerId(workerId);
        engine.setSharedTranspositionTable(table);
        engine.setSharedSearchControl(control);
        engine.setEmitUciInfo(workerId == 0);
    }

    int workerId = 0;
    MoveGenerator moveGenerator;
    Engine engine;
};

class ChessEngine::Impl {
public:
    Impl()
    {
        searcher.moveGenerator = &moveGenerator;
        searcher.setSharedTranspositionTable(table);
        searcher.setSharedSearchControl(control);
        ensureThreadCount(1);
    }

    struct WorkerOutput {
        Move bestMove;
        int score = 0;
        int depth = 0;
        long long nodes = 0;
        long long leafNodes = 0;
        bool valid = false;
    };

    void ensureThreadCount(int requested)
    {
        requested = std::clamp(requested, MinSearchThreads, MaxSearchThreads);
        while (static_cast<int>(workers.size()) < requested) {
            int id = static_cast<int>(workers.size());
            workers.push_back(std::make_unique<SearchWorker>(id, table, control));
        }
        threadPool.resize(requested);
        configuredThreads = requested;
    }

    void configureHash(int megabytes)
    {
        hashSizeMb = megabytes;
        table->resize(hashSizeMb);
    }

    WorkerOutput runWorker(int workerId, const SearchLimits& limits,
        const std::vector<uint64_t>& repetitions)
    {
        SearchWorker& worker = *workers[workerId];
        worker.engine.setWorkerId(workerId);
        worker.engine.setEmitUciInfo(workerId == 0);
        worker.engine.setSharedTranspositionTable(table);
        worker.engine.setSharedSearchControl(control);
        worker.engine.setTimeLimitMs(limits.moveTimeMs);
        worker.engine.setNodeLimit(limits.nodeLimit);
        worker.engine.clearStop();

        board workerPosition = position;
        WorkerOutput output;
        output.bestMove = worker.engine.findBestMove(
            workerPosition, limits.maxDepth, repetitions, limits.searchMoves);
        output.score = worker.engine.lastSearchScore();
        output.depth = worker.engine.lastSearchDepth();
        output.nodes = worker.engine.nodesSearched();
        output.leafNodes = worker.engine.leafNodesSearched();
        output.valid = isUsableMove(output.bestMove);
        return output;
    }

    int chooseBestWorker(const std::vector<WorkerOutput>& outputs) const
    {
        int best = 0;
        for (int i = 1; i < static_cast<int>(outputs.size()); ++i) {
            const WorkerOutput& candidate = outputs[i];
            const WorkerOutput& current = outputs[best];
            if (!candidate.valid) {
                continue;
            }
            if (!current.valid) {
                best = i;
                continue;
            }
            if (candidate.depth > current.depth) {
                best = i;
                continue;
            }
            if (candidate.depth == current.depth &&
                candidate.score > current.score) {
                best = i;
            }
        }
        return best;
    }

    board position;
    MoveGenerator moveGenerator;
    Engine searcher;
    std::shared_ptr<SharedTranspositionTable> table =
        std::make_shared<SharedTranspositionTable>();
    std::shared_ptr<SharedSearchControl> control =
        std::make_shared<SharedSearchControl>();
    std::vector<std::unique_ptr<SearchWorker>> workers;
    SearchThreadPool threadPool;
    int configuredThreads = 1;
    int hashSizeMb = 128;
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
    const int activeThreads = std::clamp(
        impl->configuredThreads, MinSearchThreads, MaxSearchThreads);
    impl->ensureThreadCount(activeThreads);

    std::vector<uint64_t> repetitions = repetitionHistory;
    if (repetitions.empty()) {
        repetitions.push_back(impl->position.hash);
    }

    impl->control->start(limits.moveTimeMs, limits.nodeLimit);
    auto start = std::chrono::steady_clock::now();

    std::vector<Impl::WorkerOutput> outputs(activeThreads);
    if (activeThreads == 1) {
        outputs[0] = impl->runWorker(0, limits, repetitions);
    }
    else {
        impl->threadPool.run(activeThreads, [&](int workerId) {
            outputs[workerId] = impl->runWorker(workerId, limits, repetitions);
        });
    }

    auto end = std::chrono::steady_clock::now();

    const int bestWorker = impl->chooseBestWorker(outputs);
    result.bestMove = outputs[bestWorker].bestMove;
    result.bestScore = outputs[bestWorker].score;
    result.completedDepth = outputs[bestWorker].depth;
    result.nodes = impl->control->nodesSearched();
    result.leafNodes = 0;
    result.threads = activeThreads;
    result.workerNodes.reserve(outputs.size());
    for (const Impl::WorkerOutput& output : outputs) {
        result.leafNodes += output.leafNodes;
        result.workerNodes.push_back(output.nodes);
    }
    result.elapsedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    return result;
}

void ChessEngine::setHashSizeMb(int megabytes)
{
    impl->configureHash(megabytes);
    impl->searcher.setSharedTranspositionTable(impl->table);
    for (const std::unique_ptr<SearchWorker>& worker : impl->workers) {
        worker->engine.setSharedTranspositionTable(impl->table);
    }
}

void ChessEngine::setThreadCount(int threads)
{
    impl->ensureThreadCount(threads);
}

int ChessEngine::threadCount() const
{
    return impl->configuredThreads;
}

void ChessEngine::clearSearchStop()
{
    impl->control->clearStop();
    impl->searcher.clearStop();
    for (const std::unique_ptr<SearchWorker>& worker : impl->workers) {
        worker->engine.clearStop();
    }
}

void ChessEngine::stopSearch()
{
    impl->control->requestStop();
    impl->searcher.requestStop();
    for (const std::unique_ptr<SearchWorker>& worker : impl->workers) {
        worker->engine.requestStop();
    }
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
