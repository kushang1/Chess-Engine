#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChessApi.h"
#include "Move.h"
#include "SearchLimits.h"
#include "SearchResult.h"

namespace chess {

class CHESS_API ChessEngine {
public:
    ChessEngine();
    ~ChessEngine();

    ChessEngine(const ChessEngine&) = delete;
    ChessEngine& operator=(const ChessEngine&) = delete;

    void newGame();
    bool setPositionFromFen(const std::string& fen);

    Piece pieceAt(int square) const;
    bool isWhiteTurn() const;
    uint64_t positionHash() const;
    std::string positionKey() const;

    std::vector<Move> legalMoves() const;

    bool makeMove(const Move& move);
    bool makeMoveUci(const std::string& uciMove);

    SearchResult findBestMove(const SearchLimits& limits);
    SearchResult findBestMove(const SearchLimits& limits, const std::vector<uint64_t>& repetitionHistory);
    void setHashSizeMb(int megabytes);
    void setThreadCount(int threads);
    int threadCount() const;
    void clearSearchStop();
    void stopSearch();
    int evaluate() const;
    int legacyEvaluate() const;
    std::string evaluationBreakdown() const;

    PerftResult perft(int depth);
    std::vector<PerftDivideEntry> divide(int depth);

    std::string currentFen() const;
    std::string moveToUci(const Move& move) const;
    std::string moveToSan(const Move& move) const;
    GameStatus gameStatus() const;
    bool isGameOver() const;

private:
    class Impl;
    Impl* impl = nullptr;
};

} // namespace chess
