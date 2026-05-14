#ifndef PROFILER_H
#define PROFILER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ChessApi.h"

namespace Profiler {

constexpr int MAX_PROFILE_PLY = 128;

enum CounterId : int {
    SearchNodes = 0,
    LeafNodes,
    MaxPly,
    BetaCutoffs,
    AlphaRaises,
    FirstMoveBetaCutoffs,
    CutoffMoveIndex0,
    CutoffMoveIndex1,
    CutoffMoveIndex2,
    CutoffMoveIndex3,
    CutoffMoveIndex4,
    CutoffMoveIndex5To7,
    CutoffMoveIndex8To15,
    CutoffMoveIndex16Plus,
    CaptureMovesSearched,
    QuietMovesSearched,
    CheckExtensions,
    PvsAttempts,
    PvsResearches,
    NullMoveAttempts,
    NullMoveCutoffs,
    LmpAttempts,
    LmpPrunes,
    LmrAttempts,
    LmrApplied,
    LmrResearches,
    QSearchNodes,
    QSearchMaxPly,
    StandPatBetaCutoffs,
    StandPatAlphaRaises,
    QMovesSearched,
    QDeltaPrunes,
    QSeePrunes,
    QBetaCutoffs,
    QAlphaRaises,
    QSearchInCheckCalls,
    QGeneratedMovesTotal,
    QCandidateMoves,
    QMovesSkippedNotCaptureOrPromotionOrCheck,
    QDeltaPruneAttempts,
    QSeePruneAttempts,
    QMovesAfterPruning,
    QMovesActuallySearched,
    QQuietChecksGenerated,
    QQuietChecksSearched,
    QPromotionsGenerated,
    QPromotionsSearched,
    QCapturesGenerated,
    QCapturesSearched,
    QInCheckEvasionNodes,
    QNonCheckNodes,
    QStandPatEvaluations,
    QNodesAtOrBeyondPly8,
    QNodesAtOrBeyondPly12,
    QNodesAtOrBeyondPly16,
    QMaxPlyHits,
    QMaxPlyLimitReturns,
    MoveGeneration,
    PseudoMovegenCalls,
    LegalMovegenCalls,
    QMovegenCalls,
    CountLegalMoveCalls,
    GeneratedPseudoMovesTotal,
    GeneratedLegalMovesTotal,
    GeneratedQMovesTotal,
    PseudoMovegenTime,
    LegalMovegenTime,
    QMovegenTime,
    CountLegalMoveTime,
    LegalMovegenFromSearch,
    LegalMovegenFromQsearch,
    LegalMovegenFromSEE,
    LegalMovegenFromPerft,
    LegalMovegenFromRoot,
    LegalMovegenOther,
    QMovegenFromQsearch,
    QMovegenOther,
    QuiescenceMoveGeneration,
    LegalityChecking,
    IsSquareAttackedCalls,
    IsSquareAttackedTime,
    AttackersToCalls,
    InCheckCalls,
    InCheckTime,
    FindKingCalls,
    FindKingTime,
    MakeMove,
    UndoMove,
    MakeMoveCalls,
    UnmakeMoveCalls,
    MakeMoveTime,
    UnmakeMoveTime,
    Evaluation,
    EvalCalls,
    EvalTime,
    EvalMaterialPstTime,
    EvalMobilityActivityTime,
    EvalPawnStructureTime,
    EvalKingSafetyTime,
    EvalPassedPawnTime,
    ScoreMoveCalls,
    TTMoveScoreHits,
    PromotionScored,
    CaptureScored,
    QuietScored,
    KillerScored,
    HistoryScored,
    ApproximateSeeCalls,
    ApproximateSeeTime,
    ApproximateSeePositive,
    ApproximateSeeNegative,
    ApproximateSeeEqual,
    ApproximateSeeUnknownOrOther,
    ApproximateSeeForMoveOrdering,
    ApproximateSeeForMainSearchPruning,
    ApproximateSeeForQsearchPruning,
    ApproximateSeeForQsearchMoveOrdering,
    ApproximateSeeForPromotionHandling,
    ApproximateSeeOther,
    ApproximateSeeOnCapture,
    ApproximateSeeOnPromotion,
    ApproximateSeeOnQuiet,
    ApproximateSeeMakeMoveCalls,
    ApproximateSeeUnmakeMoveCalls,
    ApproximateSeeLegalMovegenCalls,
    ApproximateSeeReplyMovesGenerated,
    ApproximateSeeRecaptureCandidates,
    ApproximateSeeEarlyReturns,
    ApproximateSeeBoardCopies,
    TTProbes,
    TTHits,
    TTMisses,
    TTExactHits,
    TTAlphaHits,
    TTBetaHits,
    TTCutoffs,
    TTStores,
    TTOverwrites,
    TTKeptDueToDepth,
    TTMoveTried,
    TTMoveCutoffs,
    PruningAttempts,
    PruningCutoffs,
    MoveOrderingCutoffs,
    QSearchStats,
    PerftNodes,
    PerftCalls,
    PerftDepth,
    PerftTime,
    PerftDivideCalls,
    SlidingAttack,
    HashState,
    MemoryAllocationCalls,
    MemoryAllocationBytes,
    MemoryAllocationNanoseconds,
    NodesByPly0,
    NodesByPlyLast = NodesByPly0 + MAX_PROFILE_PLY - 1,
    QNodesByPly0,
    QNodesByPlyLast = QNodesByPly0 + MAX_PROFILE_PLY - 1,
    CounterCount,

