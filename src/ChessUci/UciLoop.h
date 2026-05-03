#pragma once

#include <ChessEngine/EngineFacade.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

class UciLoop {
public:
    UciLoop();
    ~UciLoop();

    void run();

private:
    struct GoCommand {
        int depth = 128;
        int moveTimeMs = -1;
        int whiteTimeMs = -1;
        int blackTimeMs = -1;
        int whiteIncrementMs = 0;
        int blackIncrementMs = 0;
        int movesToGo = 30;
        bool infinite = false;
        bool perft = false;
        int perftDepth = 0;
    };

    void handleCommand(const std::string& line);
    void handleUci();
    void handleSetOption(const std::vector<std::string>& tokens);
    void handlePosition(const std::vector<std::string>& tokens);
    void handleGo(const std::vector<std::string>& tokens);
    void handlePerft(int depth);

    GoCommand parseGo(const std::vector<std::string>& tokens) const;
    chess::SearchLimits makeSearchLimits(const GoCommand& command) const;

    void resetPosition();
    void recordCurrentPosition();
    bool isIrreversibleMoveUci(const std::string& uciMove) const;
    void startSearch(const chess::SearchLimits& limits);
    void stopSearch();
    void writeLine(const std::string& text);

    static std::vector<std::string> split(const std::string& line);
    static std::string joinTokens(const std::vector<std::string>& tokens, int first, int last);
    static int parseInt(const std::string& text, int fallback);

    chess::ChessEngine engine;
    std::vector<uint64_t> repetitionHistory;
    std::thread searchThread;
    std::atomic<bool> searchRunning{ false };
    int moveOverheadMs = 30;
    int hashSizeMb = 128;
    int searchThreads = 1;
    bool quitRequested = false;
};
