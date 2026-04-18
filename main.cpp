#include "mainwindow.h"

#include <QApplication>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <windows.h>

#include "board.h"
#include "movegenerator.h"

static std::string squareToString(int sq)
{
    int row = sq / 8;
    int col = sq % 8;
    std::string out;
    out.push_back(static_cast<char>('a' + col));
    out.push_back(static_cast<char>('8' - row));
    return out;
}

static std::string moveToString(const Move& move)
{
    std::string out = squareToString(move.from) + squareToString(move.to);
    if (move.wasPromotion) {
        char promo = 'q';
        switch (move.promotedTo) {
        case WR: case BR: promo = 'r'; break;
        case WB: case BB: promo = 'b'; break;
        case WN: case BN: promo = 'n'; break;
        default: break;
        }
        out.push_back(promo);
    }
    return out;
}

static bool playMoveSequence(board& b, MoveGenerator& moveGenerator, int argc, char* argv[], int startIndex)
{
    for (int i = startIndex; i < argc; ++i) {
        std::string wanted = argv[i];
        auto moves = moveGenerator.generateLegalMoves(b);

        bool found = false;
        for (const Move& move : moves) {
            if (moveToString(move) == wanted) {
                b.makeMove(move);
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "Illegal move in sequence: " << wanted << "\n";
            return false;
        }
    }

    return true;
}

static void generateReferenceLegalMoves(board& b, MoveGenerator& moveGenerator, MoveList& legal)
{
    legal.clear();
    MoveList pseudo;
    moveGenerator.generatePseudoLegalMoves(b, pseudo);

    for (Move& move : pseudo) {
        Unmove undo = b.makeMove(move);
        bool movingSideIsWhite = !b.isWhiteTurn;
        int kingSq = b.kingSquare(movingSideIsWhite);
        if (kingSq != -1 && !moveGenerator.isSquareAttacked(b, kingSq, b.isWhiteTurn)) {
            legal.push_back(move);
        }
        b.unmakeMove(move, undo);
    }
}

static bool findMismatch(board& b, MoveGenerator& moveGenerator, int depth, std::vector<std::string>& line, std::ostream& out)
{
    MoveList direct;
    MoveList reference;
    moveGenerator.generateLegalMoves(b, direct);
    generateReferenceLegalMoves(b, moveGenerator, reference);

    std::set<std::string> directSet;
    std::set<std::string> referenceSet;

    for (const Move& move : direct) directSet.insert(moveToString(move));
    for (const Move& move : reference) referenceSet.insert(moveToString(move));

    if (directSet != referenceSet) {
        out << "Mismatch after line:";
        for (const std::string& move : line) {
            out << ' ' << move;
        }
        out << "\nDirect-only:\n";
        for (const std::string& move : directSet) {
            if (referenceSet.find(move) == referenceSet.end()) {
                out << "  " << move << "\n";
            }
        }
        out << "Reference-only:\n";
        for (const std::string& move : referenceSet) {
            if (directSet.find(move) == directSet.end()) {
                out << "  " << move << "\n";
            }
        }
        return true;
    }

    if (depth <= 1) {
        return false;
    }

    for (const Move& move : reference) {
        line.push_back(moveToString(move));
        Unmove undo = b.makeMove(move);
        bool found = findMismatch(b, moveGenerator, depth - 1, line, out);
        b.unmakeMove(move, undo);
        line.pop_back();
        if (found) {
            return true;
        }
    }

    return false;
}

static long long perft(int depth, board& b, MoveGenerator& moveGenerator)
{
    if (depth == 0) {
        return 1;
    }

    MoveList moves;
    moveGenerator.generateLegalMoves(b, moves);

    long long nodes = 0;
    for (Move& move : moves) {
        Unmove undo = b.makeMove(move);
        if (depth == 1) {
            ++nodes;
        }
        else {
            nodes += perft(depth - 1, b, moveGenerator);
        }
        b.unmakeMove(move, undo);
    }

    return nodes;
}

static long long perftReference(int depth, board& b, MoveGenerator& moveGenerator)
{
    if (depth == 0) {
        return 1;
    }

    MoveList moves;
    generateReferenceLegalMoves(b, moveGenerator, moves);

    long long nodes = 0;
    for (Move& move : moves) {
        Unmove undo = b.makeMove(move);
        if (depth == 1) {
            ++nodes;
        }
        else {
            nodes += perftReference(depth - 1, b, moveGenerator);
        }
        b.unmakeMove(move, undo);
    }

    return nodes;
}

int main(int argc, char* argv[])
{
    if (argc >= 3 && std::string(argv[1]) == "--find-mismatch") {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            FILE* stream = nullptr;
            freopen_s(&stream, "CONOUT$", "w", stdout);
            freopen_s(&stream, "CONOUT$", "w", stderr);
        }

        int depth = std::stoi(argv[2]);
        board b;
        b.resetBoard();
        MoveGenerator moveGenerator;
        if (!playMoveSequence(b, moveGenerator, argc, argv, 3)) {
            return 1;
        }

        std::ofstream out("mismatch_result.txt");
        std::vector<std::string> line;
        bool found = findMismatch(b, moveGenerator, depth, line, std::cout);
        if (out) {
            findMismatch(b, moveGenerator, depth, line, out);
        }
        if (!found) {
            std::cout << "No mismatch found up to depth " << depth << "\n";
            if (out) {
                out << "No mismatch found up to depth " << depth << "\n";
            }
        }
        return 0;
    }

    if (argc >= 3 &&
        (std::string(argv[1]) == "--perft" ||
         std::string(argv[1]) == "--divide" ||
         std::string(argv[1]) == "--perft-ref")) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            FILE* stream = nullptr;
            freopen_s(&stream, "CONOUT$", "w", stdout);
            freopen_s(&stream, "CONOUT$", "w", stderr);
        }

        bool divide = (std::string(argv[1]) == "--divide");
        bool referencePerft = (std::string(argv[1]) == "--perft-ref");
        int depth = std::stoi(argv[2]);

        board b;
        b.resetBoard();

        MoveGenerator moveGenerator;
        if (!playMoveSequence(b, moveGenerator, argc, argv, 3)) {
            return 1;
        }

        auto start = std::chrono::high_resolution_clock::now();
        long long nodes = 0;
        std::ofstream out("perft_result.txt");

        if (divide) {
            MoveList moves;
            moveGenerator.generateLegalMoves(b, moves);
            for (Move& move : moves) {
                Unmove undo = b.makeMove(move);
                long long childNodes = perft(depth - 1, b, moveGenerator);
                b.unmakeMove(move, undo);
                nodes += childNodes;
                std::cout << moveToString(move) << ": " << childNodes << "\n";
                if (out) {
                    out << moveToString(move) << ": " << childNodes << "\n";
                }
            }
        }
        else {
            nodes = referencePerft
                ? perftReference(depth, b, moveGenerator)
                : perft(depth, b, moveGenerator);
        }
        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> elapsed = end - start;
        std::cout << (divide ? "Divide(" : "Perft(") << depth << ") nodes: " << nodes << "\n";
        std::cout << "Time: " << elapsed.count() << " seconds\n";

        if (out) {
            out << (divide ? "Divide(" : "Perft(") << depth << ") nodes: " << nodes << "\n";
            out << "Time: " << elapsed.count() << " seconds\n";
        }

        return 0;
    }

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
