#include "Profiler.h"

#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

constexpr int FlatCounterCount = static_cast<int>(Profiler::MemoryAllocationNanoseconds) + 1;

constexpr const char* CounterNames[FlatCounterCount] = {
    "searchNodes",
    "leafNodes",
    "maxPly",
    "betaCutoffs",
    "alphaRaises",
    "firstMoveBetaCutoffs",
    "cutoffMoveIndex[0]",
    "cutoffMoveIndex[1]",
    "cutoffMoveIndex[2]",
    "cutoffMoveIndex[3]",
    "cutoffMoveIndex[4]",
    "cutoffMoveIndex[5-7]",
    "cutoffMoveIndex[8-15]",
    "cutoffMoveIndex[16+]",
    "captureMovesSearched",
    "quietMovesSearched",
    "checkExtensions",
    "pvsAttempts",
    "pvsResearches",
    "nullMoveAttempts",
    "nullMoveCutoffs",
    "lmpAttempts",
    "lmpPrunes",
    "lmrAttempts",
    "lmrApplied",
    "lmrResearches",
    "qsearchNodes",
    "qsearchMaxPly",
    "standPatBetaCutoffs",
    "standPatAlphaRaises",
    "qMovesSearched",
    "qDeltaPrunes",
    "qSeePrunes",
    "qBetaCutoffs",
    "qAlphaRaises",
    "qsearchInCheckCalls",
    "qGeneratedMovesTotal",
    "qCandidateMoves",
    "qMovesSkippedNotCaptureOrPromotionOrCheck",
    "qDeltaPruneAttempts",
    "qSeePruneAttempts",
    "qMovesAfterPruning",
    "qMovesActuallySearched",
    "qQuietChecksGenerated",
    "qQuietChecksSearched",
    "qPromotionsGenerated",
    "qPromotionsSearched",
    "qCapturesGenerated",
    "qCapturesSearched",
    "qInCheckEvasionNodes",
    "qNonCheckNodes",
    "qStandPatEvaluations",
    "qNodesAtOrBeyondPly8",
    "qNodesAtOrBeyondPly12",
    "qNodesAtOrBeyondPly16",
    "qMaxPlyHits",
    "qMaxPlyLimitReturns",
    "moveGeneration",
    "pseudoMovegenCalls",
    "legalMovegenCalls",
    "qMovegenCalls",
    "countLegalMoveCalls",
    "generatedPseudoMovesTotal",
    "generatedLegalMovesTotal",
    "generatedQMovesTotal",
    "pseudoMovegenTime",
    "legalMovegenTime",
    "qMovegenTime",
    "countLegalMoveTime",
    "legalMovegenFromSearch",
    "legalMovegenFromQsearch",
    "legalMovegenFromSEE",
    "legalMovegenFromPerft",
    "legalMovegenFromRoot",
    "legalMovegenOther",
    "qMovegenFromQsearch",
    "qMovegenOther",
    "quiescenceMoveGeneration",
    "legalityChecking",
    "isSquareAttackedCalls",
    "isSquareAttackedTime",
    "attackersToCalls",
    "inCheckCalls",
    "inCheckTime",
    "findKingCalls",
    "findKingTime",
    "makeMove",
    "undoMove",
    "makeMoveCalls",
    "unmakeMoveCalls",
    "makeMoveTime",
    "unmakeMoveTime",
    "evaluation",
    "evalCalls",
    "evalTime",
    "evalMaterialPstTime",
    "evalMobilityActivityTime",
    "evalPawnStructureTime",
    "evalKingSafetyTime",
    "evalPassedPawnTime",
    "evalAttackGenerationTime",
    "scoreMoveCalls",
    "ttMoveScoreHits",
    "promotionScored",
    "captureScored",
    "quietScored",
    "killerScored",
    "historyScored",
    "approximateSeeCalls",
    "approximateSeeTime",
    "approximateSeePositive",
    "approximateSeeNegative",
    "approximateSeeEqual",
    "approximateSeeUnknownOrOther",
    "approximateSeeForMoveOrdering",
    "approximateSeeForMainSearchPruning",
    "approximateSeeForQsearchPruning",
    "approximateSeeForQsearchMoveOrdering",
    "approximateSeeForPromotionHandling",
    "approximateSeeOther",
    "approximateSeeOnCapture",
    "approximateSeeOnPromotion",
    "approximateSeeOnQuiet",
    "approximateSeeMakeMoveCalls",
    "approximateSeeUnmakeMoveCalls",
    "approximateSeeLegalMovegenCalls",
    "approximateSeeReplyMovesGenerated",
    "approximateSeeRecaptureCandidates",
    "approximateSeeEarlyReturns",
    "approximateSeeBoardCopies",
    "ttProbes",
    "ttHits",
    "ttMisses",
    "ttExactHits",
    "ttAlphaHits",
    "ttBetaHits",
    "ttCutoffs",
    "ttStores",
    "ttOverwrites",
    "ttKeptDueToDepth",
    "ttMoveTried",
    "ttMoveCutoffs",
    "ttSlotHits[0]",
    "ttSlotHits[1]",
    "ttSlotHits[2]",
    "ttSlotHits[3]",
    "ttCollisionMisses",
    "ttReplacedByAge",
    "ttReplacedByDepth",
    "ttLowerHits",
    "ttUpperHits",
    "ttHashfullPermille",
    "pruningAttempts",
    "pruningCutoffs",
    "moveOrderingCutoffs",
    "qsearchStats",
    "perftNodes",
    "perftCalls",
    "perftDepth",
    "perftTime",
    "perftDivideCalls",
    "slidingAttack",
    "hashState",
    "memoryAllocationCalls",
    "memoryAllocationBytes",
    "memoryAllocationNanoseconds"
};

