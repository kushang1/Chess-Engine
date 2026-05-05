#pragma once

#include "GameSettings.h"

#include <ChessEngine/EngineFacade.h>

#include <QObject>

#include <cstdint>
#include <string>
#include <vector>

class EngineController : public QObject
{
    Q_OBJECT

public:
    explicit EngineController(QObject* parent = nullptr);

    void startSearch(const std::string& fen,
                     const std::vector<uint64_t>& repetitionHistory,
                     EngineDifficulty difficulty,
                     int generation);

signals:
    void searchStarted(int generation);
    void searchFinished(int generation,
                        Move bestMove,
                        long long nodes,
                        long long leafNodes,
                        int depth,
                        int moveTimeMs);
};
