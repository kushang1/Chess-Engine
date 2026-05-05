#include "EngineController.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>

EngineController::EngineController(QObject* parent)
    : QObject(parent)
{
}

void EngineController::startSearch(const std::string& fen,
                                   const std::vector<uint64_t>& repetitionHistory,
                                   EngineDifficulty difficulty,
                                   int generation)
{
    const int depth = difficultyDepth(difficulty);
    const int moveTimeMs = difficultyMoveTimeMs(difficulty);

    emit searchStarted(generation);

    auto* watcher = new QFutureWatcher<chess::SearchResult>(this);
    connect(watcher, &QFutureWatcher<chess::SearchResult>::finished, this,
            [this, watcher, generation, depth, moveTimeMs]() {
                const chess::SearchResult result = watcher->future().result();
                emit searchFinished(generation,
                                    result.bestMove,
                                    result.nodes,
                                    result.leafNodes,
                                    depth,
                                    moveTimeMs);
                watcher->deleteLater();
            });

    QFuture<chess::SearchResult> future = QtConcurrent::run(
        [fen, repetitionHistory, depth, moveTimeMs]() {
            chess::ChessEngine searchEngine;
            searchEngine.setPositionFromFen(fen);

            chess::SearchLimits limits;
            limits.maxDepth = depth;
            limits.moveTimeMs = moveTimeMs;
            searchEngine.clearSearchStop();
            return searchEngine.findBestMove(limits, repetitionHistory);
        });

    watcher->setFuture(future);
}
