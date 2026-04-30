#pragma once

#include <memory>
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

    std::vector<Move> legalMoves() const;

    bool makeMove(const Move& move);
    bool makeMoveUci(const std::string& uciMove);

    SearchResult findBestMove(const SearchLimits& limits);

    PerftResult perft(int depth);

    std::string currentFen() const;
    bool isGameOver() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace chess