    BucketCount = CounterCount
};

using Bucket = CounterId;

struct Counter {
    uint64_t value = 0;
    uint64_t maxValue = 0;
    uint64_t calls = 0;
};

enum class MovegenContext : int {
    Other,
    Search,
    Qsearch,
    See,
    Perft,
    Root
};

enum class SeeContext : int {
    Other,
    MoveOrdering,
    MainSearchPruning,
    QsearchPruning,
    QsearchMoveOrdering,
    PromotionHandling
};

CHESS_API bool isCompiledIn();
CHESS_API void reset();
CHESS_API std::string report();
CHESS_API uint64_t value(CounterId counter);

#ifdef ENABLE_ENGINE_PROFILING

CHESS_API void increment(CounterId counter);
CHESS_API void add(CounterId counter, uint64_t amount);
CHESS_API void recordMax(CounterId counter, uint64_t sample);
CHESS_API void recordTimer(CounterId counter, uint64_t nanoseconds);
CHESS_API void recordAllocation(std::size_t bytes, uint64_t nanoseconds);
CHESS_API MovegenContext currentMovegenContext();
CHESS_API MovegenContext setMovegenContext(MovegenContext context);
CHESS_API SeeContext currentSeeContext();
CHESS_API SeeContext setSeeContext(SeeContext context);

class ScopedTimer {
public:
    explicit ScopedTimer(CounterId counter)
        : counter(counter), start(Clock::now()) {
    }

    ~ScopedTimer() {
        auto end = Clock::now();
        recordTimer(counter, static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    CounterId counter;
    Clock::time_point start;
};

class ScopedMovegenContext {
public:
    explicit ScopedMovegenContext(MovegenContext context)
        : previous(setMovegenContext(context)) {
    }

    ~ScopedMovegenContext() {
        setMovegenContext(previous);
    }

    ScopedMovegenContext(const ScopedMovegenContext&) = delete;
    ScopedMovegenContext& operator=(const ScopedMovegenContext&) = delete;

private:
    MovegenContext previous;
};

class ScopedSeeContext {
public:
    explicit ScopedSeeContext(SeeContext context)
        : previous(setSeeContext(context)) {
    }

    ~ScopedSeeContext() {
        setSeeContext(previous);
    }

    ScopedSeeContext(const ScopedSeeContext&) = delete;
    ScopedSeeContext& operator=(const ScopedSeeContext&) = delete;

private:
    SeeContext previous;
};

#else

inline void increment(CounterId) {}
inline void add(CounterId, uint64_t) {}
inline void recordMax(CounterId, uint64_t) {}
inline void recordTimer(CounterId, uint64_t) {}
inline void recordAllocation(std::size_t, uint64_t) {}

class ScopedTimer {
public:
    explicit ScopedTimer(CounterId) {}
};

#endif // ENABLE_ENGINE_PROFILING

} // namespace Profiler

#ifdef ENABLE_ENGINE_PROFILING
#define ENGINE_PROFILE_JOIN_IMPL(a, b) a##b
#define ENGINE_PROFILE_JOIN(a, b) ENGINE_PROFILE_JOIN_IMPL(a, b)
#define PROFILE_INC(x) (::Profiler::increment(static_cast<::Profiler::CounterId>(x)))
#define PROFILE_ADD(x, n) (::Profiler::add(static_cast<::Profiler::CounterId>(x), static_cast<uint64_t>(n)))
#define PROFILE_MAX(x, n) (::Profiler::recordMax(static_cast<::Profiler::CounterId>(x), static_cast<uint64_t>(n)))
#define PROFILE_TIMER(x) ::Profiler::ScopedTimer ENGINE_PROFILE_JOIN(engineProfileTimer_, __LINE__)(static_cast<::Profiler::CounterId>(x))
#define PROFILE_MOVEGEN_CONTEXT(x) ::Profiler::ScopedMovegenContext ENGINE_PROFILE_JOIN(engineProfileMovegenContext_, __LINE__)(::Profiler::MovegenContext::x)
#define PROFILE_SEE_CONTEXT(x) ::Profiler::ScopedSeeContext ENGINE_PROFILE_JOIN(engineProfileSeeContext_, __LINE__)(::Profiler::SeeContext::x)
#else
#define PROFILE_INC(x) ((void)0)
#define PROFILE_ADD(x, n) ((void)0)
#define PROFILE_MAX(x, n) ((void)0)
#define PROFILE_TIMER(x) ((void)0)
#define PROFILE_MOVEGEN_CONTEXT(x) ((void)0)
#define PROFILE_SEE_CONTEXT(x) ((void)0)
#endif // ENABLE_ENGINE_PROFILING

#endif // PROFILER_H
