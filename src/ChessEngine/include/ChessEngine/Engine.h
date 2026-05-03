#ifndef ENGINE_H
#define ENGINE_H

#include "Board.h"
#include "ChessApi.h"
#include "MoveGenerator.h"
#include "ZobristHashing.h"
#include <atomic>
#include <thread>
#include <vector>
#include "Syzygy.h"
#include <chrono>
#include "Polyglot.h"
#include <unordered_map>

class CHESS_API Engine {
public:

	int maxDepth = 128;
    Move findBestMove(board& b, int depth,
        const std::vector<uint64_t>& globalReps);
    MoveGenerator* moveGenerator;
    Engine();
    ~Engine(); 
    long long nodesSearched() const;
    long long leafNodesSearched() const;
    void resetSearchStats();
    void setTimeLimitMs(int milliseconds);
    void setHashSizeMb(int megabytes);
    void clearStop();
    void requestStop();
private:
    int evaluate(board &b);
    static const int MAX_DEPTH = 64;

    Move killerMoves[2][MAX_DEPTH];
    int historyHeuristic[64][64];


    int search(board& b, int depth, int alpha, int beta,
        std::vector<uint64_t>& repHistory);

    int scoreMove(const Move& m, const board& b, int depth);

    int quiescence(board& b, int alpha, int beta);

	std::unordered_map<uint64_t, int> repTable;

    uint64_t computeHash(const board& b) {
        return b.hash;
    }

    // --- TT STRUCTS ---
    enum TTFlag : uint8_t {
        TT_EMPTY = 0,
        TT_EXACT = 1,
        TT_ALPHA = 2,
        TT_BETA = 3
    };

    struct TTEntry {
        uint64_t key = 0;
        int score = 0;
        int depth = -1;
        TTFlag flag = TT_EMPTY;
        Move bestMove;
    };

    TTEntry* tt = nullptr;
    uint64_t ttSize = 0;
    uint64_t ttMask = 0;

    void resizeTranspositionTable(int megabytes);

    std::atomic<bool> stopSearch;
    std::chrono::steady_clock::time_point searchStart;
    int timeLimitMs = 1000;  // default: 5 seconds per move

    std::vector<PolyglotEntry> openingBook;
    bool externalDataInitialized = false;

    void initializeExternalData();
    bool loadOpeningBook(const std::string& filename);
    Move probeBook(board& b);

};

#endif
