#include "UciLoop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr int DefaultMoveTimeMs = 1000;
constexpr int InfiniteMoveTimeMs = 24 * 60 * 60 * 1000;

std::string moveToLongAlgebraic(const Move& move)
{
    if (move.from < 0 || move.to < 0) {
        return "0000";
    }

    auto square = [](int sq) {
        std::string out;
        out.push_back(static_cast<char>('a' + (sq & 7)));
        out.push_back(static_cast<char>('8' - (sq >> 3)));
        return out;
    };

    std::string out = square(move.from) + square(move.to);
    if (move.wasPromotion) {
        char promotion = 'q';
        switch (move.promotedTo) {
        case WR: case BR: promotion = 'r'; break;
        case WB: case BB: promotion = 'b'; break;
        case WN: case BN: promotion = 'n'; break;
        default: break;
        }
        out.push_back(promotion);
    }
    return out;
}

int squareFromUciText(const std::string& text, std::size_t offset)
{
    if (offset + 1 >= text.size()) {
        return -1;
    }

    char file = text[offset];
    char rank = text[offset + 1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return -1;
    }

    int col = file - 'a';
    int row = 8 - (rank - '0');
    return (row << 3) | col;
}

bool moveCanChangeCastlingRights(Piece piece, int from)
{
    return (piece == WK && from == 60) ||
        (piece == BK && from == 4) ||
        (piece == WR && (from == 56 || from == 63)) ||
        (piece == BR && (from == 0 || from == 7));
}

} // namespace

UciLoop::UciLoop()
{
#ifdef _WIN32
    _putenv_s("CHESS_ENGINE_UCI_INFO", "1");
#else
    setenv("CHESS_ENGINE_UCI_INFO", "1", 1);
#endif
    resetPosition();
}

UciLoop::~UciLoop()
{
    stopSearch();
}

void UciLoop::run()
{
    std::ios::sync_with_stdio(false);

    std::string line;
    while (!quitRequested && std::getline(std::cin, line)) {
        handleCommand(line);
    }

    stopSearch();
}

void UciLoop::handleCommand(const std::string& line)
{
    std::vector<std::string> tokens = split(line);
    if (tokens.empty()) {
        return;
    }

    const std::string& command = tokens[0];
    if (command == "uci") {
        handleUci();
    }
    else if (command == "isready") {
        writeLine("readyok");
    }
    else if (command == "setoption") {
        handleSetOption(tokens);
    }
    else if (command == "ucinewgame") {
        stopSearch();
        resetPosition();
    }
    else if (command == "position") {
        stopSearch();
        handlePosition(tokens);
    }
    else if (command == "go") {
        handleGo(tokens);
    }
    else if (command == "stop") {
        stopSearch();
    }
    else if (command == "quit") {
        quitRequested = true;
        stopSearch();
    }
    else if (command == "d") {
        writeLine("Fen: " + engine.currentFen());
    }
    else if (command == "ponderhit" || command == "debug" || command == "register") {
        // Accepted for GUI compatibility. No action is needed for this engine yet.
    }
}

void UciLoop::handleUci()
{
    writeLine("id name ChessEngine");
    writeLine("id author Kushang Panchal");
    writeLine("option name Hash type spin default 128 min 1 max 4096");
    writeLine("option name Threads type spin default 1 min 1 max 1");
    writeLine("option name Move Overhead type spin default 30 min 0 max 5000");
    writeLine("uciok");
}

void UciLoop::handleSetOption(const std::vector<std::string>& tokens)
{
    int nameIndex = -1;
    int valueIndex = -1;
    for (int i = 1; i < static_cast<int>(tokens.size()); ++i) {
        if (tokens[i] == "name") {
            nameIndex = i + 1;
        }
        else if (tokens[i] == "value") {
            valueIndex = i + 1;
            break;
        }
    }

    if (nameIndex < 0 || valueIndex < 0 || valueIndex >= static_cast<int>(tokens.size())) {
        return;
    }

    std::string name = joinTokens(tokens, nameIndex, valueIndex - 1);
    if (name == "Move Overhead") {
        moveOverheadMs = std::clamp(parseInt(tokens[valueIndex], moveOverheadMs), 0, 5000);
    }
    else if (name == "Hash") {
        hashSizeMb = std::clamp(parseInt(tokens[valueIndex], hashSizeMb), 1, 4096);
        stopSearch();
        engine.setHashSizeMb(hashSizeMb);
    }
    else if (name == "Threads") {
        searchThreads = std::clamp(parseInt(tokens[valueIndex], searchThreads), 1, 1);
    }
}

