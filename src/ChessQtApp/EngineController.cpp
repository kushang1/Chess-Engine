#include "EngineController.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QMutexLocker>
#include <QtConcurrent>

EngineController::EngineController(QObject* parent)
    : QObject(parent)
{
}

EngineController::~EngineController()
{
    stopSearch();
}

void EngineController::startSearch(const std::string& fen,
                                   const std::vector<uint64_t>& repetitionHistory,
                                   EngineDifficulty difficulty,
                                   int generation)
{
    stopSearch();

    const int depth = difficultyDepth(difficulty);
    const int moveTimeMs = difficultyMoveTimeMs(difficulty);
    const int threads = difficultyThreadCount(difficulty);
    const int hashSizeMb = difficultyHashSizeMb(difficulty);
    auto searchEngine = std::make_shared<chess::ChessEngine>();
    {
        QMutexLocker locker(&m_searchMutex);
        m_activeSearch = searchEngine;
    }

    emit searchStarted(generation);

    auto* watcher = new QFutureWatcher<chess::SearchResult>(this);
    connect(watcher, &QFutureWatcher<chess::SearchResult>::finished, this,
            [this, watcher, generation, searchEngine]() {
                const chess::SearchResult result = watcher->future().result();
                emit searchFinished(generation,
                                    result.bestMove,
                                    result.nodes,
                                    result.leafNodes,
                                    result.completedDepth,
                                    result.elapsedMs,
                                    result.bestScore,
                                    result.threads);
                {
                    QMutexLocker locker(&m_searchMutex);
                    if (m_activeSearch == searchEngine) {
                        m_activeSearch.reset();
                    }
                }
                watcher->deleteLater();
            });

    QFuture<chess::SearchResult> future = QtConcurrent::run(
        [fen, repetitionHistory, depth, moveTimeMs, threads, hashSizeMb, searchEngine]() {
            searchEngine->setHashSizeMb(hashSizeMb);
            searchEngine->setThreadCount(threads);
            searchEngine->setPositionFromFen(fen);

            chess::SearchLimits limits;
            limits.maxDepth = depth;
            limits.moveTimeMs = moveTimeMs;
            return searchEngine->findBestMove(limits, repetitionHistory);
        });

    watcher->setFuture(future);
}

void EngineController::stopSearch()
{
    std::shared_ptr<chess::ChessEngine> active;
    {
        QMutexLocker locker(&m_searchMutex);
        active = m_activeSearch;
    }
    if (active) {
        active->stopSearch();
    }
}
