#ifndef ENGINE_H
#define ENGINE_H

#include "Board.h"
#include "ChessApi.h"
#include "MoveGenerator.h"
#include "ZobristHashing.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "Syzygy.h"
#include "Polyglot.h"
#include <unordered_map>

class SharedTranspositionTable {
public:
    struct alignas(16) Entry {
        std::atomic<uint64_t> data{ 0 };
        std::atomic<uint64_t> keyXorData{ 0 };
    };

    struct alignas(64) Cluster {
        Entry entries[4];
    };

    SharedTranspositionTable();
    ~SharedTranspositionTable();

    SharedTranspositionTable(const SharedTranspositionTable&) = delete;
    SharedTranspositionTable& operator=(const SharedTranspositionTable&) = delete;

    void resize(int megabytes);
    void clear();
    uint8_t newSearch();

    Cluster* clusters() const;
    uint64_t clusterCount() const;
    uint64_t clusterMask() const;
    uint64_t usedEntries() const;
    void noteNewEntry();

private:
    Cluster* table = nullptr;
    uint64_t count = 0;
    uint64_t mask = 0;
    std::atomic<uint64_t> used{ 0 };
    std::atomic<unsigned> generation{ 0 };
};

class SharedSearchControl {
public:
    void start(int milliseconds, long long nodes);
    void clearStop();
    void requestStop();
    bool isStopRequested() const;
    bool shouldStop();
    long long countNode();
    long long nodesSearched() const;
    std::chrono::steady_clock::time_point startTime() const;

private:
    alignas(64) std::atomic<bool> stop{ false };
    alignas(64) std::atomic<long long> nodes{ 0 };
    std::chrono::steady_clock::time_point started{};
    int timeLimitMs = 1000;
    long long nodeLimit = 0;
};

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
    void setSharedTranspositionTable(const std::shared_ptr<SharedTranspositionTable>& table);
    std::shared_ptr<SharedTranspositionTable> sharedTranspositionTable() const;
    void setSharedSearchControl(const std::shared_ptr<SharedSearchControl>& control);
    void setWorkerId(int id);
    void setEmitUciInfo(bool enabled);
    void clearStop();
    void requestStop();
    int lastSearchScore() const;
    int lastSearchDepth() const;
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

    struct DecodedTTEntry {
        uint16_t move16 = 0;
        int16_t score = 0;
        int16_t staticEval = TT_NO_STATIC_EVAL;
        uint8_t depth = 0;
        uint8_t generationBound = 0;
    };

    std::shared_ptr<SharedTranspositionTable> tt;
    std::shared_ptr<SharedSearchControl> sharedControl;
    uint8_t currentGeneration = 0;

    void resizeTranspositionTable(int megabytes);
    void clearTT();
    void newSearch();
    TTProbeResult probeTT(uint64_t key, int ply);
    void storeTT(uint64_t key, int depth, int score, TTBound bound,
        const Move& bestMove, int ply, int staticEval = TT_NO_STATIC_EVAL);
    int ttHashfullPermille() const;
    static uint8_t entryGeneration(const DecodedTTEntry& entry);
    static TTBound entryBound(const DecodedTTEntry& entry);
    static uint8_t makeGenerationBound(uint8_t generation, TTBound bound);
    static uint64_t packTTEntry(const DecodedTTEntry& entry);
    static DecodedTTEntry unpackTTEntry(uint64_t data);
    uint8_t generationAge(uint8_t entryGeneration) const;
    int replacementScore(const DecodedTTEntry& entry) const;
    uint16_t packMove(const Move& move) const;
    Move unpackMove(uint16_t packed) const;
    int16_t packStaticEval(int staticEval) const;
    int16_t scoreToTT(int score, int ply) const;
    int scoreFromTT(int16_t score, int ply) const;
    bool shouldStop();
    long long countNode();
    long long reportedNodeCount() const;

    std::atomic<bool> stopSearch;
    long long totalNodes = 0;
    long long leafNodes = 0;
    std::chrono::steady_clock::time_point searchStart;
    int timeLimitMs = 1000;  // default: 5 seconds per move
    long long nodeLimit = 0;
    int workerId = 0;
    bool emitSearchInfo = true;
    int lastScore = 0;
    int lastDepth = 0;

    std::vector<PolyglotEntry> openingBook;
    bool externalDataInitialized = false;

    void initializeExternalData();
    bool loadOpeningBook(const std::string& filename);
    Move probeBook(board& b);

};

#endif
