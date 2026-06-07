#include "MainWindow.h"

#include <ChessEngine/EngineFacade.h>

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

namespace {

void attachConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}

std::string squareToString(int square)
{
    std::string out;
    out.push_back(static_cast<char>('a' + (square & 7)));
    out.push_back(static_cast<char>('8' - (square >> 3)));
    return out;
}

std::string moveToString(const Move& move)
{
    std::string out = squareToString(move.from) + squareToString(move.to);
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

int countFenFields(const std::string& fen)
{
    int fields = 0;
    bool inField = false;
    for (char ch : fen) {
        if (ch == ' ') {
            inField = false;
        }
        else if (!inField) {
            inField = true;
            ++fields;
        }
    }
    return fields;
}

bool setupPositionFromArgs(chess::ChessEngine& engine, int argc, char* argv[], int startIndex)
{
    engine.newGame();
    int moveStart = startIndex;

    if (startIndex < argc && std::string(argv[startIndex]) == "--fen") {
        if (startIndex + 1 >= argc) {
            std::cerr << "Missing FEN after --fen\n";
            return false;
        }

        std::string fen = argv[startIndex + 1];
        moveStart = startIndex + 2;
        while (countFenFields(fen) < 6 && moveStart < argc) {
            fen += ' ';
            fen += argv[moveStart++];
        }

        if (countFenFields(fen) != 6) {
            std::cerr << "Invalid FEN after --fen: expected 6 fields\n";
            return false;
        }

        if (!engine.setPositionFromFen(fen)) {
            std::cerr << "Invalid FEN after --fen\n";
            return false;
        }
    }

    for (int i = moveStart; i < argc; ++i) {
        std::string move = argv[i];
        if (!engine.makeMoveUci(move)) {
            std::cerr << "Illegal move in sequence: " << move << "\n";
            return false;
        }
    }

    return true;
}

int runSearchBench(int argc, char* argv[])
{
    attachConsole();

    int depth = std::stoi(argv[2]);
    chess::ChessEngine engine;
    if (!setupPositionFromArgs(engine, argc, argv, 3)) {
        return 1;
    }

    chess::SearchLimits limits;
    limits.maxDepth = depth;

    auto start = std::chrono::high_resolution_clock::now();
    engine.clearSearchStop();
    chess::SearchResult result = engine.findBestMove(limits);
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    std::ofstream out("search_bench_result.txt");

    auto write = [&](std::ostream& os) {
        os << "Search bench depth " << depth << "\n";
        os << "Best move: " << moveToString(result.bestMove) << "\n";
        os << "Time: " << elapsed.count() << " seconds\n";
        os << "Nodes: " << result.nodes << "\n";
        os << "Leaf nodes: " << result.leafNodes << "\n";
        os << "Nodes/sec: "
           << (elapsed.count() > 0.0
               ? static_cast<long long>(result.nodes / elapsed.count())
               : 0)
           << "\n";
    };

    write(std::cout);
    if (out) {
        write(out);
    }

    return 0;
}

int runPerftCommand(int argc, char* argv[])
{
    attachConsole();

    std::string command = argv[1];
    bool divide = command == "--divide";
    int depth = std::stoi(argv[2]);

    chess::ChessEngine engine;
    if (!setupPositionFromArgs(engine, argc, argv, 3)) {
        return 1;
    }

    auto start = std::chrono::high_resolution_clock::now();
    long long nodes = 0;
    std::ofstream out("perft_result.txt");

    if (divide) {
        std::vector<chess::PerftDivideEntry> entries = engine.divide(depth);
        for (const chess::PerftDivideEntry& entry : entries) {
            nodes += entry.nodes;
            std::cout << moveToString(entry.move) << ": " << entry.nodes << "\n";
            if (out) {
                out << moveToString(entry.move) << ": " << entry.nodes << "\n";
            }
        }
    }
    else {
        chess::PerftResult result = engine.perft(depth);
        nodes = result.nodes;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << (divide ? "Divide(" : "Perft(") << depth << ") nodes: " << nodes << "\n";
    std::cout << "Time: " << elapsed.count() << " seconds\n";

    if (out) {
        out << (divide ? "Divide(" : "Perft(") << depth << ") nodes: " << nodes << "\n";
        out << "Time: " << elapsed.count() << " seconds\n";
    }

    if (command == "--profile-perft") {
        std::ofstream profileOut("profile_result.txt");
        if (profileOut) {
            profileOut << "Perft profile depth " << depth << "\n";
            profileOut << "Nodes: " << nodes << "\n";
            profileOut << "Total time: " << elapsed.count() << " seconds\n";
            profileOut << "Detailed timing counters are not exposed by the facade.\n";
        }
    }

    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        if (argc >= 3 && std::string(argv[1]) == "--bench-search") {
            return runSearchBench(argc, argv);
        }

        if (argc >= 3 &&
            (std::string(argv[1]) == "--perft" ||
             std::string(argv[1]) == "--divide" ||
             std::string(argv[1]) == "--perft-ref" ||
             std::string(argv[1]) == "--profile-perft")) {
            return runPerftCommand(argc, argv);
        }

        if (argc >= 3 && std::string(argv[1]) == "--find-mismatch") {
            attachConsole();
            std::cerr << "--find-mismatch is not exposed from ChessQtApp after the facade migration.\n";
            return 2;
        }
    }
    catch (const std::exception& e) {
        attachConsole();
        std::cerr << e.what() << "\n";
        return 1;
    }

    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    QFont appFont("Segoe UI Variable", 10);
    appFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(appFont);
    MainWindow window;
    window.show();
    return app.exec();
}