void UciLoop::handlePosition(const std::vector<std::string>& tokens)
{
    if (tokens.size() < 2) {
        return;
    }

    int moveIndex = -1;
    for (int i = 1; i < static_cast<int>(tokens.size()); ++i) {
        if (tokens[i] == "moves") {
            moveIndex = i;
            break;
        }
    }

    bool ok = false;
    if (tokens[1] == "startpos") {
        engine.newGame();
        ok = true;
    }
    else if (tokens[1] == "fen") {
        int fenEnd = moveIndex >= 0 ? moveIndex : static_cast<int>(tokens.size());
        std::string fen = joinTokens(tokens, 2, fenEnd - 1);
        ok = engine.setPositionFromFen(fen);
    }

    if (!ok) {
        writeLine("info string invalid position command");
        resetPosition();
        return;
    }

    repetitionHistory.clear();
    recordCurrentPosition();

    if (moveIndex < 0) {
        return;
    }

    for (int i = moveIndex + 1; i < static_cast<int>(tokens.size()); ++i) {
        bool irreversible = isIrreversibleMoveUci(tokens[i]);
        if (!engine.makeMoveUci(tokens[i])) {
            writeLine("info string illegal move ignored: " + tokens[i]);
            return;
        }
        if (irreversible) {
            repetitionHistory.clear();
        }
        recordCurrentPosition();
    }
}

void UciLoop::handleGo(const std::vector<std::string>& tokens)
{
    stopSearch();

    GoCommand command = parseGo(tokens);
    if (command.perft) {
        handlePerft(command.perftDepth);
        return;
    }

    startSearch(makeSearchLimits(command));
}

void UciLoop::handlePerft(int depth)
{
    auto entries = engine.divide(depth);
    long long nodes = 0;
    for (const chess::PerftDivideEntry& entry : entries) {
        nodes += entry.nodes;
        writeLine(moveToLongAlgebraic(entry.move) + ": " + std::to_string(entry.nodes));
    }
    writeLine("nodes " + std::to_string(nodes));
}

UciLoop::GoCommand UciLoop::parseGo(const std::vector<std::string>& tokens) const
{
    GoCommand command;

    for (int i = 1; i < static_cast<int>(tokens.size()); ++i) {
        const std::string& token = tokens[i];
        if (token == "depth" && i + 1 < static_cast<int>(tokens.size())) {
            command.depth = std::max(1, parseInt(tokens[++i], command.depth));
        }
        else if (token == "movetime" && i + 1 < static_cast<int>(tokens.size())) {
            command.moveTimeMs = std::max(1, parseInt(tokens[++i], DefaultMoveTimeMs));
        }
        else if (token == "wtime" && i + 1 < static_cast<int>(tokens.size())) {
            command.whiteTimeMs = parseInt(tokens[++i], -1);
        }
        else if (token == "btime" && i + 1 < static_cast<int>(tokens.size())) {
            command.blackTimeMs = parseInt(tokens[++i], -1);
        }
        else if (token == "winc" && i + 1 < static_cast<int>(tokens.size())) {
            command.whiteIncrementMs = std::max(0, parseInt(tokens[++i], 0));
        }
        else if (token == "binc" && i + 1 < static_cast<int>(tokens.size())) {
            command.blackIncrementMs = std::max(0, parseInt(tokens[++i], 0));
        }
        else if (token == "movestogo" && i + 1 < static_cast<int>(tokens.size())) {
            command.movesToGo = std::max(1, parseInt(tokens[++i], 30));
        }
        else if (token == "infinite") {
            command.infinite = true;
        }
        else if (token == "perft" && i + 1 < static_cast<int>(tokens.size())) {
            command.perft = true;
            command.perftDepth = std::max(1, parseInt(tokens[++i], 1));
        }
        else if (token == "searchmoves") {
            while (i + 1 < static_cast<int>(tokens.size())) {
                const std::string& next = tokens[i + 1];
                if (next == "wtime" || next == "btime" || next == "winc" || next == "binc" ||
                    next == "movestogo" || next == "depth" || next == "nodes" ||
                    next == "mate" || next == "movetime" || next == "infinite" ||
                    next == "ponder") {
                    break;
                }
                ++i;
            }
        }
        else if ((token == "nodes" || token == "mate") && i + 1 < static_cast<int>(tokens.size())) {
            ++i;
        }
    }

    return command;
}