bool isValidCounter(Profiler::CounterId counter)
{
    int index = static_cast<int>(counter);
    return index >= 0 && index < Profiler::CounterCount;
}

#ifdef ENABLE_ENGINE_PROFILING

Profiler::Counter g_counters[Profiler::CounterCount]{};
std::mutex g_counterMutex;
thread_local Profiler::MovegenContext g_movegenContext = Profiler::MovegenContext::Other;
thread_local Profiler::SeeContext g_seeContext = Profiler::SeeContext::Other;

void appendLine(std::string& out, const char* name, const Profiler::Counter& counter)
{
    char line[192];
    std::snprintf(line, sizeof(line), "%-32s value=%llu max=%llu calls=%llu\n",
        name,
        static_cast<unsigned long long>(counter.value),
        static_cast<unsigned long long>(counter.maxValue),
        static_cast<unsigned long long>(counter.calls));
    out += line;
}

void appendPlyCounters(std::string& out, const char* title, Profiler::CounterId first)
{
    out += title;
    out += "\n";

    bool any = false;
    for (int ply = 0; ply < Profiler::MAX_PROFILE_PLY; ++ply) {
        const Profiler::Counter& counter =
            g_counters[static_cast<int>(first) + ply];
        if (counter.value == 0 && counter.maxValue == 0 && counter.calls == 0) {
            continue;
        }

        any = true;
        char name[64];
        std::snprintf(name, sizeof(name), "%s[%d]", title, ply);
        appendLine(out, name, counter);
    }

    if (!any) {
        out += "  all zero\n";
    }
}

uint64_t counterValue(Profiler::CounterId counter)
{
    return g_counters[static_cast<int>(counter)].value;
}

void appendRateLine(std::string& out, const char* name, uint64_t numerator, uint64_t denominator)
{
    double percent = denominator == 0
        ? 0.0
        : (static_cast<double>(numerator) * 100.0) / static_cast<double>(denominator);

    char line[192];
    std::snprintf(line, sizeof(line), "%-32s %.2f%% (%llu/%llu)\n",
        name,
        percent,
        static_cast<unsigned long long>(numerator),
        static_cast<unsigned long long>(denominator));
    out += line;
}

