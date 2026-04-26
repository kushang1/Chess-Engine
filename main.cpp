#include "mainwindow.h"

#include <QApplication>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <malloc.h>
#include <new>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

#include "board.h"
#include "engine.h"
#include "movegenerator.h"
#include "profiler.h"

namespace {

using ProfileClock = std::chrono::high_resolution_clock;

void* allocateAndProfile(std::size_t size, std::size_t alignment = 0)
{
    const std::size_t allocationSize = size == 0 ? 1 : size;
    if (!Profiler::countAllocations) {
        void* ptr = alignment == 0
            ? std::malloc(allocationSize)
            : _aligned_malloc(allocationSize, alignment);
        if (!ptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    auto start = ProfileClock::now();
    void* ptr = alignment == 0
        ? std::malloc(allocationSize)
        : _aligned_malloc(allocationSize, alignment);
    auto end = ProfileClock::now();

    if (!ptr) {
        throw std::bad_alloc();
    }

    Profiler::recordAllocation(allocationSize, static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    return ptr;
}

} // namespace

void* operator new(std::size_t size) {
    return allocateAndProfile(size);
}

void* operator new[](std::size_t size) {
    return allocateAndProfile(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAndProfile(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAndProfile(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    _aligned_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    _aligned_free(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    _aligned_free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    _aligned_free(ptr);
}

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

static int countFenFields(const std::string& fen)
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

static bool setupPositionFromArgs(board& b, MoveGenerator& moveGenerator, int argc, char* argv[], int startIndex)
{
    b.resetBoard();
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

        try {
            b.loadFEN(fen);
        }
        catch (const std::exception& e) {
            std::cerr << "Invalid FEN after --fen: " << e.what() << "\n";
            return false;
        }
    }

    return playMoveSequence(b, moveGenerator, argc, argv, moveStart);
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

    if (depth == 1) {
        return moveGenerator.countLegalMoves(b);
    }

    MoveList moves;
    moveGenerator.generateLegalMoves(b, moves);

    long long nodes = 0;
    if (depth == 2) {
        for (Move& move : moves) {
            Unmove undo = b.makeMove(move);
            nodes += moveGenerator.countLegalMoves(b);
            b.unmakeMove(move, undo);
        }
        return nodes;
    }

    for (Move& move : moves) {
        Unmove undo = b.makeMove(move);
        nodes += perft(depth - 1, b, moveGenerator);
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

static const char* bucketName(Profiler::Bucket bucket)
{
    switch (bucket) {
    case Profiler::MoveGeneration: return "Move generation";
    case Profiler::LegalityChecking: return "Legality checking";
    case Profiler::MakeMove: return "makeMove";
    case Profiler::UndoMove: return "undoMove";
    case Profiler::SlidingAttack: return "Sliding attack generation";
    case Profiler::MemoryAllocation: return "Memory allocations";
    case Profiler::HashState: return "Hashing / state updates";
    default: return "Unknown";
    }
}

static void writeProfileLine(std::ostream& out, const char* name, uint64_t ns, uint64_t calls,
    uint64_t bytes, uint64_t totalNs)
{
    double ms = static_cast<double>(ns) / 1'000'000.0;
    double pct = totalNs == 0 ? 0.0 : (static_cast<double>(ns) * 100.0 / static_cast<double>(totalNs));
    out << std::left << std::setw(30) << name
        << std::right << std::setw(12) << std::fixed << std::setprecision(3) << ms << " ms"
        << std::setw(10) << std::setprecision(2) << pct << "%"
        << std::setw(14) << calls << " calls";
    if (bytes != 0) {
        out << std::setw(14) << bytes << " bytes";
    }
    out << "\n";
}

static void writeProfileReport(std::ostream& out, int depth, long long nodes, uint64_t totalNs)
{
    out << "Perft profile depth " << depth << "\n";
    out << "Nodes: " << nodes << "\n";
    out << "Total time: " << std::fixed << std::setprecision(6)
        << (static_cast<double>(totalNs) / 1'000'000'000.0) << " seconds\n";
    out << "Nodes/sec: " << std::fixed << std::setprecision(0)
        << (static_cast<double>(nodes) * 1'000'000'000.0 / static_cast<double>(totalNs)) << "\n\n";

    out << "Breakdown:\n";
    for (int bucket = 0; bucket < Profiler::BucketCount; ++bucket) {
        const Profiler::Counter& counter = Profiler::counters[bucket];
        writeProfileLine(out, bucketName(static_cast<Profiler::Bucket>(bucket)),
            counter.nanoseconds, counter.calls, counter.bytes, totalNs);
    }

    uint64_t accountedNs =
        Profiler::counters[Profiler::MoveGeneration].nanoseconds +
        Profiler::counters[Profiler::MakeMove].nanoseconds +
        Profiler::counters[Profiler::UndoMove].nanoseconds +
        Profiler::counters[Profiler::MemoryAllocation].nanoseconds;
    uint64_t recursionNs = totalNs > accountedNs ? (totalNs - accountedNs) : 0;

    writeProfileLine(out, "Recursion / loop overhead", recursionNs, 0, 0, totalNs);
    out << "\nNote: legality, sliding attacks, and hashing/state updates are nested sub-costs inside movegen/make/undo.\n";
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
        MoveGenerator moveGenerator;
        if (!setupPositionFromArgs(b, moveGenerator, argc, argv, 3)) {
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

    if (argc >= 3 && std::string(argv[1]) == "--bench-search") {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            FILE* stream = nullptr;
            freopen_s(&stream, "CONOUT$", "w", stdout);
            freopen_s(&stream, "CONOUT$", "w", stderr);
        }

        int depth = std::stoi(argv[2]);
        board b;
        MoveGenerator moveGenerator;
        if (!setupPositionFromArgs(b, moveGenerator, argc, argv, 3)) {
            return 1;
        }

        Engine engine;
        engine.moveGenerator = &moveGenerator;
        std::vector<uint64_t> reps;

        Profiler::reset();
        Profiler::countAllocations = true;
        auto start = std::chrono::high_resolution_clock::now();
        Move best = engine.findBestMove(b, depth, reps);
        auto end = std::chrono::high_resolution_clock::now();
        Profiler::countAllocations = false;

        std::chrono::duration<double> elapsed = end - start;
        uint64_t allocationCalls = Profiler::allocationCalls.load(std::memory_order_relaxed);
        uint64_t allocationBytes = Profiler::allocationBytes.load(std::memory_order_relaxed);

        std::ofstream out("search_bench_result.txt");
        auto write = [&](std::ostream& os) {
            os << "Search bench depth " << depth << "\n";
            os << "Best move: " << moveToString(best) << "\n";
            os << "Time: " << elapsed.count() << " seconds\n";
            os << "Nodes: " << engine.nodesSearched() << "\n";
            os << "Leaf nodes: " << engine.leafNodesSearched() << "\n";
            os << "Nodes/sec: "
               << (elapsed.count() > 0.0
                   ? static_cast<long long>(engine.nodesSearched() / elapsed.count())
                   : 0)
               << "\n";
            os << "Heap allocations during search: " << allocationCalls << "\n";
            os << "Heap bytes during search: " << allocationBytes << "\n";
        };

        write(std::cout);
        if (out) {
            write(out);
        }

        return 0;
    }

    if (argc >= 3 &&
        (std::string(argv[1]) == "--perft" ||
         std::string(argv[1]) == "--divide" ||
         std::string(argv[1]) == "--perft-ref" ||
         std::string(argv[1]) == "--profile-perft")) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            FILE* stream = nullptr;
            freopen_s(&stream, "CONOUT$", "w", stdout);
            freopen_s(&stream, "CONOUT$", "w", stderr);
        }

        bool divide = (std::string(argv[1]) == "--divide");
        bool referencePerft = (std::string(argv[1]) == "--perft-ref");
        bool profilePerft = (std::string(argv[1]) == "--profile-perft");
        int depth = std::stoi(argv[2]);

        board b;

        MoveGenerator moveGenerator;
        if (!setupPositionFromArgs(b, moveGenerator, argc, argv, 3)) {
            return 1;
        }

        Profiler::reset();
        Profiler::enabled = profilePerft;
        Profiler::countAllocations = profilePerft;

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
        Profiler::countAllocations = false;
        Profiler::enabled = false;

        std::chrono::duration<double> elapsed = end - start;
        uint64_t totalNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        std::cout << (divide ? "Divide(" : "Perft(") << depth << ") nodes: " << nodes << "\n";
        std::cout << "Time: " << elapsed.count() << " seconds\n";

        if (out) {
            out << (divide ? "Divide(" : "Perft(") << depth << ") nodes: " << nodes << "\n";
            out << "Time: " << elapsed.count() << " seconds\n";
        }

        if (profilePerft) {
            std::ofstream profileOut("profile_result.txt");
            writeProfileReport(std::cout, depth, nodes, totalNs);
            if (profileOut) {
                writeProfileReport(profileOut, depth, nodes, totalNs);
            }
        }

        return 0;
    }

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
