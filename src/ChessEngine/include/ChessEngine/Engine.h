#ifndef ENGINE_H
#define ENGINE_H

#include "Board.h"
#include "ChessApi.h"
#include "MoveGenerator.h"
#include "ZobristHashing.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "Syzygy.h"
#include <chrono>
#include "Polyglot.h"
#include <cstdint>
#include <unordered_map>

class CHESS_API Engine {
public:

	int maxDepth = 128;
    Move findBestMove(board& b, int depth,
        const std::vector<uint64_t>& globalReps,
        const std::vector<Move>& rootMoveFilter);
    MoveGenerator* moveGenerator;
    Engine();
    ~Engine(); 
    long long nodesSearched() const;
    long long leafNodesSearched() const;
    void resetSearchStats();
    void setTimeLimitMs(int milliseconds);
    void setNodeLimit(long long nodes);
    void setHashSizeMb(int megabytes);
    void clearStop();
    void requestStop();
    int debugEvaluate(board& b);
    int debugEvaluateLegacy(const board& b) const;
    std::string debugEvaluateBreakdown(board& b);
private:
    int evaluate(board &b);
    static const int MAX_DEPTH = 64;

    Move killerMoves[MAX_DEPTH][2];
    int historyHeuristic[2][64][64];


    int search(board& b, int depth, int alpha, int beta, int ply,
        std::vector<uint64_t>& repHistory, bool pvNode, int extensionCount);

    int scoreMove(const Move& m, const board& b, int ply,
        bool haveTTMove, const Move& ttMove);

    int quiescence(board& b, int alpha, int beta, int ply, int qply,
        std::vector<uint64_t>& repHistory);

	std::unordered_map<uint64_t, int> repTable;

    uint64_t computeHash(const board& b) {
        return b.hash;
    }

    enum class TTBound : uint8_t {
        Empty = 0,
        Exact = 1,
        Lower = 2,
        Upper = 3
    };

    static constexpr int TT_NO_STATIC_EVAL = -32768;

    struct alignas(16) TTEntry {
        uint16_t key16 = 0;
        uint16_t move16 = 0;
        int16_t score = 0;
        int16_t staticEval = TT_NO_STATIC_EVAL;
        uint8_t depth = 0;
        uint8_t generationBound = 0;
        uint16_t reserved = 0;
    };

    static_assert(sizeof(TTEntry) == 16, "TTEntry must stay 16 bytes");

    struct alignas(64) TTCluster {
        TTEntry entries[4];
    };

    static_assert(sizeof(TTCluster) == 64, "TTCluster must stay one cache line");
    static_assert(alignof(TTCluster) == 64, "TTCluster must be cache-line aligned");

    struct TTProbeResult {
        bool hit = false;
        int score = 0;
        int depth = -1;
        TTBound bound = TTBound::Empty;
        Move move;
        bool hasMove = false;
        int staticEval = 0;
        bool hasStaticEval = false;
    };

    TTCluster* tt = nullptr;
    uint64_t ttClusterCount = 0;
    uint64_t ttClusterMask = 0;
    uint64_t ttUsedEntries = 0;
    uint8_t currentGeneration = 0;

    void resizeTranspositionTable(int megabytes);
    void clearTT();
    void newSearch();
    TTProbeResult probeTT(uint64_t key, int ply) const;
    void storeTT(uint64_t key, int depth, int score, TTBound bound,
        const Move& bestMove, int ply, int staticEval = TT_NO_STATIC_EVAL);
    int ttHashfullPermille() const;
    static uint8_t entryGeneration(const TTEntry& entry);
    static TTBound entryBound(const TTEntry& entry);
    static void setGenerationBound(TTEntry& entry, uint8_t generation, TTBound bound);
    uint8_t generationAge(uint8_t entryGeneration) const;
    int replacementScore(const TTEntry& entry) const;
    uint16_t key16(uint64_t key) const;
    uint16_t packMove(const Move& move) const;
    Move unpackMove(uint16_t packed) const;
    int16_t packStaticEval(int staticEval) const;
    int16_t scoreToTT(int score, int ply) const;
    int scoreFromTT(int16_t score, int ply) const;
    bool shouldStop();

    std::atomic<bool> stopSearch;
    std::chrono::steady_clock::time_point searchStart;
    int timeLimitMs = 1000;  // default: 5 seconds per move
    long long nodeLimit = 0;

    std::vector<PolyglotEntry> openingBook;
    bool externalDataInitialized = false;

    void initializeExternalData();
    bool loadOpeningBook(const std::string& filename);
    Move probeBook(board& b);

};

#endif