void appendTimerSummaryLine(std::string& out, const char* name, const Profiler::Counter& counter)
{
    double average = counter.calls == 0
        ? 0.0
        : static_cast<double>(counter.value) / static_cast<double>(counter.calls);

    char line[192];
    std::snprintf(line, sizeof(line), "%-32s total=%llu calls=%llu avg=%.2f ns max=%llu\n",
        name,
        static_cast<unsigned long long>(counter.value),
        static_cast<unsigned long long>(counter.calls),
        average,
        static_cast<unsigned long long>(counter.maxValue));
    out += line;
}

void appendMovegenCategoryLine(std::string& out, const char* name,
    Profiler::CounterId callsCounter,
    Profiler::CounterId generatedCounter,
    Profiler::CounterId timerCounter)
{
    uint64_t calls = counterValue(callsCounter);
    uint64_t generated = counterValue(generatedCounter);
    double averageMoves = calls == 0
        ? 0.0
        : static_cast<double>(generated) / static_cast<double>(calls);

    char line[192];
    std::snprintf(line, sizeof(line), "%-32s calls=%llu generated=%llu avgMoves=%.2f\n",
        name,
        static_cast<unsigned long long>(calls),
        static_cast<unsigned long long>(generated),
        averageMoves);
    out += line;

    char timerName[64];
    std::snprintf(timerName, sizeof(timerName), "%sTimeNs", name);
    appendTimerSummaryLine(out, timerName, g_counters[timerCounter]);
}

void appendMovegenSummary(std::string& out)
{
    out += "movegen summary\n";
    appendMovegenCategoryLine(out, "pseudoMovegen",
        Profiler::PseudoMovegenCalls,
        Profiler::GeneratedPseudoMovesTotal,
        Profiler::PseudoMovegenTime);
    appendMovegenCategoryLine(out, "legalMovegen",
        Profiler::LegalMovegenCalls,
        Profiler::GeneratedLegalMovesTotal,
        Profiler::LegalMovegenTime);
    appendMovegenCategoryLine(out, "qMovegen",
        Profiler::QMovegenCalls,
        Profiler::GeneratedQMovesTotal,
        Profiler::QMovegenTime);
    appendLine(out, "countLegalMoveCalls", g_counters[Profiler::CountLegalMoveCalls]);
    appendTimerSummaryLine(out, "countLegalMoveTimeNs", g_counters[Profiler::CountLegalMoveTime]);
}

void appendMovegenContextSummary(std::string& out)
{
    out += "movegen context summary\n";
    appendLine(out, "legalMovegenFromSearch", g_counters[Profiler::LegalMovegenFromSearch]);
    appendLine(out, "legalMovegenFromQsearch", g_counters[Profiler::LegalMovegenFromQsearch]);
    appendLine(out, "legalMovegenFromSEE", g_counters[Profiler::LegalMovegenFromSEE]);
    appendLine(out, "legalMovegenFromPerft", g_counters[Profiler::LegalMovegenFromPerft]);
    appendLine(out, "legalMovegenFromRoot", g_counters[Profiler::LegalMovegenFromRoot]);
    appendLine(out, "legalMovegenOther", g_counters[Profiler::LegalMovegenOther]);
    appendLine(out, "qMovegenFromQsearch", g_counters[Profiler::QMovegenFromQsearch]);
    appendLine(out, "qMovegenOther", g_counters[Profiler::QMovegenOther]);
}

void appendAttackCheckSummary(std::string& out)
{
    out += "attack/check summary\n";
    appendLine(out, "isSquareAttackedCalls", g_counters[Profiler::IsSquareAttackedCalls]);
    appendTimerSummaryLine(out, "isSquareAttackedTimeNs", g_counters[Profiler::IsSquareAttackedTime]);
    appendLine(out, "attackersToCalls", g_counters[Profiler::AttackersToCalls]);
    appendLine(out, "inCheckCalls", g_counters[Profiler::InCheckCalls]);
    appendTimerSummaryLine(out, "inCheckTimeNs", g_counters[Profiler::InCheckTime]);
    appendLine(out, "findKingCalls", g_counters[Profiler::FindKingCalls]);
    appendTimerSummaryLine(out, "findKingTimeNs", g_counters[Profiler::FindKingTime]);
}

