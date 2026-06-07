#pragma once

#include "GameSettings.h"

#include <ChessEngine/EngineFacade.h>

#include <QObject>
#include <QMutex>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class EngineController : public QObject
{
    Q_OBJECT

public:
    explicit EngineController(QObject* parent = nullptr);
    ~EngineController() override;

    void startSearch(const std::string& fen,
                     const std::vector<uint64_t>& repetitionHistory,
                     EngineDifficulty difficulty,
                     int generation);
    void stopSearch();

signals:
    void searchStarted(int generation);
    void searchFinished(int generation,
                        Move bestMove,
                        long long nodes,
                        long long leafNodes,
                        int depth,
                        int elapsedMs,
                        int bestScore,
                        int threads);

private:
    QMutex m_searchMutex;
    std::shared_ptr<chess::ChessEngine> m_activeSearch;
};