chess::SearchLimits UciLoop::makeSearchLimits(const GoCommand& command) const
{
    chess::SearchLimits limits;
    limits.maxDepth = command.depth;

    if (command.infinite) {
        limits.moveTimeMs = InfiniteMoveTimeMs;
        return limits;
    }

    if (command.moveTimeMs > 0) {
        limits.moveTimeMs = std::max(1, command.moveTimeMs - moveOverheadMs);
        return limits;
    }

    int available = engine.isWhiteTurn() ? command.whiteTimeMs : command.blackTimeMs;
    int increment = engine.isWhiteTurn() ? command.whiteIncrementMs : command.blackIncrementMs;

    if (available > 0) {
        int base = available / std::max(1, command.movesToGo);
        int withIncrement = base + increment / 2;
        int capped = std::min(withIncrement, std::max(1, available / 3));
        limits.moveTimeMs = std::max(10, capped - moveOverheadMs);
    }
    else {
        limits.moveTimeMs = DefaultMoveTimeMs;
    }

    return limits;
}

void UciLoop::resetPosition()
{
    engine.newGame();
    repetitionHistory.clear();
    recordCurrentPosition();
}

void UciLoop::recordCurrentPosition()
{
    repetitionHistory.push_back(engine.positionHash());
}

bool UciLoop::isIrreversibleMoveUci(const std::string& uciMove) const
{
    int from = squareFromUciText(uciMove, 0);
    int to = squareFromUciText(uciMove, 2);
    if (from < 0 || to < 0) {
        return true;
    }

    Piece moving = engine.pieceAt(from);
    Piece captured = engine.pieceAt(to);

    return moving == WP ||
        moving == BP ||
        captured != EMPTY ||
        uciMove.size() >= 5 ||
        moveCanChangeCastlingRights(moving, from);
}

void UciLoop::startSearch(const chess::SearchLimits& limits)
{
    std::vector<uint64_t> repetitions = repetitionHistory;
    engine.clearSearchStop();
    searchRunning.store(true, std::memory_order_release);

    searchThread = std::thread([this, limits, repetitions]() {
        chess::SearchResult result = engine.findBestMove(limits, repetitions);
        std::string bestMove = moveToLongAlgebraic(result.bestMove);

        writeLine("bestmove " + bestMove);
        searchRunning.store(false, std::memory_order_release);
    });
}

void UciLoop::stopSearch()
{
    if (searchThread.joinable()) {
        if (searchRunning.load(std::memory_order_acquire)) {
            engine.stopSearch();
        }
        searchThread.join();
        searchRunning.store(false, std::memory_order_release);
    }
}

void UciLoop::writeLine(const std::string& text)
{
#ifdef _WIN32
    const std::string line = text + "\n";
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(output, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
#else
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
#endif
}

std::vector<std::string> UciLoop::split(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream input(line);
    std::string token;
    while (input >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string UciLoop::joinTokens(const std::vector<std::string>& tokens, int first, int last)
{
    if (first > last || first < 0 || last >= static_cast<int>(tokens.size())) {
        return "";
    }

    std::string out;
    for (int i = first; i <= last; ++i) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += tokens[i];
    }
    return out;
}

int UciLoop::parseInt(const std::string& text, int fallback)
{
    try {
        std::size_t parsed = 0;
        int value = std::stoi(text, &parsed);
        return parsed == text.size() ? value : fallback;
    }
    catch (...) {
        return fallback;
    }
}