void appendBoardMoveSummary(std::string& out)
{
    out += "board move summary\n";
    appendLine(out, "makeMoveCalls", g_counters[Profiler::MakeMoveCalls]);
    appendTimerSummaryLine(out, "makeMoveTimeNs", g_counters[Profiler::MakeMoveTime]);
    appendLine(out, "unmakeMoveCalls", g_counters[Profiler::UnmakeMoveCalls]);
    appendTimerSummaryLine(out, "unmakeMoveTimeNs", g_counters[Profiler::UnmakeMoveTime]);
}

void appendPerftSummary(std::string& out)
{
    uint64_t nodes = counterValue(Profiler::PerftNodes);
    uint64_t timeNs = counterValue(Profiler::PerftTime);
    double nps = timeNs == 0
        ? 0.0
        : (static_cast<double>(nodes) * 1000000000.0) / static_cast<double>(timeNs);

    char line[192];
    out += "perft summary\n";
    std::snprintf(line, sizeof(line), "%-32s max=%llu\n",
        "perftDepth",
        static_cast<unsigned long long>(g_counters[Profiler::PerftDepth].maxValue));
    out += line;
    appendLine(out, "perftCalls", g_counters[Profiler::PerftCalls]);
    appendLine(out, "perftDivideCalls", g_counters[Profiler::PerftDivideCalls]);
    appendLine(out, "perftNodes", g_counters[Profiler::PerftNodes]);
    appendTimerSummaryLine(out, "perftTimeNs", g_counters[Profiler::PerftTime]);
    std::snprintf(line, sizeof(line), "%-32s %.2f\n", "perftNps", nps);
    out += line;
    std::snprintf(line, sizeof(line),
        "%-32s pseudo=%llu legal=%llu q=%llu countLegal=%llu\n",
        "perftMovegenCalls",
        static_cast<unsigned long long>(counterValue(Profiler::PseudoMovegenCalls)),
        static_cast<unsigned long long>(counterValue(Profiler::LegalMovegenCalls)),
        static_cast<unsigned long long>(counterValue(Profiler::QMovegenCalls)),
        static_cast<unsigned long long>(counterValue(Profiler::CountLegalMoveCalls)));
    out += line;
    std::snprintf(line, sizeof(line), "%-32s make=%llu unmake=%llu\n",
        "perftMakeUnmakeCalls",
        static_cast<unsigned long long>(counterValue(Profiler::MakeMoveCalls)),
        static_cast<unsigned long long>(counterValue(Profiler::UnmakeMoveCalls)));
    out += line;
}

void appendQSearchSummary(std::string& out)
{
    uint64_t qnodes = counterValue(Profiler::QSearchNodes);

    out += "qsearch detail\n";
    appendLine(out, "qsearchNodes", g_counters[Profiler::QSearchNodes]);
    appendLine(out, "qGeneratedMovesTotal", g_counters[Profiler::QGeneratedMovesTotal]);
    appendLine(out, "qCandidateMoves", g_counters[Profiler::QCandidateMoves]);
    appendLine(out, "qMovesAfterPruning", g_counters[Profiler::QMovesAfterPruning]);
    appendLine(out, "qMovesSearchedExisting", g_counters[Profiler::QMovesSearched]);
    appendLine(out, "qMovesActuallySearched", g_counters[Profiler::QMovesActuallySearched]);
    appendLine(out, "qMovesSkippedNotCaptureOrPromotionOrCheck",
        g_counters[Profiler::QMovesSkippedNotCaptureOrPromotionOrCheck]);
    appendRateLine(out, "qDeltaPruneRate",
        counterValue(Profiler::QDeltaPrunes),
        counterValue(Profiler::QDeltaPruneAttempts));
    appendRateLine(out, "qSeePruneRate",
        counterValue(Profiler::QSeePrunes),
        counterValue(Profiler::QSeePruneAttempts));
    appendLine(out, "qQuietChecksGenerated", g_counters[Profiler::QQuietChecksGenerated]);
    appendLine(out, "qQuietChecksSearched", g_counters[Profiler::QQuietChecksSearched]);
    appendLine(out, "qPromotionsGenerated", g_counters[Profiler::QPromotionsGenerated]);
    appendLine(out, "qPromotionsSearched", g_counters[Profiler::QPromotionsSearched]);
    appendLine(out, "qCapturesGenerated", g_counters[Profiler::QCapturesGenerated]);
    appendLine(out, "qCapturesSearched", g_counters[Profiler::QCapturesSearched]);
    appendLine(out, "qInCheckEvasionNodes", g_counters[Profiler::QInCheckEvasionNodes]);
    appendLine(out, "qNonCheckNodes", g_counters[Profiler::QNonCheckNodes]);
    appendLine(out, "qStandPatEvaluations", g_counters[Profiler::QStandPatEvaluations]);
    appendLine(out, "qMaxPlyHits", g_counters[Profiler::QMaxPlyHits]);
    appendLine(out, "qMaxPlyLimitReturns", g_counters[Profiler::QMaxPlyLimitReturns]);
    appendRateLine(out, "qNodesAtOrBeyondPly8Rate",
        counterValue(Profiler::QNodesAtOrBeyondPly8), qnodes);
    appendRateLine(out, "qNodesAtOrBeyondPly12Rate",
        counterValue(Profiler::QNodesAtOrBeyondPly12), qnodes);
    appendRateLine(out, "qNodesAtOrBeyondPly16Rate",
        counterValue(Profiler::QNodesAtOrBeyondPly16), qnodes);
}

void appendEvaluationSummary(std::string& out)
{
    uint64_t seeCalls = counterValue(Profiler::ApproximateSeeCalls);
    uint64_t seePositive = counterValue(Profiler::ApproximateSeePositive);
    uint64_t seeEqual = counterValue(Profiler::ApproximateSeeEqual);
    uint64_t seeNegative = counterValue(Profiler::ApproximateSeeNegative);
    uint64_t seeClassified = seePositive + seeEqual + seeNegative;
    uint64_t seeOther = counterValue(Profiler::ApproximateSeeUnknownOrOther);
    if (seeCalls > seeClassified) {
        seeOther += seeCalls - seeClassified;
    }

    out += "eval summary\n";
    appendLine(out, "evalCalls", g_counters[Profiler::EvalCalls]);
    appendTimerSummaryLine(out, "evalTimeNs", g_counters[Profiler::EvalTime]);
    appendTimerSummaryLine(out, "evalMaterialPstTimeNs", g_counters[Profiler::EvalMaterialPstTime]);
    appendTimerSummaryLine(out, "evalMobilityActivityTimeNs", g_counters[Profiler::EvalMobilityActivityTime]);
    appendTimerSummaryLine(out, "evalPassedPawnTimeNs", g_counters[Profiler::EvalPassedPawnTime]);
    appendTimerSummaryLine(out, "evalPawnStructureTimeNs", g_counters[Profiler::EvalPawnStructureTime]);
    appendTimerSummaryLine(out, "evalKingSafetyTimeNs", g_counters[Profiler::EvalKingSafetyTime]);
    appendTimerSummaryLine(out, "evalAttackGenerationTimeNs", g_counters[Profiler::EvalAttackGenerationTime]);

    out += "move ordering summary\n";
    appendLine(out, "scoreMoveCalls", g_counters[Profiler::ScoreMoveCalls]);

    out += "approximate see summary\n";
    appendLine(out, "approximateSeeCalls", g_counters[Profiler::ApproximateSeeCalls]);
    appendTimerSummaryLine(out, "approximateSeeTimeNs", g_counters[Profiler::ApproximateSeeTime]);
    appendRateLine(out, "approximateSeePositiveRate", seePositive, seeCalls);
    appendRateLine(out, "approximateSeeEqualRate", seeEqual, seeCalls);
    appendRateLine(out, "approximateSeeNegativeRate", seeNegative, seeCalls);
    appendRateLine(out, "approximateSeeOtherRate", seeOther, seeCalls);
    appendLine(out, "approximateSeeForMoveOrdering", g_counters[Profiler::ApproximateSeeForMoveOrdering]);
    appendLine(out, "approximateSeeForMainSearchPruning", g_counters[Profiler::ApproximateSeeForMainSearchPruning]);
    appendLine(out, "approximateSeeForQsearchPruning", g_counters[Profiler::ApproximateSeeForQsearchPruning]);
    appendLine(out, "approximateSeeForQsearchMoveOrdering", g_counters[Profiler::ApproximateSeeForQsearchMoveOrdering]);
    appendLine(out, "approximateSeeForPromotionHandling", g_counters[Profiler::ApproximateSeeForPromotionHandling]);
    appendLine(out, "approximateSeeOther", g_counters[Profiler::ApproximateSeeOther]);
    appendLine(out, "approximateSeeOnCapture", g_counters[Profiler::ApproximateSeeOnCapture]);
    appendLine(out, "approximateSeeOnPromotion", g_counters[Profiler::ApproximateSeeOnPromotion]);
    appendLine(out, "approximateSeeOnQuiet", g_counters[Profiler::ApproximateSeeOnQuiet]);
    appendLine(out, "approximateSeeMakeMoveCalls", g_counters[Profiler::ApproximateSeeMakeMoveCalls]);
    appendLine(out, "approximateSeeUnmakeMoveCalls", g_counters[Profiler::ApproximateSeeUnmakeMoveCalls]);
    appendLine(out, "approximateSeeLegalMovegenCalls", g_counters[Profiler::ApproximateSeeLegalMovegenCalls]);
    appendLine(out, "approximateSeeReplyMovesGenerated", g_counters[Profiler::ApproximateSeeReplyMovesGenerated]);
    appendLine(out, "approximateSeeRecaptureCandidates", g_counters[Profiler::ApproximateSeeRecaptureCandidates]);
    appendLine(out, "approximateSeeEarlyReturns", g_counters[Profiler::ApproximateSeeEarlyReturns]);
    appendLine(out, "approximateSeeBoardCopies", g_counters[Profiler::ApproximateSeeBoardCopies]);
}

void appendTTSummary(std::string& out)
{
    uint64_t probes = counterValue(Profiler::TTProbes);
    uint64_t hits = counterValue(Profiler::TTHits);
    uint64_t cutoffs = counterValue(Profiler::TTCutoffs);
    uint64_t slotHits =
        counterValue(Profiler::TTSlotHit0) +
        counterValue(Profiler::TTSlotHit1) +
        counterValue(Profiler::TTSlotHit2) +
        counterValue(Profiler::TTSlotHit3);

    out += "tt summary\n";
    appendRateLine(out, "ttHitRate", hits, probes);
    appendRateLine(out, "ttCutoffRate", cutoffs, probes);
    appendRateLine(out, "ttCollisionMissRate",
        counterValue(Profiler::TTCollisionMisses), probes);
    appendRateLine(out, "ttExactHitRate",
        counterValue(Profiler::TTExactHits), hits);
    appendRateLine(out, "ttLowerHitRate",
        counterValue(Profiler::TTLowerHits), hits);
    appendRateLine(out, "ttUpperHitRate",
        counterValue(Profiler::TTUpperHits), hits);
    appendRateLine(out, "ttSlot0HitShare",
        counterValue(Profiler::TTSlotHit0), slotHits);
    appendRateLine(out, "ttSlot1HitShare",
        counterValue(Profiler::TTSlotHit1), slotHits);
    appendRateLine(out, "ttSlot2HitShare",
        counterValue(Profiler::TTSlotHit2), slotHits);
    appendRateLine(out, "ttSlot3HitShare",
        counterValue(Profiler::TTSlotHit3), slotHits);
    appendLine(out, "ttReplacedByAge", g_counters[Profiler::TTReplacedByAge]);
    appendLine(out, "ttReplacedByDepth", g_counters[Profiler::TTReplacedByDepth]);
    char line[192];
    std::snprintf(line, sizeof(line), "%-32s %llu permille\n",
        "ttHashfullEstimate",
        static_cast<unsigned long long>(
            g_counters[Profiler::TTHashfullPermille].maxValue));
    out += line;
}

#endif // ENABLE_ENGINE_PROFILING

} // namespace

namespace Profiler {

bool isCompiledIn()
{
#ifdef ENABLE_ENGINE_PROFILING
    return true;
#else
    return false;
#endif
}

void reset()
{
#ifdef ENABLE_ENGINE_PROFILING
    std::lock_guard<std::mutex> lock(g_counterMutex);
    std::memset(g_counters, 0, sizeof(g_counters));
#endif
}

std::string report()
{
#ifndef ENABLE_ENGINE_PROFILING
    return "profiling not compiled in";
#else
    std::lock_guard<std::mutex> lock(g_counterMutex);
    std::string out;
    out.reserve(4096);
    out += "engine profiling report\n";
    out += "counter                          value max calls\n";
    for (int i = 0; i < FlatCounterCount; ++i) {
        appendLine(out, CounterNames[i], g_counters[i]);
    }
    appendPerftSummary(out);
    appendMovegenSummary(out);
    appendMovegenContextSummary(out);
    appendAttackCheckSummary(out);
    appendBoardMoveSummary(out);
    appendQSearchSummary(out);
    appendEvaluationSummary(out);
    appendTTSummary(out);
    appendPlyCounters(out, "nodesByPly", NodesByPly0);
    appendPlyCounters(out, "qnodesByPly", QNodesByPly0);
    return out;
#endif
}

uint64_t value(CounterId counter)
{
#ifndef ENABLE_ENGINE_PROFILING
    (void)counter;
    return 0;
#else
    if (!isValidCounter(counter)) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_counterMutex);
    return g_counters[static_cast<int>(counter)].value;
#endif
}

#ifdef ENABLE_ENGINE_PROFILING

void increment(CounterId counter)
{
    if (!isValidCounter(counter)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_counterMutex);
    ++g_counters[static_cast<int>(counter)].value;
}

void add(CounterId counter, uint64_t amount)
{
    if (!isValidCounter(counter)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_counterMutex);
    g_counters[static_cast<int>(counter)].value += amount;
}

void recordMax(CounterId counter, uint64_t sample)
{
    if (!isValidCounter(counter)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_counterMutex);
    Counter& current = g_counters[static_cast<int>(counter)];
    ++current.calls;
    if (sample > current.maxValue) {
        current.maxValue = sample;
    }
}

void recordTimer(CounterId counter, uint64_t nanoseconds)
{
    if (!isValidCounter(counter)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_counterMutex);
    Counter& current = g_counters[static_cast<int>(counter)];
    current.value += nanoseconds;
    ++current.calls;
    if (nanoseconds > current.maxValue) {
        current.maxValue = nanoseconds;
    }
}

void recordAllocation(std::size_t bytes, uint64_t nanoseconds)
{
    increment(MemoryAllocationCalls);
    add(MemoryAllocationBytes, static_cast<uint64_t>(bytes));
    add(MemoryAllocationNanoseconds, nanoseconds);
}

MovegenContext currentMovegenContext()
{
    return g_movegenContext;
}

MovegenContext setMovegenContext(MovegenContext context)
{
    MovegenContext previous = g_movegenContext;
    g_movegenContext = context;
    return previous;
}

SeeContext currentSeeContext()
{
    return g_seeContext;
}

SeeContext setSeeContext(SeeContext context)
{
    SeeContext previous = g_seeContext;
    g_seeContext = context;
    return previous;
}

#endif // ENABLE_ENGINE_PROFILING

} // namespace Profiler
